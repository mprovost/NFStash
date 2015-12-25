#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static READ3res *do_read(CLIENT *, nfs_fh_list *, offset3, const unsigned long, unsigned long *);

/* globals */
int verbose = 0;

void usage() {
    printf("Usage: nfscat [options] [targets...]\n\
    -b n     blocksize (in bytes, default 8192)\n\
    -c n     count of read requests to send to target\n\
    -h       display this help and exit\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n");

    exit(3);
}


/* the NFS READ call */
/* read a file from offset with a size of blocksize */
/* returns the RPC result, updates us with the time that the call took */
READ3res *do_read(CLIENT *client, nfs_fh_list *dir, offset3 offset, const unsigned long blocksize, unsigned long *us) {
    READ3res *res;
    READ3args args = {
        .file = dir->nfs_fh,
        .offset = 0,
        .count = blocksize,
    };
    struct rpc_err clnt_err;
    struct timeval call_start, call_end;

    args.offset = offset;

    gettimeofday(&call_start, NULL);
    res = nfsproc3_read_3(&args, client);
    gettimeofday(&call_end, NULL);

    *us = tv2us(call_end) - tv2us(call_start);

    if (res) {
        if (res->status != NFS3_OK) {
            clnt_geterr(client, &clnt_err);
            if (clnt_err.re_status)
                clnt_perror(client, "nfsproc3_read_3");
            else
                nfs_perror(res->status);
        }
    } else {
        clnt_perror(client, "nfsproc3_read_3");
    }

    return res;
}


int main(int argc, char **argv) {
    int ch;
    char *input_fh;
    size_t n = 0; /* for getline() */
    CLIENT *client = NULL;
    nfs_fh_list *current;
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
    double avg = 0, loss;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    while ((ch = getopt(argc, argv, "b:c:hS:Tv")) != -1) {
        switch(ch) {
            /* blocksize */
            case 'b':
                /* TODO this maxes out at 64k for TCP and 8k for UDP */
                blocksize = strtoul(optarg, NULL, 10); 
                break;
            case 'c':
                count = strtoul(optarg, NULL, 10);
                if (count == 0) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            /* verbose */
            case 'v':
                verbose = 1;
                break;
            case 'h':
            default:
                usage();
        }

    }

    /* no arguments, use stdin */
    if (optind == argc) {
        if (getline(&input_fh, &n, stdin) == -1) {
            input_fh = NULL;
        }
    /* first argument */
    } else {
        input_fh = argv[optind];
    }

    while (input_fh) {

        current = parse_fh(input_fh);

        /* check if we can use the same client connection as the previous target */
        if (client) {
            /* get the server address out of the client */
            clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);
            /* ok to reuse client connection if it's the same target address */
            if (clnt_info.sin_addr.s_addr != current->client_sock->sin_addr.s_addr) {
                /* different client address, close the connection */
                client = destroy_rpc_client(client);
            }
        }

        /* no client connection */
        if (client == NULL) {
            current->client_sock->sin_family = AF_INET;
            current->client_sock->sin_port = htons(NFS_PORT);
            /* connect to server */
            client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, version, timeout, src_ip);
            /* don't use default AUTH_NONE */
            auth_destroy(client->cl_auth);
            /* set up AUTH_SYS */
            client->cl_auth = authunix_create_default();
        }

        if (client) {
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
                if (count && sent >= count) {
                    break;
                }
            /* check for errors or end of file */
            } while (res && res->status == NFS3_OK && res->READ3res_u.resok.eof == 0);
        }

        /* cleanup */
        free(current->client_sock);
        free(current);

        /* get the next filehandle */
        if (optind == argc) {
            if (getline(&input_fh, &n, stdin) == -1) {
                input_fh = NULL;
            }
        } else {
            optind++;
            if (optind < argc) {
                input_fh = argv[optind];
            } else {
                input_fh = NULL;
            }
        }

    } /* while(input_fh) */

    return(0);
}
