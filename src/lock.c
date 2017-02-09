#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static int do_nlm_test(CLIENT *, char *, pid_t, const char *, nfs_fh_list *);

/* globals */
int verbose = 0;


void usage() {
    printf("Usage: nfslock [options]\n\
    -c n     count of lock requests to send to target\n\
    -h       display this help and exit\n\
    -H n     frequency in Hertz (requests per second, default %i)\n\
    -l       loop forever\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n",
    NFS_HERTZ); 

    exit(3);
}


/* TODO take a nfs_fh3 instead of nfs_fh_list */
int do_nlm_test(CLIENT *client, char *nodename, pid_t mypid, const char *host, nfs_fh_list *current) {
    /* default to failed */
    int status = nlm4_failed;
    char *fh;
    unsigned long us;
    struct timespec wall_clock, call_start, call_end, call_elapsed;
    nlm4_testres *res = NULL;
    /* build the arguments for the test procedure */
    nlm4_testargs testargs = {
        .cookie    = 0, /* the cookie is only used in async RPC calls */
        .exclusive = FALSE,
        .alock = {
            /* TODO should we append "nfslock" to the nodename so it's easy to distinguish from the kernel's own locks? */
            .caller_name = nodename,
            .svid = mypid,
            .l_offset = 0,
            .l_len = 0,
            .fh = {
                .n_len = current->nfs_fh.data.data_len,
                .n_bytes = current->nfs_fh.data.data_val,
            },
        },
    };
    /* "The oh field is an opaque object that identifies the host, or a process on the host, that is making the request" */
    /* don't need to count the terminating null */
    testargs.alock.oh.n_len = asprintf(&testargs.alock.oh.n_bytes, "%i@%s", mypid, nodename);

    /* string labels corresponding to return values in nlm4_stats enum */
    const char *nlm4_stats_labels[] = {
        "granted",
        "denied",
        "denied_nolocks",
        "blocked",
        "denied_grace_period",
        "deadlock",
        "read_only_filesystem",
        "stale_filehandle",
        "file_too_big",
        "failed"
    };


    if (client) {
        /* first grab the wall clock time for output */
        clock_gettime(CLOCK_REALTIME, &wall_clock);

        /* then use the more accurate timer for the elapsed RPC time */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif

        /* run the test procedure */
        res = nlm4_test_4(&testargs, client);

#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif
    }

    timespecsub(&call_end, &call_start, &call_elapsed);
    us = ts2us(call_elapsed);

    if (res) {
        status = res->stat.stat;
        fprintf(stderr, "%s\n", nlm4_stats_labels[status]);

        fh = nfs_fh3_to_string(current->nfs_fh);

        /* human output for now */
        /* use filehandle until we get the mount point from nfsmount, path can be ambiguous (or not present) */
        printf("%s:%s %lu %li\n", host, fh, us, wall_clock.tv_sec);
        free(fh);
    } else {
        /* TODO still print a graphite result */
        clnt_perror(client, "nlm4_test_4");
        /* use something that doesn't overlap with values in nlm4_testres.stat */
        status = 10;
    }

    return status;
}


int main(int argc, char **argv) {
    int ch;
    char *input_fh;
    size_t n = 0; /* for getline() */
    targets_t dummy = { 0 };
    targets_t *targets = &dummy;
    targets_t *current = targets;
    nfs_fh_list *filehandle;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    struct sockaddr_in clnt_info;
    int version = 4;
    unsigned long count = 1;
    int loop = 0;
    unsigned int sent = 0;
    struct timespec sleep_time;
    unsigned long hertz = NFS_HERTZ;
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };
    int status = 0;
    /* get the pid of the current process to use in the lock request(s) */
    pid_t mypid = getpid();
    int getaddr;
    char nodename[NI_MAXHOST];

    while ((ch = getopt(argc, argv, "c:hH:lTv")) != -1) {
        switch(ch) {
            /* number of locks per target */
            case 'c':
                if (loop) {
                    fatal("Can't specify count and loop!\n");
                }
                count = strtoul(optarg, NULL, 10);
                if (count == 0 || count == ULONG_MAX) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            /* polling frequency */
            case 'H':
                /* TODO check for reasonable values */
                hertz = strtoul(optarg, NULL, 10);
                break;
            /* loop forever */
            case 'l':
                if (count) {
                    fatal("Can't specify loop and count!\n");
                }
                loop = 1;
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
        current = parse_fh(targets, input_fh, 0, timeout, 0);
    }

    targets = targets->next;

    while ((sent < count) || loop) {
        /* reset to start of list */
        current = targets;

        while (current) {
            if (current->client == NULL) {
                /* connect to server */
                current->client = create_rpc_client(current->client_sock, &hints, NLM_PROG, version, timeout, src_ip);

                if (current->client) {
                    auth_destroy(current->client->cl_auth);
                    current->client->cl_auth = authunix_create_default();

                    /* look up the address that was used to connect to the server */
                    /* TODO just use current->client_sock? */
                    clnt_control(current->client, CLGET_SERVER_ADDR, (char *)&clnt_info);

                    /* do a reverse lookup to find our client name */
                    /* we need this to populate the nlm test arguments */
                    /* TODO store this in the target_t struct? */
                    getaddr = getnameinfo((struct sockaddr *)&clnt_info, sizeof(struct sockaddr_in), nodename, NI_MAXHOST, NULL, 0, 0);
                    if (getaddr > 0) { /* failure! */
                        /* use something that doesn't overlap with values in nlm4_testres.stat */
                        fatalx(10, "%s: %s\n", current->name, gai_strerror(getaddr));
                    }
                }
            }

            filehandle = current->filehandles;

            while (filehandle) {
                /* the RPC */
                if (current->client) {
                    status = do_nlm_test(current->client, nodename, mypid, current->name, filehandle);
                }

                filehandle = filehandle->next;
            }

            current = current->next;
        } /* while (current) */

        /* at this point we've sent one request to each target/filehandle */
        sent++;

        /* sleep between requests */
        /* TODO don't sleep on last round */
        nanosleep(&sleep_time, NULL);
    }

    /* this is zero if everything worked, or the last error code seen */
    return status;
}
