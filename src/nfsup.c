#include "nfsping.h"
#include "util.h"
#include "rpc.h"
#include "nagios.h"

/* globals */
int verbose = 1;

int main(int argc, char **argv) {
    void                *status = NULL;
    CLIENT              *client;
    struct sockaddr_in  sock    = {
        .sin_family     = AF_INET,
        .sin_port       = 0, /* use the portmapper */
    };
    struct addrinfo     hints   = {
        .ai_family      = AF_INET,
        /* default to UDP */
        .ai_socktype    = SOCK_DGRAM,
    };
    /* source ip address for packets */
    struct sockaddr_in  src_ip = {
        .sin_family     = AF_INET,
        .sin_addr       = INADDR_ANY,
    };
    unsigned long       prognum = NFS_PROGRAM;
    /* default to NFS v3 */
    unsigned long       version = 3;
    struct timeval      timeout = NFS_TIMEOUT;

    if (inet_pton(AF_INET, argv[1], &sock.sin_addr)) {

        /* first check the portmapper */
        /* just do a NULL check because we'll use it to find specific ports for other protocols */
        /* convert the port to network byte order */
        sock.sin_port = htons(PMAPPORT);
        /* only one version of portmap protocol */
        version = 2;
        client = create_rpc_client(&sock, &hints, PMAPPROG, version, timeout, src_ip);

        if (client) {
            status = pmapproc_null_2(NULL, client);

            if (status) {
                printf("pmap ok\n");
                destroy_rpc_client(client);

                sock.sin_port = 0; /* use portmapper */
                prognum = MOUNTPROG; /* check the mount protocol */
                version = 3; /* NFSv3 */
                client = create_rpc_client(&sock, &hints, prognum, version, timeout, src_ip);

                if (client) {
                    /* get a list of exports from the server */
                    status = mountproc_export_3(NULL, client);

                    if (status) {
                        printf("mount ok\n");
                        destroy_rpc_client(client);

                        sock.sin_port = 0; /* use portmapper */
                        prognum = NFS_PROGRAM;
                        version = 3; /* NFSv3 */
                        client = create_rpc_client(&sock, &hints, prognum, version, timeout, src_ip);

                        if (client) {
                            status = nfsproc3_null_3(NULL, client);

                            if (status) {
                                printf("nfs ok\n");
                                destroy_rpc_client(client);
                            } else {
                                printf("nfs fail\n");
                            }
                        } else {
                            printf("nfs fail\n");
                            status = 0;
                        }
                    } else {
                        printf("mount fail\n");
                    }
                } else {
                    printf("mount fail\n");
                    status = 0;
                }
            } else {
                printf("pmap fail\n");
            }
        }
    }

    if (status) {
        return STATE_OK;
    } else {
        return STATE_CRITICAL;
    }
}
