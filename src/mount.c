#include "nfsping.h"
#include "rpc.h"
#include "util.h"

void usage() {
    printf("Usage: nfsmount [options] host[:mountpoint]\n\
    -e       print exports (like showmount -e)\n\
    -m       use multiple target IP addresses if found\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n"
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


/* get the root filehandle from the server */
mountres3 *get_root_filehandle(char *hostname, CLIENT *client, char *path) {
    struct rpc_err clnt_err;
    mountres3 *mountres;

    if (path[0] == '/') {
        /* the actual RPC call */
        mountres = mountproc_mnt_3(&path, client);

        if (mountres) {
            if (mountres->fhs_status != MNT3_OK) {
                fprintf(stderr, "%s:%s: ", hostname, path);
                if (mountres->fhs_status == MNT3ERR_ACCES && geteuid()) {
                    fprintf(stderr, "Unable to mount filesystem, consider running as root\n");
                } else {
                    mount_perror(mountres->fhs_status);
                }
            }
        } else {
            fprintf(stderr, "%s:%s: ", hostname, path);
            clnt_geterr(client, &clnt_err);
            /* check for authentication errors which probably mean it needs to come from a low port */
            /* TODO just print one error and exit? */
            /* check if we are root already */
            if  (clnt_err.re_status == RPC_AUTHERROR && geteuid())
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


/* print a list of exports, like showmount -e */
int print_exports(char *host, struct exportnode *ex) {
    int i = 0;
    size_t max = 0;
    exports first = ex;
    groups gr;

    while (ex) {
        i++;
        if (strlen(ex->ex_dir) > max) {
            max = strlen(ex->ex_dir);
        }

        ex = ex->ex_next; 
    }

    ex = first;
    max++; /* spacing */
    
    while (ex) {
        printf("%s:%-*s", host, (int)max, ex->ex_dir);
        gr = ex->ex_groups;
        if (gr) {
            printf("%s", gr->gr_name);
            gr = gr->gr_next;
            while (gr) {
                printf(",%s", gr->gr_name);
                gr = gr->gr_next;
            }
        } else {
            /* no groups means open to everyone */
            printf("(everyone)");
        }
        printf("\n");
        ex = ex->ex_next;
    }

    return i;
}


int main(int argc, char **argv) {
    mountres3 *mountres;
    struct sockaddr_in client_sock;
    char *error;
    fhstatus result;
    int getaddr;
    struct addrinfo hints, *addr;
    char *host;
    char *path;
    exports ex;
    int exports_count = 0, exports_ok = 0;
    int ch, first, index;
    int showmount = 0;
    CLIENT *client;
    u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    client_sock.sin_family = AF_INET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    /* default to UDP */
    hints.ai_socktype = SOCK_DGRAM;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "ehS:T")) != -1) {
        switch(ch) {
            /* output like showmount -e */
            case 'e':
                showmount = 1;
                break;
            /* specify source address */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fprintf(stderr, "nfsping: Invalid source IP address!\n");
                    exit(3);
                }
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

    /* loop through arguments */
    while (optind < argc) {
        /* split host:path arguments, path is optional */
        host = strtok(argv[optind], ":");
        path = strtok(NULL, ":");

        /* DNS lookup */
        getaddr = getaddrinfo(host, "nfs", &hints, &addr);

        if (getaddr == 0) { /* success! */
            client_sock.sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            client_sock.sin_family = AF_INET;
            client_sock.sin_port = 0; /* use portmapper */

            /* create an rpc connection */
            client = create_rpc_client(&client_sock, &hints, MOUNTPROG, version, timeout, src_ip);
            /* mounts don't need authentication because they return a list of authentication flavours supported */
            client->cl_auth = authnone_create();

            if (client_sock.sin_port) {
                if (path) {
                    mountres = get_root_filehandle(host, client, path);
                    exports_count++;
                    if (mountres && mountres->fhs_status == MNT3_OK) {
                        exports_ok++;
                        /* print the filehandle in hex */
                        print_fh(host, path, mountres->mountres3_u.mountinfo.fhandle);
                    }
                } else {
                    /* get the list of all exported filesystems from the server */
                    ex = *mountproc_export_3(NULL, client);

                    if (ex) {
                        if (showmount) {
                            exports_count = print_exports(host, ex);
                            /* if the call succeeds at all it can't return individual bad results */
                            exports_ok = exports_count;
                        } else {
                            while (ex) {
                                mountres = get_root_filehandle(host, client, ex->ex_dir);
                                exports_count++;
                                if (mountres && mountres->fhs_status == MNT3_OK) {
                                    exports_ok++;
                                    /* print the filehandle in hex */
                                    print_fh(host, ex->ex_dir, mountres->mountres3_u.mountinfo.fhandle);
                                }
                                ex = ex->ex_next;
                            }
                        }
                    }
                }
            } else {
                clnt_pcreateerror("pmap_getport");
                mountres->fhs_status = MNT3ERR_SERVERFAULT; /* is this the most appropriate error code? */
            }
        } else {
            fprintf(stderr, "%s: %s\n", host, gai_strerror(getaddr));
            /* TODO soldier on with other arguments or bail at first sign of trouble? */
            return EXIT_FAILURE;
        }

        optind++;
    }

    if (exports_count && exports_count == exports_ok)
        return EXIT_SUCCESS;

    return EXIT_FAILURE;
}
