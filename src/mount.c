/*
 * Get root filehandles from NFS server
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static void mount_perror(mountstat3);
static exports get_exports(CLIENT *, char *, unsigned long *);
static mountres3 *get_root_filehandle(CLIENT *, char *, char *, unsigned long *);
static int print_exports(char *, struct exportnode *);
static targets_t *make_exports(targets_t *, const uint16_t);

/* globals */
extern volatile sig_atomic_t quitting;
int verbose = 0;


void usage() {
    printf("Usage: nfsmount [options] host[:mountpoint]\n\
    -e       print exports (like showmount -e)\n\
    -h       display this help and exit\n\
    -l       loop forever\n\
    -m       use multiple target IP addresses if found\n\
    -p n     polling interval, check targets every n ms (default 1000)\n\
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


/* get the list of exports from a server */
exports get_exports(CLIENT *client, char *hostname, unsigned long *usec) {
    struct rpc_err clnt_err;
    exports ex = NULL;
    struct timespec call_start, call_end, call_elapsed;

    if (client) {
        /* first time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif

        /* the actual RPC call */
        ex = *mountproc_export_3(NULL, client);

        /* second time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif

        /* calculate elapsed microseconds */
        timespecsub(&call_end, &call_start, &call_elapsed);
        *usec = ts2us(call_elapsed);
    }

    /* export call doesn't return errors */
    /* it also doesn't usually require a privileged port */
    if (ex == NULL) {
        /* RPC error */
        clnt_geterr(client, &clnt_err);
        if  (clnt_err.re_status) {
            fprintf(stderr, "%s: ", hostname);
            clnt_perror(client, "mountproc_export_3");
        }
    }

    return ex;
}


/* get the root filehandle from the server */
/* take a pointer to usec so we can return the elapsed call time */
mountres3 *get_root_filehandle(CLIENT *client, char *hostname, char *path, unsigned long *usec) {
    struct rpc_err clnt_err;
    mountres3 *mountres = NULL;
    struct timespec call_start, call_end, call_elapsed;

    if (client) {
        /* first time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif

        /* the actual RPC call */
        mountres = mountproc_mnt_3(&path, client);

        /* second time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif

        /* calculate elapsed microseconds */
        timespecsub(&call_end, &call_start, &call_elapsed);
        *usec = ts2us(call_elapsed);
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


/* make a target list by querying the server for a list of exports */
targets_t *make_exports(targets_t *target, const uint16_t port) {
    exports ex;
    /* timer for the exports call */
    unsigned long usec = 0;
    targets_t dummy;
    targets_t *current = &dummy;
    targets_t *head = current;

    if (target->client) {
        /* get the list of exports from the server */
        ex = get_exports(target->client, target->name, &usec);
        printf("get_exports: %lu\n", usec);

        while (ex) {
            /* allocate a new entry */
            current->next = init_target(target->name, port);
            current = current->next;
            /* copy the hostname from the mount result into the target */
            current->path = calloc(1, MNTPATHLEN);
            strncpy(current->path, ex->ex_dir, MNTPATHLEN);
            /* reuse the same client connection for each export */
            current->client = target->client;
            /* and the same client_sock */
            current->client_sock = target->client_sock;

            ex = ex->ex_next;
        }
    }

    /* skip the dummy entry */
    return head->next;
}


int main(int argc, char **argv) {
    mountres3 *mountres;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM
    };
    char *host;
    char *path;
    exports ex;
    targets_t *targets;
    targets_t *current;
    targets_t target_dummy = {0};
    targets_t *new_targets;
    targets_t exports_dummy = {0};
    targets_t *exports_dummy_ptr = &exports_dummy;
    unsigned int exports_count = 0, exports_ok = 0;
    int ch;
    /* command line options */
    uint16_t port = 0; /* 0 = use portmapper */
    int dns = 0, ip = 0;
    int loop = 0;
    int multiple = 0, showmount = 0;
    u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    struct timespec sleep_time = NFS_SLEEP;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };
    unsigned long usec;
    struct timespec wall_clock;
    JSON_Object *json;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "ehlmp:S:Tv")) != -1) {
        switch(ch) {
            /* output like showmount -e */
            case 'e':
                showmount = 1;
                break;
            case 'l':
                loop = 1;
                break;
            /* use multiple IP addresses if found */
            /* TODO in this case do we also want to default to showing IP addresses instead of names? */
            case 'm':
                multiple = 1;
                break;
            /* time between pings to target */
            case 'p':
                /* TODO check for reasonable values */
                ms2ts(&sleep_time, strtoul(optarg, NULL, 10));
                break;
            /* specify source address */
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
            case '?':
            default:
                usage();
        }
    }

    /* pointer to head of list */
    current = &target_dummy;
    targets = current;

    /* loop through arguments and create targets */
    /* TODO accept from stdin? */
    while (optind < argc) {
        /* split host:path arguments, path is optional */
        host = strtok(argv[optind], ":");
        path = strtok(NULL, ":");

        if (path) {
            /* check for valid path */
            if (path[0] != '/') {
                fatalx(3, "%s: Invalid path: %s\n", host, path);
            }

            if (showmount) {
                fatalx(3, "Can't specify -e (exports) and a path!\n");
            }
        }

        /* make possibly multiple new targets */
        new_targets = make_target(host, &hints, port, dns, ip, multiple);

        /* go through this argument's list of possibly multiple dns responses/targets */

        while (new_targets) {
            if (path) {
                new_targets->path = path;
            /* no path given, look up exports on server */
            } else {
                /* first create an rpc connection so we can query the server for an exports list */
                new_targets->client = create_rpc_client(new_targets->client_sock, &hints, MOUNTPROG, version, timeout, src_ip);

                if (showmount) {
                    ex = get_exports(new_targets->client, host, &usec);
                    exports_count = print_exports(host, ex);
                } else {
                    /* look up the export list on the server and create a target for each */
                    append_target(&exports_dummy_ptr, make_exports(new_targets, port));
                }
            }

            new_targets = new_targets->next;
        }

        /* append to the global target list */
        if (path) {
            append_target(&targets, new_targets);
        } else {
            /* TODO showmount */
            /* skip the dummy entry */
            append_target(&targets, exports_dummy.next);
        }

        optind++;
    }

    /* listen for ctrl-c */
    quitting = 0;
    signal(SIGINT, int_handler);

    /* skip the first dummy entry */
    if (showmount) {
        targets = NULL;
    } else {
        targets = targets->next;
    }

    while(1) {
        /* reset to head of list */
        current = targets;

        while(current) {

            if (current->client == NULL) {
                /* create an rpc connection */
                current->client = create_rpc_client(current->client_sock, &hints, MOUNTPROG, version, timeout, src_ip);
                /* mounts don't need authentication because they return a list of authentication flavours supported so leave it as default (AUTH_NONE) */
            }

            if (current->client) {
                    /* get the current timestamp */
                    clock_gettime(CLOCK_REALTIME, &wall_clock);

                    mountres = get_root_filehandle(current->client, current->name, current->path, &usec);

                    if (mountres && mountres->fhs_status == MNT3_OK) {
                        exports_ok++;

                        json = json_value_get_object(current->json_root);

                        json_object_set_number(json, "timestamp", wall_clock.tv_sec);

                        /* print the filehandle in hex */
                        print_fhandle3(current->json_root, current->client_sock, current->path, mountres->mountres3_u.mountinfo.fhandle, usec, wall_clock);
                    }
            }

            current = current->next;

        } /* while(current) */

        if (loop) {
            /* sleep between rounds */
            nanosleep(&sleep_time, NULL);
        } else {
            break;
        }

        /* ctrl-c */
        if (quitting) {
            break;
        }
    } /* while(1) */

    if (exports_count && exports_count == exports_ok) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
