#include "nfsping.h"
#include "rpc.h"


void usage() {
    printf("Usage: nfscat [options] [targets...]\n\
    -b    blocksize (in bytes, default 8192)\n\
    -c n  count of read requests to send to target\n\
    -T    use TCP (default UDP)\n");

    exit(3);
}


/* the NFS READ call */
/* read a file from offset with a size of blocksize */
/* returns the RPC result, updates us with the time that the call took */
READ3res *do_read(CLIENT *client, fsroots_t *dir, offset3 offset, const unsigned long blocksize, unsigned long *us) {
    READ3res *readres;
    READ3args args = {
        .file = dir->fsroot,
        .offset = 0,
        .count = blocksize,
    };
    struct rpc_err clnt_err;
    int i;
    unsigned int count = 0;
    struct timeval call_start, call_end;

    args.offset = offset;

    gettimeofday(&call_start, NULL);
    readres = nfsproc3_read_3(&args, client);
    gettimeofday(&call_end, NULL);

    *us = tv2us(call_end) - tv2us(call_start);

    if (readres) {
        if (readres->status != NFS3_OK) {
            clnt_geterr(client, &clnt_err);
            if (clnt_err.re_status)
                clnt_perror(client, "nfsproc3_read_3");
            else
                nfs_perror(readres->status);
        }
    } else {
        clnt_perror(client, "nfsproc3_read_3");
    }

    return readres;
}


int main(int argc, char **argv) {
    int ch;
    char input_fh[FHMAX];
    CLIENT *client = NULL;
    fsroots_t *current, *tail, dummy;
    READ3res *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    struct sockaddr_in clnt_info;
    unsigned long version = 3;
    offset3 offset = 0;
    unsigned long blocksize = 8192;
    unsigned long count = 0;
    struct timeval timeout = NFS_TIMEOUT;
    unsigned long us;
    unsigned long sent = 0, received = 0;
    unsigned long min = ULONG_MAX, max = 0;
    double avg, loss;

    while ((ch = getopt(argc, argv, "b:c:hT")) != -1) {
        switch(ch) {
            /* blocksize */
            case 'b':
                /* TODO this maxes out at 64k for TCP and 8k for UDP */
                blocksize = strtoul(optarg, NULL, 10); 
                break;
            case 'c':
                count = strtoul(optarg, NULL, 10);
                if (count == 0) {
                    fprintf(stderr, "nfscat: zero count, nothing to do!\n");
                    exit(3);
                }
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            default:
                usage();
        }

    }

    dummy.next = NULL;
    tail = &dummy;

    while (fgets(input_fh, FHMAX, stdin)) {
        tail->next = malloc(sizeof(fsroots_t));
        tail = tail->next;
        tail->next = NULL;

        parse_fh(input_fh, tail);

        tail->client_sock->sin_family = AF_INET;
        tail->client_sock->sin_port = htons(NFS_PORT);
    }

    /* skip the first empty struct */
    current = dummy.next;

    /* loop through the list of targets */
    while (current) {
        /* check if we can use the same client connection as the previous target */
        while (client && current) {
            /* get the server address out of the client */
            clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);
            while (current) {
                if (clnt_info.sin_addr.s_addr == current->client_sock->sin_addr.s_addr) {
                    /* start at the beginning of the file */
                    offset = 0;
                    sent = received = 0;
                    do {
                        res = do_read(client, current, offset, blocksize, &us);
                        sent++;
                        if (res && res->status == NFS3_OK) {
                            received++;
                            loss = (sent - received) / (double)sent * 100;
                            /* TODO the final read could be short and take less time, discard? */
                            /* what about files that come back in a single RPC? */
                            if (us < min) min = us;
                            if (us > max) max = us;
                            /* calculate the average time */
                            avg = (avg * (received - 1) + us) / received;

                            if (count) {
                                fprintf(stderr, "%s:%s: [%lu] %lu bytes %03.2f ms (xmt/rcv/%%loss = %lu/%lu/%.0f%%, min/avg/max = %.2f/%.2f/%.2f)\n",
                                    current->host,
									current->path,
									received - 1, res->READ3res_u.resok.count, us
									/ 1000.0,
									sent,
									received,
									loss,
									min / 1000.0,
								   	avg / 1000.0,
									max / 1000.0 );
                            } else {
                                /* write to stdout */
                                fwrite(res->READ3res_u.resok.data.data_val, 1, res->READ3res_u.resok.data.data_len, stdout);
                            }

                            offset += res->READ3res_u.resok.count;
                        }
                        /* check count argument */
                        if (count && sent > count) {
                            break;
                        }
                    /* check for errors or end of file */
                    } while (res && res->status == NFS3_OK && res->READ3res_u.resok.eof == 0);

                    current = current->next;
                } else {
                    client = destroy_rpc_client(client);
                    break;
                }
            }
        }
        if (current) {
            /* connect to server */
            client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, version, timeout);
            client->cl_auth = authunix_create_default();
        }
    }
}
