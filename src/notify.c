/*
 * NSM Notify
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

int verbose = 0;

void usage() {
    printf("Usage: nsmnotify [options] server\n\
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
    char *server;
    struct timespec wall_clock;
    int newstate;
    void *status;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    unsigned long version = 1; /* for SM protocol */
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

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

    /* first argument */
    client_name = argv[optind];

    inet_pton(AF_INET, server, &clnt_info.sin_addr);

    clnt_info.sin_family = AF_INET;
    clnt_info.sin_port = 0;

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
