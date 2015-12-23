/*
 * Clear NFS locks from server
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

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
void *do_notify(CLIENT *client, char *name, int state) {
    void *status;
    struct stat_chge notify_stat = {
        .mon_name = name,
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
void *do_free_all(CLIENT *client, char *name, int state){
    void *status;
    struct nlm4_notify notify_stat = {
        .name = name,
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
    char *server = "";
    struct timespec wall_clock;
    void *status;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info = {
        .sin_family = AF_INET,
        .sin_port = 0
    };
    unsigned long version = 1; /* for SM protocol */
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    /* check for no arguments */
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
                if (strlen(optarg) < NI_MAXHOST) {
                    server = optarg;
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
        /* check if we also don't have a server specified */
        /* clearing locks for our local hostname on the local server doesn't make sense */
        if (strlen(server) == 0) {
            fatal("Either client or server required!\n");
        }
        /* default to using local hostname */
        client_name = malloc(sizeof(char) * NI_MAXHOST);
        /* leave room for NULL */
        if (gethostname(client_name, NI_MAXHOST - 1) == -1) {
            fatalx(2, "gethostname: %s\n", strerror(errno));
        }
    } else {
        /* first argument */
        client_name = argv[optind];
    }

    /* default to localhost if server not specified */
    if (strlen(server) == 0) {
        /* TODO or "localhost"? */
        server = "127.0.0.1";
    }

    /* TODO resolve DNS */
    inet_pton(AF_INET, server, &clnt_info.sin_addr);

    /* use current unix timestamp as state so that it always increments between calls (unless they're the same second) */
    /* TODO check that clock_gettime returns a signed 32 bit int for seconds (ie time_t == longword) */
    clock_gettime(CLOCK_REALTIME, &wall_clock);

    debug("Clearing locks for %s on %s with status %li\n", client_name, server, wall_clock.tv_sec);

    /* connect to server */
    client = create_rpc_client(&clnt_info, &hints, SM_PROG, version, timeout, src_ip);

    if (client) {
        //auth_destroy(client->cl_auth);
        //client->cl_auth = authunix_create_default();

        /* first try to notify the network status daemon that the client has rebooted */
        status = do_notify(client, client_name, wall_clock.tv_sec);

        /* if that doesn't work, free the locks directly on the network lock manager */
        if (status == NULL) {
            /* the client is set up to talk to the NSM so reconnect to the NLM */
            destroy_rpc_client(client);
            /* just do version 4 for now for NFS v3 */
            client = create_rpc_client(&clnt_info, &hints, NLM_PROG, 4, timeout, src_ip);
            //auth_destroy(client->cl_auth);
            //client->cl_auth = authunix_create_default();

            status = do_free_all(client, client_name, wall_clock.tv_sec);
        }
    } else {
        /* just do version 4 for now for NFS v3 */
        client = create_rpc_client(&clnt_info, &hints, NLM_PROG, 4, timeout, src_ip);
        auth_destroy(client->cl_auth);
        client->cl_auth = authunix_create_default();

        status = do_free_all(client, client_name, wall_clock.tv_sec);
    }

    if (status) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
