#include "nfsping.h"
#include "rpc.h"
#include "util.h"

int verbose = 0;

void usage() {
    printf("Usage: nfslock [options] [filehandle...]\n\
    -h       display this help and exit\n\
    -l       loop forever\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n"); 

    exit(3);
}


int do_nlm_test(CLIENT *client, char *nodename, pid_t mypid, fsroots_t *current) {
    int status;
    unsigned long us;
    struct timeval call_start, call_end;
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
    /* TODO should we append nfslock to the nodename so it's easy to distinguish from the kernel's own locks? */
    testargs.alock.caller_name = nodename;
    testargs.alock.svid = mypid;
    /* copy the filehandle */
    memcpy(&testargs.alock.fh, &current->fsroot, sizeof(nfs_fh3));
    /* don't need to count the terminating null */
    testargs.alock.oh.n_len = asprintf(&testargs.alock.oh.n_bytes, "%i@%s", mypid, nodename);
    testargs.alock.l_offset = 0;
    testargs.alock.l_len = 0;

    if (client) {
        gettimeofday(&call_start, NULL);

        /* run the test procedure */
        res = nlm4_test_4(&testargs, client);

        gettimeofday(&call_end, NULL);
    }

    if (res) {
        fprintf(stderr, "%s\n", nlm4_stats_labels[res->stat.stat]);
        /* if we got an error, update the status for return */
        if (res->stat.stat) {
            status = res->stat.stat;
        }

        us = tv2us(call_end) - tv2us(call_start);

        /* graphite output for now */
        printf("nfslock.%s.test.usec %lu %li\n", current->host, us, call_end.tv_sec);
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
    fsroots_t *filehandles;
    fsroots_t *current;
    fsroots_t fh_dummy;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    int version = 4;
    int loop = 0;
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

    while ((ch = getopt(argc, argv, "hlTv")) != -1) {
        switch(ch) {
            /* loop forever */
            case 'l':
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
        /* make it the max size not the length of the current string because we'll reuse it for all filehandles */
        input_fh = malloc(sizeof(char) * FHMAX);
        fgets(input_fh, FHMAX, stdin);
    /* first argument */
    } else {
        input_fh = argv[optind];
    }

    /* pointer to head of list */
    current = &fh_dummy;
    filehandles = current;

    /* get the pid of the current process to use in the lock request(s) */
    mypid = getpid();

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
                client->cl_auth = authunix_create_default();
                //client->cl_auth = authunix_create(char *host, int uid, int gid, int len, int *aup_gids);

                /* look up the address that was used to connect to the server */
                clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);

                /* do a reverse lookup to find our client name */
                getaddr = getnameinfo((struct sockaddr *)&clnt_info, sizeof(struct sockaddr_in), nodename, NI_MAXHOST, NULL, 0, 0);
                if (getaddr > 0) { /* failure! */
                    fprintf(stderr, "%s: %s\n", current->host, gai_strerror(getaddr));
                    /* use something that doesn't overlap with values in nlm4_testres.stat */
                    exit(10);
                }
            }

            /* the RPC */
            status = do_nlm_test(client, nodename, mypid, current);
        }

        /* get the next filehandle*/
        if (optind == argc) {
            input_fh = fgets(input_fh, FHMAX, stdin);
        } else {
            optind++;
            if (optind < argc) {
                input_fh = argv[optind];
            } else {
                input_fh = NULL;
            }
        }

    }

    /* skip the first dummy entry */
    filehandles = filehandles->next;

    while (loop) {
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
                client->cl_auth = authunix_create_default();
                //client->cl_auth = authunix_create(char *host, int uid, int gid, int len, int *aup_gids);

                /* look up the address that was used to connect to the server */
                clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);

                /* do a reverse lookup to find our client name */
                getaddr = getnameinfo((struct sockaddr *)&clnt_info, sizeof(struct sockaddr_in), nodename, NI_MAXHOST, NULL, 0, 0);
                if (getaddr > 0) { /* failure! */
                    fprintf(stderr, "%s: %s\n", current->host, gai_strerror(getaddr));
                    /* use something that doesn't overlap with values in nlm4_testres.stat */
                    exit(10);
                }
            }

            /* the RPC */
            status = do_nlm_test(client, nodename, mypid, current);

            current = current->next;
        }
    }

    /* this is zero if everything worked, or the last error code seen */
    return status;
}
