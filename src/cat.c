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
    printf("Usage: nfscat [options]\n\
    -b n      blocksize (in bytes, default 8192)\n\
    -c n      count of read requests to send to target\n\
    -E        StatsD format output (default human readable)\n\
    -g string prefix for Graphite/StatsD metric names (default \"nfsping\")\n\
    -G        Graphite format output (default human readable)\n\
    -h        display this help and exit\n\
    -H n      frequency in Hertz (requests per second, default %i)\n\
    -S addr   set source address\n\
    -T        use TCP (default UDP)\n\
    -v        verbose output\n",
    NFS_HERTZ);

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
    const char *proc = "nfsproc3_read_3";
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
            clnt_err.re_status ? clnt_perror(client, proc) : nfs_perror(res->status, proc);
        }
    } else {
        clnt_perror(client, proc);
    }

    return res;
}


/* Prints to stderr because file contents are printed via stdout */
void print_output(enum outputs format, char *prefix, char* host, char* path, count3 count, unsigned long min, unsigned long max, double avg, unsigned long sent, unsigned long received,  const struct timespec now, unsigned long us) {
    double loss;
 
   if (format == ping) {
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
    targets_t dummy = { 0 };
    targets_t *targets = &dummy;
    targets_t *current = targets;
    nfs_fh_list *filehandle;
    READ3res *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    unsigned long version = 3;
    offset3 offset = 0;
    unsigned long blocksize = 8192;
    unsigned long count = 0;
    struct timespec wall_clock, loop_start, loop_end, loop_elapsed, sleepy;
    struct timespec sleep_time;
    unsigned long hertz = NFS_HERTZ;
    struct timeval timeout = NFS_TIMEOUT;
    unsigned long us;
    enum outputs format = ping;
    char *prefix = "nfscat";
    unsigned long sent = 0, received = 0;
    unsigned long min = ULONG_MAX, max = 0;
    double avg = 0;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    while ((ch = getopt(argc, argv, "b:c:Eg:GhH:S:Tv")) != -1) {
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
            /* [E]tsy's StatsD output */
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
            /* Graphite output  */
            case 'G':
                format = graphite;
                break;
            /* polling frequency */
            case 'H':
                /* TODO check for reasonable values */
                hertz = strtoul(optarg, NULL, 10);
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

    /* calculate the sleep_time based on the frequency */
    /* check for a frequency of 1, that's a simple case */
    /* this doesn't support frequencies lower than 1Hz */
    if (hertz == 1) {
        sleep_time.tv_sec = 1;
        sleep_time.tv_nsec = 0;
    } else {
        sleep_time.tv_sec = 0;
        /* nanoseconds */
        sleep_time.tv_nsec = 1000000000 / hertz;
    }

    /* no arguments, use stdin */
    while (getline(&input_fh, &n, stdin) != -1) {
        /* don't allocate space for results */
        current = parse_fh(targets, input_fh, 0, 0);
    }

    targets = targets->next;
    current = targets;

    while (current) {
        /* no client connection */
        if (current->client == NULL) {
            /* connect to server */
            current->client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, version, timeout, src_ip);
            /* don't use default AUTH_NONE */
            auth_destroy(current->client->cl_auth);
            /* set up AUTH_SYS */
            current->client->cl_auth = authunix_create_default();
        }

        if (current->client) {
            /* start at the beginning of the file */
            offset = 0;
            sent = received = 0;

            filehandle = current->filehandles;

            while (filehandle) {
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

                    res = do_read(current->client, filehandle, offset, blocksize, &us);
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

                            print_output(format, prefix, current->name, filehandle->path, res->READ3res_u.resok.count, min, max, avg, sent, received, wall_clock, us);

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

                filehandle = filehandle->next;
            } /* while (filehandle) */
        }

        current = current->next;
    } /* while(current) */

    return(0);
}
