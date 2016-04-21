/*
 * Get root filehandles from NFS server
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static void mount_perror(mountstat3);
static exports get_exports(struct targets *);
static mountres3 *fhstatus_to_mountres3(fhstatus *);
static mountres3 *copy_mountres3(mountres3 *);
static void free_mountres3(mountres3 *);
static mountres3 *mountproc_mnt_x(char *, CLIENT *);
static fhandle3 *get_root_filehandle(CLIENT *, char *, char *, fhandle3 *, unsigned long *);
static int print_exports(char *, struct exportnode *);
static struct mount_exports *make_exports(targets_t *);
static int print_fhandle3(JSON_Object *, const fhandle3, const unsigned long, const struct timespec);
void print_output(enum outputs, const char *, const int, const char *, const char *, struct mount_exports *, const fhandle3, const struct timespec, unsigned long);
void print_summary(targets_t *, enum outputs, const int, const int);

/* globals */
extern volatile sig_atomic_t quitting;
int verbose = 0;

/* global config "object" */
static struct config {
    enum outputs format;
    char *prefix;
    u_long version;
    unsigned long count;
    uint16_t port;
    int dns;
    int ip;
    int loop;
    int multiple;
    int quiet;
    struct timeval timeout;
    unsigned long hertz;
} cfg;

/* MOUNT protocol function pointers */
/* EXPORT procedure */
typedef exports *(*proc_export_t)(void *, CLIENT *);

struct export_procs {
    /* function pointer */
    proc_export_t proc;
    /* store the name as a string for error messages */
    char *name;
    /* protocol name for output functions */
    char *protocol;
    /* version is the filehandle version, ie NFSv2 or v3 */
    u_long version;
};

/* array to store pointers to mount procedures for different mount protocol versions */
static const struct export_procs export_dispatch[4] = {
    [1] = { .proc = mountproc_export_1, .name = "mountproc_export_1", .protocol = "mountv1", .version = 2 },
    [2] = { .proc = mountproc_export_2, .name = "mountproc_export_2", .protocol = "mountv2", .version = 2 },
    [3] = { .proc = mountproc_export_3, .name = "mountproc_export_3", .protocol = "mountv3", .version = 3 },
};


