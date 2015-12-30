#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static int do_nlm_test(CLIENT *, char *, pid_t, nfs_fh_list *);

/* globals */
int verbose = 0;


void usage() {
    printf("Usage: nfslock [options] [filehandle...]\n\
    -c n     count of lock requests to send to target\n\
    -h       display this help and exit\n\
    -l       loop forever\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n"); 

    exit(3);
}


int do_nlm_test(CLIENT *client, char *nodename, pid_t mypid, nfs_fh_list *current) {
    /* default to failed */
    int status = nlm4_failed;
    char fh[128];
    unsigned long us;
    struct timespec wall_clock, call_start, call_end, call_elapsed;
    nlm4_testres *res = NULL;
    nlm4_testargs testargs = {
        .cookie    = 0, /* the cookie is only used in async RPC calls */
        .exclusive = FALSE,
    };
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

    /* build the arguments for the test procedure */
    /* TODO build these once at the start when we make the target */
    /* TODO should we append nfslock to the nodename so it's easy to distinguish from the kernel's own locks? */
    testargs.alock.caller_name = nodename;
    testargs.alock.svid = mypid;
    /* copy the filehandle */
    memcpy(&testargs.alock.fh, &current->nfs_fh, sizeof(nfs_fh3));
    /* don't need to count the terminating null */
    testargs.alock.oh.n_len = asprintf(&testargs.alock.oh.n_bytes, "%i@%s", mypid, nodename);
    testargs.alock.l_offset = 0;
    testargs.alock.l_len = 0;

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

    if (res) {
        fprintf(stderr, "%s\n", nlm4_stats_labels[res->stat.stat]);
        nfs_fh3_to_string(fh, current->nfs_fh);
        /* if we got an error, update the status for return */
        if (res->stat.stat) {
            status = res->stat.stat;
        }

        timespecsub(&call_end, &call_start, &call_elapsed);
        us = ts2us(call_elapsed);

        /* human output for now */
        /* use filehandle until we get the mount point from nfsmount, path can be ambiguous (or not present) */
        printf("%s:%s %lu %li\n", current->host, fh, us, wall_clock.tv_sec);
    } else {
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
    nfs_fh_list *filehandles, *current, fh_dummy;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    int version = 4;
    unsigned long count = 0;
    int loop = 0;
    unsigned int sent = 0;
    struct timespec sleep_time = NFS_SLEEP;
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };
    int status = 0;
    pid_t mypid;
    int getaddr;
    char nodename[NI_MAXHOST];

    while ((ch = getopt(argc, argv, "c:hlTv")) != -1) {
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

    /* no arguments, use stdin */
    if (optind == argc) {
        if (getline(&input_fh, &n, stdin) == -1) {
            input_fh = NULL;
        }
    /* first argument */
    } else {
        input_fh = argv[optind];
    }

    /* pointer to head of list */
    current = &fh_dummy;
    filehandles = current;

    /* get the pid of the current process to use in the lock request(s) */
    mypid = getpid();

    /* first loop through all of the inputs, either arguments or stdin */
    /* ping each one as we see it, don't wait for the full list to come in on stdin */
    while (input_fh) {

        current->next = parse_fh(input_fh);
        current = current->next;

        if (current) {
            /* check if we can use the same client connection as the previous target */
            /* get the server address out of the client */
            if (client) {
                if (clnt_info.sin_addr.s_addr != current->client_sock->sin_addr.s_addr) {
                    client = destroy_rpc_client(client);
                }
            }

            if (client == NULL) {
                current->client_sock->sin_family = AF_INET;
                current->client_sock->sin_port = 0;
                /* connect to server */
                client = create_rpc_client(current->client_sock, &hints, NLM_PROG, version, timeout, src_ip);
                if (client) {
                    auth_destroy(client->cl_auth);
                    client->cl_auth = authunix_create_default();

                    /* look up the address that was used to connect to the server */
                    clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);

                    /* do a reverse lookup to find our client name */
                    /* we need this to populate the nlm test arguments */
                    getaddr = getnameinfo((struct sockaddr *)&clnt_info, sizeof(struct sockaddr_in), nodename, NI_MAXHOST, NULL, 0, 0);
                    if (getaddr > 0) { /* failure! */
                        fprintf(stderr, "%s: %s\n", current->host, gai_strerror(getaddr));
                        /* use something that doesn't overlap with values in nlm4_testres.stat */
                        exit(10);
                    }
                }
            }

            /* the RPC */
            if (client) {
                status = do_nlm_test(client, nodename, mypid, current);
            }

        } /* TODO else (bad filehandle) */

        /* get the next filehandle*/
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
    }

    /* at this point we've sent one request to each target */
    sent++;

    /* skip the first dummy entry */
    filehandles = filehandles->next;

    /* now check if we're looping through the filehandles */
    while ((sent < count) || loop) {
        /* sleep between requests */
        nanosleep(&sleep_time, NULL);

        /* reset to start of list */
        current = filehandles;

        while (current) {
            /* check if we can use the same client connection as the previous target */
            /* get the server address out of the client */
            if (client) {
                if (clnt_info.sin_addr.s_addr != current->client_sock->sin_addr.s_addr) {
                    client = destroy_rpc_client(client);
                }
            }

            if (client == NULL) {
                current->client_sock->sin_family = AF_INET;
                current->client_sock->sin_port = 0;
                /* connect to server */
                client = create_rpc_client(current->client_sock, &hints, NLM_PROG, version, timeout, src_ip);
                if (client) {
                    auth_destroy(client->cl_auth);
                    client->cl_auth = authunix_create_default();

                    /* look up the address that was used to connect to the server */
                    clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);

                    /* do a reverse lookup to find our client name */
                    /* we need this to populate the nlm test arguments */
                    getaddr = getnameinfo((struct sockaddr *)&clnt_info, sizeof(struct sockaddr_in), nodename, NI_MAXHOST, NULL, 0, 0);
                    if (getaddr > 0) { /* failure! */
                        fprintf(stderr, "%s: %s\n", current->host, gai_strerror(getaddr));
                        /* use something that doesn't overlap with values in nlm4_testres.stat */
                        exit(10);
                    }
                }
            }

            /* the RPC */
            if (client) {
                status = do_nlm_test(client, nodename, mypid, current);
            }

            current = current->next;
        }

        /* just increment this once for all targets */
        sent++;
    }

    /* this is zero if everything worked, or the last error code seen */
    return status;
}
