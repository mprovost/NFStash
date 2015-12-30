/*
 * Clear NFS locks from server
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static void *do_notify(CLIENT *, char *, int);
static void *do_free_all(CLIENT *, char *, int);

/* globals */
int verbose = 0;


void usage() {
    printf("Usage: clear_locks [options] client\n\
    Clear NFS locks for client held on server\n\
    -h         display this help and exit\n\
    -S addr    set source address\n\
    -s server  NFS server address (default localhost)\n\
    -T         use TCP (default UDP)\n\
    -v         verbose output\n");

    exit(3);
}


/* the SM_NOTIFY call */
/* returns a void pointer, NULL is an error, anything else is success */
void *do_notify(CLIENT *client, char *client_name, int state) {
    void *status;
    struct stat_chge notify_stat = {
        .mon_name = client_name,
        .state = state
    };

    /* SM_NOTIFY has no return value */
    /* returns NULL on RPC failure */
    status = sm_notify_1(&notify_stat, client);

    if (status) {
        debug("sm_notify_1 succeeded\n");
    } else {
        debug("sm_notify_1 failed\n");
    }

    return status;
}


/* the NLM_FREE_ALL call */
/* returns a void pointer, NULL is an error, anything else is success */
void *do_free_all(CLIENT *client, char *client_name, int state){
    void *status;
    struct nlm4_notify notify_stat = {
        .name = client_name,
        /* FIXME state is unused, 0? */
        .state = state
    };

    status = nlm4_free_all_4(&notify_stat, client);

    if (status) {
        debug("nlm4_free_all_4 succeeded\n");
    } else {
        debug("nlm4_free_all_4 failed\n");
    }

    return status;
}


int main(int argc, char **argv) {
    int ch;
    char *client_name;
    char *server_name = NULL;
    struct timespec wall_clock;
    void *status = NULL;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *addr;
    int getaddr;
    char ip_address[INET_ADDRSTRLEN];
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info = {
        .sin_family = AF_INET,
        .sin_port = 0
    };
    unsigned long sm_version = 1; /* for SM protocol, only one version available */
    /* just do version 4 for now for NFS v3 */
    unsigned long nlm_version = 4; /* for NLM protocol */
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    /* check for no arguments */
    /* clearing locks for our local hostname on the local server doesn't make sense */
    /* this won't catch everything so check again after processing options */
    if (argc == 1) {
        fatal("Either client or server required!\n");
    }

    while ((ch = getopt(argc, argv, "hS:s:Tv")) != -1) {
        switch(ch) {
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            case 's':
                if (strlen(optarg) < SM_MAXSTRLEN) {
                    server_name = optarg;
                } else {
                    fatal("Invalid hostname!\n");
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

    /* check if there is no client argument */
    if (optind == argc) {
        /* check if we have a server specified */
        /* clearing locks for our local hostname on the local server doesn't make sense */
        /* we check above for no arguments, now specifically check that we have either a client or server set */
        if (server_name) {
            /* default to using local hostname */
            client_name = calloc(SM_MAXSTRLEN + 1, sizeof(char));
            /* leave room for NULL */
            /* TODO check for name too long */
            if (gethostname(client_name, SM_MAXSTRLEN) == -1) {
                fatalx(2, "gethostname: %s\n", strerror(errno));
            }
        } else {
            fatal("Either client or server required!\n");
        }
    } else {
        /* first argument */
        client_name = argv[optind];
    }

    /* default to localhost if server not specified */
    if (server_name == NULL) {
        /* TODO or "localhost"? */
        server_name = "127.0.0.1";
    }

    /* first try the server name as an IP address */
    if (inet_pton(AF_INET, server_name, &clnt_info.sin_addr)) {
        debug("Clearing locks for %s on %s\n", client_name, server_name);
    } else {
        /* otherwise do a DNS lookup */
        getaddr = getaddrinfo(server_name, "nfs", &hints, &addr);

        if (getaddr == 0) { /* success! */
            /* only use the first DNS response */
            /* presumably if there are multiple servers they are coordinating locks */
            /* TODO a -m option for multiple addresses anyway? */
            clnt_info.sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            if (verbose) {
                inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, ip_address, INET_ADDRSTRLEN); 
            }
            debug("Clearing locks for %s on %s (%s)\n", client_name, server_name, ip_address);
        /* failure! */
        } else {
            fatalx(2, "%s: %s\n", server_name, gai_strerror(getaddr));
        }
    }

    /* use current unix timestamp as state so that it always increments between calls (unless they're the same second) */
    /* TODO check that clock_gettime returns a signed 32 bit int for seconds (ie time_t == longword) */
    clock_gettime(CLOCK_REALTIME, &wall_clock);

    debug("status = %li\n", wall_clock.tv_sec);

    /* connect to server */
    client = create_rpc_client(&clnt_info, &hints, SM_PROG, sm_version, timeout, src_ip);

    if (client) {
        /* first try to notify the network status daemon that the client has rebooted */
        status = do_notify(client, client_name, wall_clock.tv_sec);

        /* if that doesn't work, free the locks directly on the network lock manager */
        if (status == NULL) {
            /* the client is set up to talk to the NSM so reconnect to the NLM */
            destroy_rpc_client(client);
            client = create_rpc_client(&clnt_info, &hints, NLM_PROG, nlm_version, timeout, src_ip);

            /* the NLM call */
            status = do_free_all(client, client_name, wall_clock.tv_sec);
        }
    /* can't connect to SM, try NLM */
    } else {
        client = create_rpc_client(&clnt_info, &hints, NLM_PROG, nlm_version, timeout, src_ip);

        if (client) {
            /* the NLM call */
            status = do_free_all(client, client_name, wall_clock.tv_sec);
        }
    }

    if (status) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