void usage() {
    printf("Usage: nfsmount [options] host[:mountpoint]\n\
    -A       show IP addresses\n\
    -c n     count of mount requests to send to target\n\
    -C n     same as -c, output parseable format\n\
    -D       print timestamp (unix time) before each line\n\
    -e       print exports (like showmount -e)\n\
    -E       StatsD format output\n\
    -G       Graphite format output\n\
    -h       display this help and exit\n\
    -H n     frequency in Hertz (requests per second, default 1)\n\
    -J       force JSON output\n\
    -l       loop forever\n\
    -m       use multiple target IP addresses if found (implies -A)\n\
    -q       quiet, only print summary\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n\
    -V n     MOUNT protocol version (1/2/3, default 3)\n"
    );

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
exports get_exports(struct targets *target) {
    struct rpc_err clnt_err;
    exports ex = NULL;
    unsigned long usec;
    struct timespec call_start, call_end, call_elapsed;

    if (target->client) {
        /* first time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif

        /* the actual RPC call */
        ex = *export_dispatch[cfg.version].proc(NULL, target->client);

        /* second time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif

        /* calculate elapsed microseconds */
        timespecsub(&call_end, &call_start, &call_elapsed);
        usec = ts2us(call_elapsed);

        /* only print timing to stderr if verbose is enabled */
        /* TODO unless we're doing graphite output */
        debug("%s (%s): %s=%03.2f ms\n", target->name, target->ip_address, export_dispatch[cfg.version].name, usec / 1000.0);
    }

    /* export call doesn't return errors */
    /* it also doesn't usually require a privileged port */
    if (ex == NULL) {
        /* RPC error */
        clnt_geterr(target->client, &clnt_err);
        if  (clnt_err.re_status) {
            fprintf(stderr, "%s: ", target->name);
            clnt_perror(target->client, "mountproc_export_3");
        }
    }

    return ex;
}


/* convert a version 1/2 fhstatus result to a version 3 mountres */
mountres3 *fhstatus_to_mountres3(fhstatus *status) {
    mountres3 *mountres = NULL;

    if (status) {
        mountres = calloc(1, sizeof(mountres3));

        /* status codes are the same between versions */
        mountres->fhs_status = status->fhs_status;

        /* copy the filehandle array pointer */
        /* version 1/2 are fixed length array */
        mountres->mountres3_u.mountinfo.fhandle.fhandle3_len = FHSIZE;
        mountres->mountres3_u.mountinfo.fhandle.fhandle3_val = calloc(FHSIZE, sizeof(char));
        memcpy(mountres->mountres3_u.mountinfo.fhandle.fhandle3_val, status->fhstatus_u.fhs_fhandle, FHSIZE);

        /* set the default AUTH_SYS authentication */
        /* make a default authentication list */
        mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_len = 1;
        mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_val = calloc(1, sizeof(int));
        mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_val[0] = AUTH_SYS;
    }

    return mountres;
}


/* make a deep copy of a mountres3 struct */
mountres3 *copy_mountres3(mountres3 *mountres) {
    mountres3 *new_mountres = calloc(1, sizeof(mountres3));

    /* shallow copy */
    *new_mountres = *mountres;

    /* copy the filehandle */
    new_mountres->mountres3_u.mountinfo.fhandle.fhandle3_val = calloc(mountres->mountres3_u.mountinfo.fhandle.fhandle3_len, sizeof(char));
    memcpy(new_mountres->mountres3_u.mountinfo.fhandle.fhandle3_val, mountres->mountres3_u.mountinfo.fhandle.fhandle3_val, mountres->mountres3_u.mountinfo.fhandle.fhandle3_len);

    /* copy the authentication list */
    new_mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_val = calloc(mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_len, sizeof(int));
    memcpy(new_mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_val, mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_val, mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_len);

    return new_mountres;
}


/* frees a mountres3 struct */
void free_mountres3(mountres3 *mountres) {
    /* free the filehandle */
    free(mountres->mountres3_u.mountinfo.fhandle.fhandle3_val);
    /* free the authentication list */
    free(mountres->mountres3_u.mountinfo.auth_flavors.auth_flavors_val);
    /* finally free the mountres3 */
    free(mountres);
}


/* wrapper for mountproc_mnt that handles different protocol versions and always returns a v3 result */
/* this frees the RPC result in the client and returns a newly allocated mountres which must be freed using free_mountres3() */
mountres3 *mountproc_mnt_x(char *path, CLIENT *client) {
    /* for versions 1 and 2 */
    fhstatus *status = NULL;
    /* for version 3 */
    mountres3 *mountres = NULL;
    /* temp result */
    mountres3 *result;

    /* the actual RPC call */
    switch (cfg.version) {
        case 1:
            status = mountproc_mnt_1(&path, client);
            /* convert to v3 */
            mountres = fhstatus_to_mountres3(status);
            /* free fhstatus */
            if (clnt_freeres(client, (xdrproc_t) xdr_fhstatus, (caddr_t) status) == 0) {
                fatalx(3, "Couldn't free fhstatus!\n");
            }
            break;
        case 2:
            status = mountproc_mnt_2(&path, client);
            /* convert to v3 */
            mountres = fhstatus_to_mountres3(status);
            /* free fhstatus */
            if (clnt_freeres(client, (xdrproc_t) xdr_fhstatus, (caddr_t) status) == 0) {
                fatalx(3, "Couldn't free fhstatus!\n");
            }
            break;
        case 3:
            result = mountproc_mnt_3(&path, client);
            /* make a copy so it's out of the RPC client */
            mountres = copy_mountres3(result);
            /* now free the result in the client */
            if (clnt_freeres(client, (xdrproc_t) xdr_mountres3, (caddr_t) result) == 0) {
                fatalx(3, "Couldn't free mountres3!\n");
            }
            break;
        default:
            fatal("Illegal protocol version %lu!\n", cfg.version);
    }

    return mountres;
}


/* get the root filehandle from the server */
/* take a pointer to usec so we can return the elapsed call time */
/* if protocol versions 1 or 2 are used, create a synthetic v3 result */
/* TODO have this just return the filehandle and not a mountres? */
fhandle3 *get_root_filehandle(CLIENT *client, char *hostname, char *path, fhandle3 *root, unsigned long *usec) {
    struct rpc_err clnt_err;
    mountres3 *mountres = NULL;
    struct timespec call_start, call_end, call_elapsed;

    /* on error return a blank filehandle */
    root->fhandle3_len = 0;

    if (client) {
        /* first time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif
        /* the RPC call */
        mountres = mountproc_mnt_x(path, client);

        /* second time marker */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
        clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif

        /* calculate elapsed microseconds */
        timespecsub(&call_end, &call_start, &call_elapsed);
        *usec = ts2us(call_elapsed);

        /* process the result */
        if (mountres) {
            if (mountres->fhs_status == MNT3_OK) {
                /* copy the filehandle */
                root->fhandle3_len = mountres->mountres3_u.mountinfo.fhandle.fhandle3_len;
                memcpy(root->fhandle3_val, mountres->mountres3_u.mountinfo.fhandle.fhandle3_val, root->fhandle3_len);
            } else {
                fprintf(stderr, "%s:%s: ", hostname, path);
                /* check if we get an access error, this probably means the server wants us to use a reserved port */
                /* TODO do this check in mount_perror? */
                if (mountres->fhs_status == MNT3ERR_ACCES && geteuid()) {
                    fprintf(stderr, "Unable to mount filesystem, consider running as root\n");
                } else {
                    mount_perror(mountres->fhs_status);
                }
            }
            /* clean up the result */
            free_mountres3(mountres);
        /* RPC error */
        } else {
            clnt_geterr(client, &clnt_err);
            if  (clnt_err.re_status) {
                fprintf(stderr, "%s:%s: ", hostname, path);
                /* TODO specific versions? */
                clnt_perror(client, "mountproc_mnt_x");
            }
        }
    } /* if(client) */

    return root;
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


/* make an export list by querying the server for a list of exports */
struct mount_exports *make_exports(targets_t *target) {
    exports ex;
    struct mount_exports dummy;
    struct mount_exports *current = &dummy;

    dummy.next = NULL;

    if (target->client) {
        /* get the list of exports from the server */
        ex = get_exports(target);

        while (ex) {
            /* copy the target don't make a new one */
            current->next = calloc(1, sizeof(struct mount_exports));
            /* TODO allocate space for fping results */
            current = current->next;
            /* terminate the list */
            current->next = NULL;

            /* copy the hostname from the mount result into the target */
            strncpy(current->path, ex->ex_dir, MNTPATHLEN);

            ex = ex->ex_next;
        }
    }

    /* skip the dummy entry */
    return dummy.next;
}


/* print a MOUNT filehandle as a series of hex bytes wrapped in a JSON object */
int print_fhandle3(JSON_Object *json_obj, const fhandle3 file_handle, const unsigned long usec, const struct timespec wall_clock) {
    unsigned int i;
    /* two chars for each byte (FF in hex) plus terminating NULL */
    char fh_string[NFS3_FHSIZE * 2 + 1];
    char *my_json_string;

    /* this escapes / to \/ */
    //json_object_set_string(json_obj, "path", target->path);
    json_object_set_number(json_obj, "usec", usec);
    json_object_set_number(json_obj, "timestamp", wall_clock.tv_sec);

    /* walk through the NFS filehandle, print each byte as two hex characters */
    for (i = 0; i < file_handle.fhandle3_len; i++) {
        sprintf(&fh_string[i * 2], "%02hhx", file_handle.fhandle3_val[i]);
    
    }

    json_object_set_string(json_obj, "filehandle", fh_string);

    /* NFS filehandle version */
    json_object_set_number(json_obj, "version", export_dispatch[cfg.version].version);

    //my_json_string = json_serialize_to_string(json_obj);
    //printf("%s\n", my_json_string);
    //json_free_serialized_string(my_json_string);

    return i;
}


/* print output to stdout in different formats for each mount result */
void print_output(enum outputs format, const char *prefix, const int width, const char *display_name, const char *ndqf, struct mount_exports *export, const fhandle3 file_handle, const struct timespec wall_clock, unsigned long usec) {
    double loss = (export->sent - export->received) / export->sent * 100.0;
    char epoch[TIME_T_MAX_DIGITS]; /* the largest time_t seconds value, plus a terminating NUL */
    struct tm *secs;

    switch(format) {
        case unixtime:
            /* get the epoch time in seconds in the local timezone */
            /* TODO should we be doing everything in UTC? */
            /* strftime needs a struct tm so use localtime to convert from time_t */
            secs = localtime(&wall_clock.tv_sec);
            strftime(epoch, sizeof(epoch), "%s", secs);
            printf("[%s.%06li] ", epoch, wall_clock.tv_nsec / 1000);
            /* fall through to ping output, this just prepends the current time */
            /*FALLTHROUGH*/
        /* print "ping" style output */
        case ping:
        case fping: /* fping is only different in the summary at the end */
            printf("%s:%-*s : [%u], %03.2f ms (%03.2f avg, %.0f%% loss)\n",
                display_name,
                /* have to cast size_t to int for compiler warning */
                /* printf only accepts ints for field widths with * */
                width - (int)strlen(display_name),
                export->path,
                export->sent - 1,
                usec / 1000.0,
                export->avg / 1000.0,
                loss);
            break;
        /* Graphite output */
        case graphite:
            /* TODO use escape_char from df.c to escape paths */
            printf("%s.%s.%s.%s.usec %lu %li\n",
                prefix, ndqf, export->path, 
                /* use exports struct to get version string */
                export_dispatch[cfg.version].protocol,
                usec, wall_clock.tv_sec);
            break;
        case statsd:
            printf("%s.%s.%s.%s:%03.2f|ms\n",
                prefix, ndqf, export->path, 
                /* use exports struct to get version string */
                export_dispatch[cfg.version].protocol,
                usec / 1000.0);
            break;
        /* print the filehandle as JSON */
        case json:
            print_fhandle3(json_value_get_object(export->json_root), file_handle, usec, wall_clock);
            break;
        /* this is handled in print_exports() */
        case showmount:
            /* shouldn't get here */
            fatal("No showmount support in print_output()!\n");
            break;
        case unset:
            fatal("Need a format!\n");
            break;
    }
}


/* print a summary to stderr */
void print_summary(targets_t *targets, enum outputs format, const int width, const int ip) {
    targets_t *current = targets;
    struct mount_exports *export;
    double loss;
    unsigned long i;
    char *display_name;

    switch (format) {
        /* print a summary for these formats */
        case ping:
        case unixtime:
        case fping:
            /* print a newline between the results and the summary */
            fprintf(stderr, "\n");

            while (current) {
                export = current->exports;
                while (export) {
                    /* whether to display IP address or hostname */
                    if (ip) {
                        display_name = current->ip_address;
                    } else {
                        display_name = current->name;
                    }

                    /* first print the aligned host and path */
                    fprintf(stderr, "%s:%-*s :",
                        display_name,
                        width - (int)strlen(display_name),
                        export->path);

                    switch (format) {
                        case ping:
                        case unixtime:
                        case json: /* not sure if this makes sense? */
                            loss = (export->sent - export->received) / export->sent * 100.0;
                            fprintf(stderr, " xmt/rcv/%%loss = %u/%u/%.0f%%",
                                export->sent, export->received, loss);
                            /* only print times if we got any responses */
                            if (export->received) {
                                fprintf(stderr, ", min/avg/max = %.2f/%.2f/%.2f",
                                    export->min / 1000.0, export->avg / 1000.0, export->max / 1000.0);
                            }
                            break;
                        case fping:
                            for (i = 0; i < export->sent; i++) {
                                if (export->results[i]) {
                                    fprintf(stderr, " %.2f", export->results[i] / 1000.0);
                                } else {
                                    fprintf(stderr, " -");
                                }
                            }
                            break;
                        default:
                            fatalx(3, "There should be a summary here!");
                    }

                    fprintf(stderr, "\n");

                    export = export->next;
                }

                current = current->next;
            } /* while (current) */

            break;
        /* skip the summary for these formats */
        case unset: /* this shouldn't happen but keeps compiler quiet */
        case json:
        case graphite:
        case statsd:
        case showmount:
            break;
    }
}


int main(int argc, char **argv) {
    /* allocate space for root filehandle */
    char fhandle3_val[FHSIZE3];
    fhandle3 root = {
        .fhandle3_len = 0,
        .fhandle3_val = fhandle3_val
    };
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM
    };
    char *host;
    char *path;
    char *display_name; /* for print_output() */
    /* RPC call results */
    exports ex;
    /* target lists */
    targets_t target_dummy = {0};
    /* pointer to head of list */
    targets_t *current = &target_dummy;
    /* global target list */
    targets_t *targets = current;
    /* current export */
    struct mount_exports *export = NULL;
    /* counters for results */
    unsigned int exports_count = 0;
    unsigned int exports_ok    = 0;
    /* getopt */
    int ch;
    struct timespec sleep_time;
    struct timespec wall_clock;
    struct timespec loop_start, loop_end, loop_elapsed, sleepy;
    /* response time in microseconds */
    unsigned long usec = 0;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };
    /* for output alignment */
    /* printf requires an int for %*s formats */
    int width    = 0;
    int tmpwidth = 0;

    /* default config */
    const struct config CONFIG_DEFAULT = {
        /* default to unset so we can check in getopt */
        .format    = unset,
        .prefix    = "nfsmount",
        /* default to version 3 for NFSv3 */
        .version  = 3,
        .timeout  = NFS_TIMEOUT,
        .hertz    = NFS_HERTZ,
        .count    = 0,
        .port     = 0, /* 0 = use portmapper */
        /* reverse DNS lookups */
        .dns      = 0,
        .ip       = 0,
        .loop     = 0,
        .multiple = 0,
        .quiet    = 0,
    };

    cfg = CONFIG_DEFAULT;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "Ac:C:dDeEGhH:JlmqS:TvV:")) != -1) {
        switch(ch) {
            /* show IP addresses instead of hostnames */
            case 'A':
                /* check for conflicting option to do reverse DNS lookups */
                if (cfg.dns) {
                    fatal("Can't specify both -d and -A!\n");
                } else {
                    cfg.ip = 1;
                }
                break;
            /* ping output with a count */
            case 'c':
                if (cfg.loop) {
                    fatal("Can't specify both -l and -c!\n");
                }

                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case ping:
                        cfg.format = ping;
                        break;
                    case fping:
                        fatal("Can't specify both -C and -c!\n");
                        break;
                    case showmount:
                        fatal("Can't specify both -e and -c!\n");
                        break;
                    /* -D/-J/-G/-E are ok */
                    case unixtime:
                    case json:
                    case graphite:
                    case statsd:
                        break;
                }

                cfg.count = strtoul(optarg, NULL, 10);
                if (cfg.count == 0 || cfg.count == ULONG_MAX) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            case 'C':
                if (cfg.loop) {
                    fatal("Can't specify both -l and -C!\n");
                }

                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case fping:
                        cfg.format = fping;
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -C!\n");
                        break;
                    case ping:
                        fatal("Can't specify both -c and -C!\n");
                        break;
                    case showmount:
                        fatal("Can't specify both -e and -C!\n");
                        break;
                    case json:
                        /* JSON doesn't have a summary */
                        fatal("Can't specify both -J and -C, use -c instead!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -C, use -c instead!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -C, use -c instead!\n");
                        break;
                }

                cfg.count = strtoul(optarg, NULL, 10);
                if (cfg.count == 0 || cfg.count == ULONG_MAX) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            /* reverse DNS lookups */
            case 'd':
                /* check if option to use IP addresses was already set */
                if (cfg.ip) {
                    /* this could have been set by -m */
                    if (cfg.multiple) {
                        /* override with new setting */
                        cfg.ip = 0;
                        cfg.dns = 1;
                    /* set with -A */
                    } else {
                        fatal("Can't specify both -A and -d!\n");
                    }
                } else {
                    cfg.dns = 1;
                }
                break;
            /* unixtime ping output */
            case 'D':
                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case unixtime:
                    case ping:
                        cfg.format = unixtime;
                        break;
                    case fping:
                        fatal("Can't specify both -C and -D!\n");
                        break;
                    case showmount:
                        fatal("Can't specify both -e and -D!\n");
                        break;
                    case json:
                        fatal("Can't specify both -J and -D!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -D!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -D!\n");
                        break;
                }
                break;
            /* output like showmount -e */
            case 'e':
                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case showmount:
                        cfg.format = showmount;
                        break;
                    case ping:
                        fatal("Can't specify both -c and -e!\n");
                        break;
                    case fping:
                        fatal("Can't specify both -C and -e!\n");
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -e!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -e!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -e!\n");
                        break;
                    case json:
                        fatal("Can't specify both -J and -e!\n");
                        break;
                }
                break;
            /* Etsy's StatsD format */
            case 'E':
                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case statsd:
                    case ping:
                        cfg.format = statsd;
                        break;
                    case fping:
                        fatal("Can't specify both -C and -E!\n");
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -E!\n");
                        break;
                    case showmount:
                        fatal("Can't specify both -e and -E!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -E!\n");
                        break;
                    case json:
                        fatal("Can't specify both -J and -E!\n");
                        break;
                }
                break;
            /* Graphite */
            case 'G':
                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case ping:
                    case graphite:
                        cfg.format = graphite;
                        break;
                    case fping:
                        fatal("Can't specify both -C and -G!\n");
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -G!\n");
                        break;
                    case showmount:
                        fatal("Can't specify both -e and -G!\n");
                        break;
                    case json:
                        fatal("Can't specify both -J and -G!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -G!\n");
                        break;
                }
                break;
            /* polling frequency */
            case 'H':
                /* TODO check for reasonable values */
                cfg.hertz = strtoul(optarg, NULL, 10);
                break;
            case 'J':
                /* check for conflicting format options */
                switch (cfg.format) {
                    case unset:
                    case json:
                    case ping:
                        cfg.format = json;
                        break;
                    case fping:
                        fatal("Can't specify both -J and -C, use -c instead!\n");
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -J!\n");
                        break;
                    case showmount:
                        fatal("Can't specify both -e and -J!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -J!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -J!\n");
                        break;
                }
                break;
            case 'l':
                /* Can't count and loop */
                if (cfg.count) {
                    if (cfg.format == ping || cfg.format == unixtime) {
                        fatal("Can't specify both -c and -l!\n");
                    } else if (cfg.format == fping) {
                        fatal("Can't specify both -C and -l!\n");
                    }
                } else if (cfg.format == unset) {
                    cfg.format = ping;
                } /* other formats are ok */

                cfg.loop = 1;
                break;
            /* use multiple IP addresses if found */
            /* in this case we also want to default to showing IP addresses instead of names */
            case 'm':
                cfg.multiple = 1;
                /* implies -A to use IP addresses so output isn't ambiguous */
                /* unless -d already set */
                if (cfg.dns == 0) {
                    cfg.ip = 1;
                }
                break;
            case 'q':
                cfg.quiet = 1;
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
            case 'V':
                cfg.version = strtoul(optarg, NULL, 10);
                if (cfg.version == 0 || cfg.version == ULONG_MAX || cfg.version > 3) {
                    fatal("Illegal version %lu!\n", cfg.version);
                }
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* default to JSON output to pipe to other commands */
    if (cfg.format == unset) {
        cfg.format = json;
    }

    /* calculate the sleep_time based on the frequency */
    /* check for a frequency of 1, that's a simple case */
    /* this doesn't support frequencies lower than 1Hz */
    if (cfg.hertz == 1) {
        sleep_time.tv_sec = 1;
        sleep_time.tv_nsec = 0;
    } else {
        sleep_time.tv_sec = 0;
        /* nanoseconds */
        sleep_time.tv_nsec = 1000000000 / cfg.hertz;
    }

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

            /* TODO check length against MNTPATHLEN */

            if (cfg.format == showmount) {
                fatalx(3, "Can't specify -e (exports) and a path!\n");
            }
        }

        /* make possibly multiple new targets */
        current->next = make_target(host, &hints, cfg.port, cfg.dns, cfg.ip, cfg.multiple, cfg.count, cfg.format);
        current = current->next;

        while (current) {
            if (path) {
                /* copy the path into the new target */
                current->exports = calloc(1, sizeof(struct mount_exports));
                strncpy(current->exports->path, path, MNTPATHLEN);
            /* no path given, look up exports on server */
            } else {
                /* first create an rpc connection so we can query the server for an exports list */
                current->client = create_rpc_client(current->client_sock, &hints, MOUNTPROG, cfg.version, cfg.timeout, src_ip);

                if (cfg.format == showmount) {
                    ex = get_exports(current);
                    if (cfg.ip) {
                        exports_count = print_exports(current->ip_address, ex);
                    } else {
                        exports_count = print_exports(current->name, ex);
                    }
                } else {
                    /* look up the export list on the server and create a list of exports */
                    current->exports = make_exports(current);
                }
            }

            current = current->next;
        }

        optind++;
    }

    /* listen for ctrl-c */
    quitting = 0;
    signal(SIGINT, sigint_handler);

    /* don't need to do the target loop for showmount */
    if (cfg.format == showmount) {
        targets = NULL;
    } else {
        /* skip the first dummy entry */
        targets = targets->next;

        /* calculate the maximum width for aligned printing */
        current = targets;
        while (current) {
            export = current->exports;
            while (export) {
                /* depends whether we're displaying IP addresses or not */
                if (cfg.ip) {
                    tmpwidth = strlen(current->ip_address) + strlen(export->path);
                } else {
                    tmpwidth = strlen(current->name) + strlen(export->path);
                }

                if (tmpwidth > width) {
                    width = tmpwidth;
                }

                export = export->next;
            }

            current = current->next;
        }
    }


    /* now we have a target list, loop through and query the server(s) */
    while(1) {
        /* reset to head of list */
        current = targets;

        /* grab the starting time of each loop */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &loop_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &loop_start);
