#include "nfsping.h"
#include "rpc.h"


/* the NFS READ call */
unsigned int do_read(fsroots_t *dir) {
    READ3res *res;
    READ3args args;
    CLIENT *client;
    const u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    int nfs_sock = RPC_ANYSOCK;
    struct rpc_err clnt_err;
    int i;
    unsigned int count = 0;
    struct timeval call_start, call_end;
    unsigned long us;
    unsigned long sent = 0, received = 0;
    unsigned long min = ULONG_MAX, max = 0;
    float avg;
    double loss;

    dir->client_sock->sin_family = AF_INET;
    dir->client_sock->sin_port = htons(NFS_PORT);

    //client = *clntudp_create(dir->client_sock, NFS_PROGRAM, version, timeout, &nfs_sock);

    args.file = dir->fsroot;
    args.offset = 0;
    args.count = 8192;

    do {
        nfs_sock = RPC_ANYSOCK;
        client = clnttcp_create(dir->client_sock, NFS_PROGRAM, version, &nfs_sock, 0, 0);
        client->cl_auth = authunix_create_default();

        gettimeofday(&call_start, NULL);
        res = nfsproc3_read_3(&args, client);
        gettimeofday(&call_end, NULL);
        sent++;

        us = tv2us(call_end) - tv2us(call_start);
        fprintf(stderr, "%s:%s %03.2f ms\n", dir->host, dir->path, us / 1000.0);

        if (res) {
            if (res->status != NFS3_OK) {
                clnt_geterr(client, &clnt_err);                                                                                                                     
                if (clnt_err.re_status)
                    clnt_perror(client, "nfsproc3_read_3");
                else
                    nfs_perror(res->status);
                break;
            } else {
                received++;
                loss = (sent - received) / (double)sent * 100;
                /* TODO the final read could be short and take less time, discard? */
                /* what about files that come back in a single packet? */
                if (us < min) min = us;
                if (us > max) max = us;
                /* calculate the average time */
                avg = (avg * (received - 1) + us) / received;

                count += res->READ3res_u.resok.count;

                //fprintf(stderr, "%s: %u bytes xmt/rcv/%%loss = %lu/%lu/%.0f%%, min/avg/max = %.2f/%.2f/%.2f\n",
                    //dir->path, count, sent, received, loss, min / 1000.0, avg / 1000.0, max / 1000.0);

                /* write to stdout */
                fwrite(res->READ3res_u.resok.data.data_val, 1, res->READ3res_u.resok.data.data_len, stdout);

                if (res->READ3res_u.resok.eof) {
                    break;
                } else {
                    args.offset += res->READ3res_u.resok.count;
                }
            }
        } else {
            clnt_perror(client, "nfsproc3_read_3");
        }
            auth_destroy(client->cl_auth);
            clnt_destroy(client);
            close(nfs_sock);
    } while (res);

    return count;
}


int main(int argc, char **argv) {
    char input_fh[FHMAX];
    fsroots_t *current, *tail, dummy;
    READ3res *res;

    dummy.next = NULL;
    tail = &dummy;

    while (fgets(input_fh, FHMAX, stdin)) {
        tail->next = malloc(sizeof(fsroots_t));
        tail = tail->next;
        tail->next = NULL;

        parse_fh(input_fh, tail);
    }

    /* skip the first empty struct */
    current = dummy.next;
    while (current) {
        do_read(current);
        current = current->next;
    }
}
