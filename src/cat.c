#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static READ3res *do_read(CLIENT *, nfs_fh_list *, offset3, const unsigned long, unsigned long *);
static void print_output(enum outputs format, char *prefix, char* host, char* path, count3 count, unsigned long min, unsigned long max, double avg, unsigned long sent, unsigned long received,  const struct timespec now, unsigned long us);
 

/* globals */
int verbose = 0;

void usage() {
    struct timespec sleep_time = NFS_SLEEP;
    printf("Usage: nfscat [options] [targets...]\n\
    -b n      blocksize (in bytes, default 8192)\n\
    -c n      count of read requests to send to target\n\
    -p n      polling interval, check target every n ms (default %lu)\n\
    -h        display this help and exit\n\
    -S addr   set source address\n\
    -T        use TCP (default UDP)\n\
    -g string prefix for Graphite/StatsD metric names (default \"nfsping\")\n\
    -G        Graphite format output (default human readable)\n\
    -E        StatsD format output (default human readable)\n\
    -v        verbose output\n",
        ts2ms(sleep_time));

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


/* Prints to stderr because file contents are printed via stdout */
void print_output(enum outputs format, char *prefix, char* host, char* path, count3 count, unsigned long min, unsigned long max, double avg, unsigned long sent, unsigned long received,  const struct timespec now, unsigned long us) {
    double loss;
 
   if (format == human) {
        loss = (sent - received) / (double)sent * 100;

        fprintf(stderr, "%s:%s: [%lu] %lu bytes %03.2f ms (xmt/rcv/%%loss = %lu/%lu/%.0f%%, min/avg/max = %.2f/%.2f/%.2f)\n",
                            host,
                            path,
                            received - 1, 
                            count, 
                            us / 1000.0,
                            sent,
                            received,
                            loss,
                            min / 1000.0,
                            avg / 1000.0,
                            max / 1000.0 );

    }
    if (format == graphite) {
       fprintf(stderr, "%s.%s.%s.usec %lu %li\n", prefix, host, path, us, now.tv_sec); 
    }
    if (format == statsd) {
       fprintf(stderr, "%s.%s.%s.msec:%03.2f|ms\n", prefix, host, path, us / 1000.0 );
    }
    fflush(stderr);
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
    struct timespec wall_clock, loop_start, loop_end, loop_elapsed, sleepy;
    struct timespec sleep_time = NFS_SLEEP;
    struct timeval timeout = NFS_TIMEOUT;
    unsigned long us;
    enum outputs format = human;
    char *prefix = "nfscat";
    unsigned long sent = 0, received = 0;
    unsigned long min = ULONG_MAX, max = 0;
    double avg = 0;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    while ((ch = getopt(argc, argv, "b:c:hS:TvEGg:p:")) != -1) {
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
            /* time between pings to target */
            case 'p':
                /* TODO check for reasonable values */
                ms2ts(&sleep_time, strtoul(optarg, NULL, 10));
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            /* Graphite output  */
            case 'G':
                format = graphite;
                break;
            case 'E':
                format = statsd;
                break;
            /* prefix to use for graphite metrics */
            case 'g':
                /*TODO: Find the real limit of graphite prefix. NAME_MAX 
                        comparison not good enough.
                */
                if (strlen(optarg) < NAME_MAX) {
                    prefix = optarg;
                } else {
                    fatal("The prefix is longer than NAME_MAX\n");
                }
                //strncpy(prefix, optarg, sizeof(prefix));
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
                /* grab the starting time of each loop */
#ifdef CLOCK_MONOTONIC_RAW
               clock_gettime(CLOCK_MONOTONIC_RAW, &loop_start);
#else
               clock_gettime(CLOCK_MONOTONIC, &loop_start);
#endif
                /* grab the wall clock time for output */
                /* use the start time of the request */
                /* the call_start timer is more important so do this first so we're not measuring the time this call takes */
                clock_gettime(CLOCK_REALTIME, &wall_clock);

                res = do_read(client, current, offset, blocksize, &us);
                sent++;
                if (res && res->status == NFS3_OK) {
                    received++;
                    /* TODO the final read could be short and take less time, discard? */
                    /* what about files that come back in a single RPC? */
                    if (us < min) min = us;
                    if (us > max) max = us;
                    /* calculate the average time */
                    avg = (avg * (received - 1) + us) / received;

                    if (count) {

                        print_output(format, prefix, current->host, current->path, res->READ3res_u.resok.count, min, max, avg, sent, received, wall_clock, us);

                    } else {
                        /* write to stdout */
                        fwrite(res->READ3res_u.resok.data.data_val, 1, res->READ3res_u.resok.data.data_len, stdout);
                    }

                    offset += res->READ3res_u.resok.count;
                }
                /* check count argument */
                if (count && sent >= count) {
                    break;
                } else {
                    /* sleep between rounds */
                    /* measure how long the current round took, and subtract that from the sleep time */
                    /* this tries to ensure that each polling round takes the same time */
#ifdef CLOCK_MONOTONIC_RAW
                    clock_gettime(CLOCK_MONOTONIC_RAW, &loop_end);
#else
                    clock_gettime(CLOCK_MONOTONIC, &loop_end);
#endif
                    timespecsub(&loop_end, &loop_start, &loop_elapsed);
                    debug("Polling took %lld.%.9lds\n", (long long)loop_elapsed.tv_sec, loop_elapsed.tv_nsec);
                    /* don't sleep if we went over the sleep_time */
                    if (timespeccmp(&loop_elapsed, &sleep_time, >)) {
                       debug("Slow poll, not sleeping\n");
                    } else {
                       timespecsub(&sleep_time, &loop_elapsed, &sleepy);
                       debug("Sleeping for %lld.%.9lds\n", (long long)sleepy.tv_sec, sleepy.tv_nsec);
                       nanosleep(&sleepy, NULL);
                    }
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