#endif

        while(current) {

            if (current->client == NULL) {
                /* create an rpc connection */
                current->client = create_rpc_client(current->client_sock, &hints, MOUNTPROG, cfg.version, cfg.timeout, src_ip);
                /* mounts don't need authentication because they return a list of authentication flavours supported so leave it as default (AUTH_NONE) */
            }

            if (current->client) {
                export = current->exports;

                while (export) {
                    exports_count++;

                    /* get the current timestamp */
                    clock_gettime(CLOCK_REALTIME, &wall_clock);

                    /* the RPC call */
                    get_root_filehandle(current->client, current->name, export->path, &root, &usec);

                    export->sent++;

                    if (root.fhandle3_len) {
                        export->received++;
                        exports_ok++;

                        /* only calculate these if we're looping */
                        if (cfg.count || cfg.loop) {
                            if (usec < export->min) export->min = usec;
                            if (usec > export->max) export->max = usec;
                            /* calculate the average time */
                            export->avg = (export->avg * (export->received - 1) + usec) / export->received;

                            if (cfg.format == fping) {
                                current->results[current->sent - 1] = usec;
                            }
                        }

                        if (cfg.quiet == 0) {
                            /* whether to display IP address or hostname */
                            if (cfg.ip) {
                                display_name = current->ip_address;
                            } else {
                                display_name = current->name;
                            }

                            print_output(cfg.format, cfg.prefix, width, display_name, current->ndqf, export, root, wall_clock, usec);
                        }
                    }

                    export = export->next;
                }
            } /* TODO else if no connection */

            /* disconnect from server */
            //current->client = destroy_rpc_client(current->client);

            current = current->next;

        } /* while(current) */

        /* ctrl-c */
        if (quitting) {
            break;
        }

        /* at the end of the targets list, see if we need to loop */
        /* check the first export of the first target */
        /* TODO do we even need to store the sent number for each target or just once globally? */
        if (cfg.loop || (cfg.count && targets->exports->sent < cfg.count)) {
            /* sleep between rounds */
            /* measure how long the current round took, and subtract that from the sleep time */
            /* this keeps us on the polling frequency */
#ifdef CLOCK_MONOTONIC_RAW
            clock_gettime(CLOCK_MONOTONIC_RAW, &loop_end);
#else
            clock_gettime(CLOCK_MONOTONIC, &loop_end);
#endif
            timespecsub(&loop_end, &loop_start, &loop_elapsed);
            debug("Polling took %lld.%.9lds\n", (long long)loop_elapsed.tv_sec, loop_elapsed.tv_nsec);
            /* don't sleep if we went over the sleep_time */
            if (timespeccmp(&loop_elapsed, &sleep_time, >)) {
                debug("Slow poll, not sleeping\n");
            } else {
                timespecsub(&sleep_time, &loop_elapsed, &sleepy);
                debug("Sleeping for %lld.%.9lds\n", (long long)sleepy.tv_sec, sleepy.tv_nsec);
                nanosleep(&sleepy, NULL);
            }
        } else {
            break;
        }

    } /* while(1) */

    /* only print summary if looping */
    if (cfg.count || cfg.loop) {
        print_summary(targets, cfg.format, width, cfg.ip);
    }

    if (exports_count && exports_count == exports_ok) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
