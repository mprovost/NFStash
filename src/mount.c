/*
 * Get root filehandles from NFS server
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static void mount_perror(mountstat3);
static mountres3 *get_root_filehandle(CLIENT *, char *, char *);
static int print_exports(char *, struct exportnode *);

/* globals */
int verbose = 0;


void usage() {
    printf("Usage: nfsmount [options] host[:mountpoint]\n\
    -e       print exports (like showmount -e)\n\
    -h       display this help and exit\n\
    -m       use multiple target IP addresses if found\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n");

    exit(3);
}


/* print an error message for mount results */
/* for now just print the error name from the header */
void mount_perror(mountstat3 fhs_status) {
    static const char *labels[] = {
        [MNT3ERR_NOENT]       = "MNT3ERR_NOENT",
        [MNT3ERR_ACCES]       = "MNT3ERR_ACCES",
        [MNT3ERR_NOTDIR]      = "MNT3ERR_NOTDIR",
        [MNT3ERR_INVAL]       = "MNT3ERR_INVAL",
        [MNT3ERR_NAMETOOLONG] = "MNT3ERR_NAMETOOLONG",
        [MNT3ERR_NOTSUPP]     = "MNT3ERR_NOTSUPP",
        [MNT3ERR_SERVERFAULT] = "MNT3ERR_SERVERFAULT",
    };

    if (fhs_status && fhs_status != MNT3_OK) {
        fprintf(stderr, "%s\n", labels[fhs_status]);
    }
}


/* get the root filehandle from the server */
mountres3 *get_root_filehandle(CLIENT *client, char *hostname, char *path) {
    struct rpc_err clnt_err;
    mountres3 *mountres = NULL;

    if (path[0] == '/') {
        /* the actual RPC call */
        if (client) {
            mountres = mountproc_mnt_3(&path, client);
        }

        if (mountres) {
            if (mountres->fhs_status != MNT3_OK) {
                fprintf(stderr, "%s:%s: ", hostname, path);
                /* check if we get an access error, this probably means the server wants us to use a reserved port */
                /* TODO do this check in mount_perror? */
                if (mountres->fhs_status == MNT3ERR_ACCES && geteuid()) {
                    fprintf(stderr, "Unable to mount filesystem, consider running as root\n");
                } else {
                    mount_perror(mountres->fhs_status);
                }
            }
        /* RPC error */
        } else {
            clnt_geterr(client, &clnt_err);
            if  (clnt_err.re_status) {
                fprintf(stderr, "%s:%s: ", hostname, path);
                clnt_perror(client, "mountproc_mnt_3");
            }
        }
    } else {
        fprintf(stderr, "%s: Invalid path: %s\n", hostname, path);
        /* create an empty result */
        mountres = malloc(sizeof(mountres));
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
    int getaddr;
    struct addrinfo hints, *addr;
    char *host;
    char *path;
    exports ex;
    int exports_count = 0, exports_ok = 0;
    int ch;
    /* command line options */
    int multiple = 0, showmount = 0;
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

    while ((ch = getopt(argc, argv, "ehmS:Tv")) != -1) {
        switch(ch) {
            /* output like showmount -e */
            case 'e':
                showmount = 1;
                break;
            /* specify source address */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            /* use multiple IP addresses if found */
            /* TODO in this case do we also want to default to showing IP addresses instead of names? */
            case 'm':
                multiple = 1;
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
            /* loop through possibly multiple DNS responses */
            while (addr) {
                client_sock.sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
                client_sock.sin_family = AF_INET;
                client_sock.sin_port = 0; /* use portmapper */

                /* create an rpc connection */
                client = create_rpc_client(&client_sock, &hints, MOUNTPROG, version, timeout, src_ip);
                /* mounts don't need authentication because they return a list of authentication flavours supported so leave it as default (AUTH_NONE) */

                if (client) {
                    if (path && !showmount) {
                        mountres = get_root_filehandle(client, host, path);
                        exports_count++;
                        if (mountres && mountres->fhs_status == MNT3_OK) {
                            exports_ok++;
                            /* print the filehandle in hex */
                            print_fhandle3(addr->ai_addr, path, mountres->mountres3_u.mountinfo.fhandle);
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
                                    mountres = get_root_filehandle(client, host, ex->ex_dir);
                                    exports_count++;
                                    if (mountres && mountres->fhs_status == MNT3_OK) {
                                        exports_ok++;
                                        /* print the filehandle in hex */
                                        print_fhandle3(addr->ai_addr, ex->ex_dir, mountres->mountres3_u.mountinfo.fhandle);
                                    }
                                    ex = ex->ex_next;
                                }
                            }
                        }
                    }
                }

                if (multiple) {
                    addr = addr->ai_next;
                } else {
                    break;
                }
            }
        } else {
            fprintf(stderr, "%s: %s\n", host, gai_strerror(getaddr));
            /* TODO soldier on with other arguments or bail at first sign of trouble? */
            return EXIT_FAILURE;
        }

        optind++;
    }

    if (exports_count && exports_count == exports_ok) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
