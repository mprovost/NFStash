#include "nfsping.h"

void usage() {
    printf("Usage: nfsmount [options] host:mountpoint\n\
    -m    use multiple target IP addresses if found\n\
    -T    use TCP (default UDP)\n"
    );

    exit(3);
}


u_int mount_perror(mountstat3 fhs_status) {
    switch (fhs_status) {
        case MNT3_OK:
            /* not an error */
            break;
        case MNT3ERR_NOENT:
            fprintf(stderr, "MNT3ERR_NOENT");
            break;
        case MNT3ERR_ACCES:
            fprintf(stderr, "MNT3ERR_ACCES");
            break;
        case MNT3ERR_NOTDIR:
            fprintf(stderr, "MNT3ERR_NOTDIR");
            break;
        case MNT3ERR_INVAL:
            fprintf(stderr, "MNT3ERR_INVAL");
            break;
        case MNT3ERR_NAMETOOLONG:
            fprintf(stderr, "MNT3ERR_NAMETOOLONG");
            break;
        case MNT3ERR_NOTSUPP:
            fprintf(stderr, "MNT3ERR_NOTSUPP");
            break;
        case MNT3ERR_SERVERFAULT:
            fprintf(stderr, "MNT3ERR_SERVERFAULT");
            break;
        default:
            fprintf(stderr, "UNKNOWN");
            break;
    }
    if (fhs_status)
        fprintf(stderr, "\n");
    return fhs_status;
}


mountres3 *get_root_filehandle(char *hostname, CLIENT *client, char *path) {
    struct rpc_err clnt_err;
    mountres3 *mountres;
    int i;

    if (path[0] == '/') {
        /* the actual RPC call */
        mountres = mountproc_mnt_3(&path, client);

        if (mountres) {
            if (mountres->fhs_status == MNT3_OK) {
                printf("%s:%s:", hostname, path);
                /* print the filehandle in hex */
                for (i = 0; i < mountres->mountres3_u.mountinfo.fhandle.fhandle3_len; i++) {
                    printf("%02hhx", mountres->mountres3_u.mountinfo.fhandle.fhandle3_val[i]);
                }
                printf("\n");
            } else {
                fprintf(stderr, "%s:%s: ", hostname, path);
                mount_perror(mountres->fhs_status);
            }
        } else {
            fprintf(stderr, "%s:%s: ", hostname, path);
            clnt_geterr(client, &clnt_err);
            /* check for authentication errors which probably mean it needs to come from a low port */
            /* TODO just print one error and exit? */
            /* TODO check if we are root already */
            if (clnt_err.re_status == RPC_AUTHERROR)
               fprintf(stderr, "Unable to mount filesystem, consider running as root\n");
            else
                clnt_perror(client, "mountproc_mnt_3");
        }
    } else {
        fprintf(stderr, "%s: Invalid path: %s\n", hostname, path);
        mountres->fhs_status = MNT3ERR_INVAL;
    }

    return mountres;
}


int main(int argc, char **argv) {
    mountres3 *mountres;
    struct sockaddr_in client_sock;
    char *error;
    char hostname[INET_ADDRSTRLEN];
    fhstatus result;
    int getaddr;
    struct addrinfo hints, *addr;
    char *host;
    char *path;
    exports ex, oldex;
    int ch, first, index;
    int multiple = 0;
    CLIENT *client;
    u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    int sock = RPC_ANYSOCK;

    client_sock.sin_family = AF_INET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    /* default to UDP */
    hints.ai_socktype = SOCK_DGRAM;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "hmT")) != -1) {
        switch(ch) {
            /* use multiple IP addresses */
            case 'm':
                multiple = 1;
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    host = strtok(argv[optind], ":");
    path = strtok(NULL, ":");

    /* DNS lookup */
    getaddr = getaddrinfo(host, "nfs", &hints, &addr);

    if (getaddr == 0) { /* success! */
        /* loop through possibly multiple DNS responses */
        while (addr) {
            client_sock.sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            /* new socket for each connection */
            sock = RPC_ANYSOCK;

            /* get mount port from portmapper */
            if (hints.ai_socktype == SOCK_DGRAM) {
                client_sock.sin_port = htons(pmap_getport(&client_sock, MOUNTPROG, version, IPPROTO_UDP));
                /* TODO check sin_port */
                client = clntudp_create(&client_sock, MOUNTPROG, version, timeout, &sock);
            } else {
                client_sock.sin_port = htons(pmap_getport(&client_sock, MOUNTPROG, version, IPPROTO_TCP));
                /* TODO check sin_port */
                client = clnttcp_create(&client_sock, MOUNTPROG, version, &sock, 0, 0);
            }

            if (client_sock.sin_port == 0) {
                clnt_pcreateerror("pmap_getport");
                mountres->fhs_status = MNT3ERR_SERVERFAULT; /* is this the most appropriate error code? */
            }
                    
            /* mounts don't need authentication because they return a list of authentication flavours supported */
            client->cl_auth = authnone_create();

            if (inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, hostname, INET_ADDRSTRLEN)) {
                if (path) {
                    mountres = get_root_filehandle(hostname, client, path);
                } else {
                    ex = *mountproc_export_3(NULL, client);

                    while (ex) {
                        mountres = get_root_filehandle(hostname, client, ex->ex_dir);
                        oldex = ex;
                        ex = ex->ex_next;
                        free(oldex);
                    }
                }
            } else {
                fprintf(stderr, "%s: ", hostname);
                perror("inet_ntop");
                return EXIT_FAILURE;
            }

            if (multiple)
                addr = addr->ai_next;
            else
                addr = NULL;
        }
    } else {
        fprintf(stderr, "%s: %s\n", host, gai_strerror(getaddr));
        return EXIT_FAILURE;
    }

    if (mountres)
        return EXIT_SUCCESS;
    else
        return EXIT_FAILURE;
}
