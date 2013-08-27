#include "nfsping.h"


READ3res *do_read(fsroots_t *dir) {
    READ3res *res;
    READ3args args;
    CLIENT client;
    const u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    int nfs_sock = RPC_ANYSOCK;
    struct rpc_err clnt_err;
    int i;
    unsigned int count = 0;

    dir->client_sock->sin_family = AF_INET;
    dir->client_sock->sin_port = htons(NFS_PORT);

    client = *clntudp_create(dir->client_sock, NFS_PROGRAM, version, timeout, &nfs_sock);
    client.cl_auth = authunix_create_default();

    args.file = dir->fsroot;
    args.offset = 0;
    args.count = 8192;

    res = nfsproc3_read_3(&args, &client);

    if (res) {
        while (res) {
            if (res->status != NFS3_OK) {
                clnt_geterr(&client, &clnt_err);                                                                                                                     
                if (clnt_err.re_status)
                    clnt_perror(&client, "nfsproc3_read_3");
                else
                    nfs_perror(res->status);
                break;
            } else {
                /* print the data in hex */
                /*
                for (i = 0; i < res->READ3res_u.resok.data.data_len; i++) {
                    printf("%02hhx", res->READ3res_u.resok.data.data_val[i]);
                }
                printf("\n");
                */
                fwrite(res->READ3res_u.resok.data.data_val, 1, res->READ3res_u.resok.data.data_len, stdout);

                if (res->READ3res_u.resok.eof) {
                    break;
                } else {
                    count += res->READ3res_u.resok.count;
                    fprintf(stderr, "count = %u\n", count);
                    args.offset += res->READ3res_u.resok.count;
                    res = nfsproc3_read_3(&args, &client);
                }
            }
        }
    } else {
        clnt_perror(&client, "nfsproc3_read_3");
    }  

    return res;
}


int main(int argc, char **argv) {
    char input_fh[FHMAX];
    fsroots_t *current, *tail, dummy;
    READ3res *res;

    dummy.next = NULL;
    tail = &dummy;

    while (fgets(input_fh, FHMAX, stdin)) {
        parse_fh(input_fh, &(tail->next));
        tail = tail->next;
    }

    /* skip the first empty struct */
    current = dummy.next;
    while (current) {
        do_read(current);
        current = current->next;
    }
}
