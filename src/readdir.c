#include "nfsping.h"


READDIRPLUS3res *do_readdirplus(fsroots_t *dir) {
    READDIRPLUS3res *res;
    READDIRPLUS3args args;
    CLIENT client;
    entryplus3 *entry;
    const u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    int nfs_sock = RPC_ANYSOCK;
    struct rpc_err clnt_err;
    int i;

    dir->client_sock->sin_family = AF_INET;
    dir->client_sock->sin_port = htons(NFS_PORT);
                                           
    client = *clntudp_create(dir->client_sock, NFS_PROGRAM, version, timeout, &nfs_sock);
    client.cl_auth = authunix_create_default();

    args.dir = dir->fsroot;
    args.cookie = 0;
    //args.cookieverf = "";
    args.dircount = 512;
    args.maxcount = 8192;

    res = nfsproc3_readdirplus_3(&args, &client);

    if (res) {
        while (res) {
            if (res->status != NFS3_OK) {
                clnt_geterr(&client, &clnt_err);                                                                                                                     
                if (clnt_err.re_status)
                    clnt_perror(&client, "nfsproc3_readdirplus_3");
                else
                    nfs_perror(res->status);
            } else {
                entry = res->READDIRPLUS3res_u.resok.reply.entries;
                while (entry) {
                    printf("%s:%s", dir->host, entry->name);
                    /* if it's a directory print a trailing slash */
                    if (entry->name_attributes.post_op_attr_u.attributes.type == NF3DIR)
                        printf("/");
                    printf(":");

                    /* print the filehandle in hex */
                    for (i = 0; i < entry->name_handle.post_op_fh3_u.handle.data.data_len; i++) {
                        printf("%02hhx", entry->name_handle.post_op_fh3_u.handle.data.data_val[i]);
                    }
                    printf("\n");

                    args.cookie = entry->cookie;
                    entry = entry->nextentry;
                }
                if (res->READDIRPLUS3res_u.resok.reply.eof) {
                    break;
                } else {
                    memcpy(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE);
                    res = nfsproc3_readdirplus_3(&args, &client);
                }
            }
        }
    } else {
        clnt_perror(&client, "nfsproc3_readdirplus_3");
    }  

    return res;
}


int main(int argc, char **argv) {
    char input_fh[FHMAX];
    fsroots_t *current, *tail, dummy;
    READDIRPLUS3res *res;

    dummy.next = NULL;
    tail = &dummy;

    while (fgets(input_fh, FHMAX, stdin)) {
        parse_fh(input_fh, &(tail->next));
        tail = tail->next;
    }

    /* skip the first empty struct */
    current = dummy.next;
    while (current) {
        do_readdirplus(current);
        current = current->next;
    }
}
