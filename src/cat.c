#include "nfsping.h"
#include "rpc.h"


/* the NFS READ call */
unsigned int do_read(fsroots_t *dir, struct addrinfo *hints, uint16_t port, unsigned long version, struct timeval timeout) {
    READ3res *res;
    READ3args args;
    CLIENT *client;
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
    client = create_rpc_client(dir->client_sock, hints, port, NFS_PROGRAM, version, timeout);
    client->cl_auth = authunix_create_default();

    args.file = dir->fsroot;
    args.offset = 0;
    args.count = 8192;

    do {
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
    } while (res);

    return count;
}


int main(int argc, char **argv) {
    char input_fh[FHMAX];
    fsroots_t *current, *tail, dummy;
    READ3res *res;
    struct addrinfo hints;
    uint16_t port = htons(NFS_PORT);
    unsigned long version = 3;
    struct timeval timeout = NFS_TIMEOUT;

    dummy.next = NULL;
    tail = &dummy;

    hints.ai_family = AF_INET;
    /* default to UDP */
    hints.ai_socktype = SOCK_DGRAM;

    while (fgets(input_fh, FHMAX, stdin)) {
        tail->next = malloc(sizeof(fsroots_t));
        tail = tail->next;
        tail->next = NULL;

        parse_fh(input_fh, tail);
    }

    /* skip the first empty struct */
    current = dummy.next;
    while (current) {
        do_read(current, &hints, port, version, timeout);
        current = current->next;
    }
}
