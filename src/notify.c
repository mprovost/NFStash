/*
 * NSM Notify
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

int verbose = 0;

void usage() {
    printf("Usage: nsmnotify [options] server\n\
    -h       display this help and exit\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n");

    exit(3);
}

/* SM_STAT */
/*
struct sm_name {
    char *mon_name;
};

struct sm_stat_res {
    sm_res res_stat;
    int state;
};
*/


/* SM_NOTIFY has no return value */
/*struct stat_chge {
    char *mon_name;
    int state;
  };
*/

void *do_notify(CLIENT *client, char *name, int state) {
    void *status;
    struct sm_name stat_name = {
        .mon_name = name
    };
    struct sm_stat_res *stat_res;
    struct stat_chge notify_stat = {
        .mon_name = name,
        .state = state
    };

    stat_res = sm_stat_1(&stat_name, client);

    if (stat_res && stat_res->res_stat == stat_succ) {
        printf("state = %i\n", stat_res->state);
    }

    /* the SM_NOTIFY call */
    /* returns NULL on RPC failure */
    status = sm_notify_1(&notify_stat, client);

    if (status) {
        printf("ok\n");
    }

    return status;
}


int main(int argc, char **argv) {
    int ch;
    char *name;
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

    while ((ch = getopt(argc, argv, "hS:Tv")) != -1) {
        switch(ch) {
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

    /* first argument */
    name = argv[1];

    /* second argument */
    inet_pton(AF_INET, argv[2], &clnt_info.sin_addr);

    clnt_info.sin_family = AF_INET;
    clnt_info.sin_port = 0;
    /* connect to server */
    client = create_rpc_client(&clnt_info, &hints, SM_PROG, version, timeout, src_ip);
    auth_destroy(client->cl_auth);
    client->cl_auth = authunix_create_default();

    if (client) {
        do_notify(client, name, 2);
    }
}
