#include "util.h"
#include "nfsping.h"


/* globals */
volatile sig_atomic_t quitting;


/* handle control-c */
void sigint_handler(int sig) {
    if (sig == SIGINT) {
            quitting = 1;
    }
}


/* print a string message for each NFS status code */
/* returns that original status unless there was illegal input and then -1 */
int nfs_perror(nfsstat3 status) {
    /*
     * split the nfs status codes into two arrays
     * this is ugly but otherwise it wastes too much memory
     */
    static const char *labels_low[] = {
        [NFS3ERR_PERM] =
            "NFS3ERR_PERM",
        [NFS3ERR_NOENT] =
            "NFS3ERR_NOENT",
        [NFS3ERR_IO] =
            "NFS3ERR_IO",
        [NFS3ERR_NXIO] =
            "NFS3ERR_NXIO",
        [NFS3ERR_ACCES] =
            "NFS3ERR_ACCES",
        [NFS3ERR_EXIST] =
            "NFS3ERR_EXIST",
        [NFS3ERR_XDEV] =
            "NFS3ERR_XDEV",
        [NFS3ERR_NODEV] =
            "NFS3ERR_NODEV",
        [NFS3ERR_NOTDIR] =
            "NFS3ERR_NOTDIR",
        [NFS3ERR_ISDIR] =
            "NFS3ERR_ISDIR",
        [NFS3ERR_INVAL] =
            "NFS3ERR_INVAL",
        [NFS3ERR_FBIG] =
            "NFS3ERR_FBIG",
        [NFS3ERR_NOSPC] =
            "NFS3ERR_NOSPC",
        [NFS3ERR_ROFS] =
            "NFS3ERR_ROFS",
        [NFS3ERR_MLINK] =
            "NFS3ERR_MLINK",
        [NFS3ERR_NAMETOOLONG] =
            "NFS3ERR_NAMETOOLONG",
        [NFS3ERR_NOTEMPTY] =
            "NFS3ERR_NOTEMPTY",
        [NFS3ERR_DQUOT] =
            "NFS3ERR_DQUOT",
        [NFS3ERR_STALE] =
            "NFS3ERR_STALE",
        [NFS3ERR_REMOTE] =
            "NFS3ERR_REMOTE",
    };

    /* these start at 10001 */
    static const char *labels_high[] = {
        [NFS3ERR_BADHANDLE - 10000] =
            "NFS3ERR_BADHANDLE",
        [NFS3ERR_NOT_SYNC - 10000] =
            "NFS3ERR_NOT_SYNC",
        [NFS3ERR_BAD_COOKIE - 10000] =
            "NFS3ERR_BAD_COOKIE",
        [NFS3ERR_NOTSUPP - 10000] =
            "NFS3ERR_NOTSUPP",
        [NFS3ERR_TOOSMALL - 10000] =
            "NFS3ERR_TOOSMALL",
        [NFS3ERR_SERVERFAULT - 10000] =
            "NFS3ERR_SERVERFAULT",
        [NFS3ERR_BADTYPE - 10000] =
            "NFS3ERR_BADTYPE",
        [NFS3ERR_JUKEBOX - 10000] =
            "NFS3ERR_JUKEBOX",
    };


    if (status) { /* NFS3_OK == 0 */
        if (status > 10000) {
            if (status > NFS3ERR_JUKEBOX) {
                status = -1;
                fprintf(stderr, "UNKNOWN\n");
            } else {
                fprintf(stderr, "%s\n", labels_high[status - 10000]);
            }
        } else {
            if (status > NFS3ERR_REMOTE) {
                status = -1;
                fprintf(stderr, "UNKNOWN\n");
            } else {
                /* check for missing/empty values */
                if (labels_low[status][0]) {
                    fprintf(stderr, "%s\n", labels_low[status]);
                } else {
                    status = -1;
                    fprintf(stderr, "UNKNOWN\n");
                }
            }
        }
    }

    return status;
}


/* break up a JSON filehandle into parts */
/* this uses parson */
nfs_fh_list *parse_fh(char *input) {
    unsigned int i;
    const char *tmp;
    char *copy;
    u_int fh_len = 0;
    JSON_Value  *root_value;
    JSON_Object *filehandle;
    nfs_fh_list *next;

    /* sanity check */
    if (strlen(input) == 0) {
        fprintf(stderr, "No input!\n");
        return NULL;
    }

    next = malloc(sizeof(nfs_fh_list));
    next->client_sock = NULL;
    next->next = NULL;

    /* keep a copy of the original input around for error messages */
    /* TODO do we need this with parson? Might not eat input */
    copy = strdup(input);

    root_value = json_parse_string(input);
    /* TODO if root isn't object, bail */
    filehandle = json_value_get_object(root_value);

    /* first find the hostname */
    tmp = json_object_get_string(filehandle, "host");

    if (tmp) {
        next->host = strndup(tmp, NI_MAXHOST);

        /* then find the IP */
        tmp = json_object_get_string(filehandle, "ip");

        /* TODO if there isn't an IP, try and resolve it from the hostname? */

        if (tmp) {
            /* set up the socket */
            next->client_sock = malloc(sizeof(struct sockaddr_in));
            next->client_sock->sin_family = AF_INET;
            next->client_sock->sin_port = 0; /* use portmapper */

            /* convert the IP string back into a network address */
            if (inet_pton(AF_INET, tmp, &next->client_sock->sin_addr)) {

                /* path is just used for display */
                tmp = json_object_get_string(filehandle, "path");

                if (tmp) {
                    next->path = strndup(tmp, MNTPATHLEN);

                    /* the root filehandle in hex */
                    tmp = json_object_get_string(filehandle, "filehandle");

                    if (tmp) {
                        /* hex takes two characters for each byte */
                        /* check that it's an even number */
                        if (strlen(tmp) % 2 == 0) {
                            fh_len = strlen(tmp) / 2;

                            if (fh_len && fh_len <= FHSIZE3) {
                                next->nfs_fh.data.data_len = fh_len;
                                next->nfs_fh.data.data_val = malloc(fh_len);

                                /* convert from the hex string to a byte array */
                                for (i = 0; i <= next->nfs_fh.data.data_len; i++) {
                                    sscanf(&tmp[i * 2], "%2hhx", &next->nfs_fh.data.data_val[i]);
                                }
                            } else {
                                fprintf(stderr, "Invalid filehandle: %s\n", copy);
                                next->path = NULL;
                            }
                        } else {
                            fprintf(stderr, "Invalid filehandle: %s\n", copy);
                            next->path = NULL;
                        }
                    } else {
                        fprintf(stderr, "No filehandle found!\n");
                        next->path = NULL;
                    }
                } else {
                    fprintf(stderr, "No path found!\n");
                    next->path = NULL;
                }
            } else {
                fprintf(stderr, "Invalid IP address: %s\n", copy);
                next->path = NULL;
            }
        } else {
            fprintf(stderr, "No ip found!\n");
            next->path = NULL;
        }
    } else {
        fprintf(stderr, "No host found!\n");
        next->path = NULL;
    }

    if (next->host && next->path && fh_len) {
        return next;
    } else {
        if (next->client_sock) free(next->client_sock);
        free(next);
        return NULL;
    }
}


/* convert an NFS filehandle to a string */
int nfs_fh3_to_string(char *str, nfs_fh3 file_handle) {
    unsigned int i;

    for (i = 0; i < file_handle.data.data_len; i++) {
        /* each input byte is two output bytes (in hex) */
        sprintf(&str[i * 2], "%02hhx", file_handle.data.data_val[i]);
    }

    /* terminating NUL */
    str[i * 2] = '\0';
   
    return (i * 2) + 1;
}


/* same function as above, but for NFS filehandles */
/* maybe make a generic struct like sockaddr? */
int print_nfs_fh3(struct sockaddr *host, char *path, char *file_name, nfs_fh3 file_handle) {
    unsigned int i;
    char ip[INET_ADDRSTRLEN];

    /* get the IP address as a string */
    inet_ntop(AF_INET, &((struct sockaddr_in *)host)->sin_addr, ip, INET_ADDRSTRLEN);

    printf("{ \"ip\": \"%s\", \"path\": \"%s", ip, path);
    /* if the path doesn't already end in /, print one now */
    if (path[strlen(path) - 1] != '/') {
        printf("/");
    }
    /* filename */
    printf("%s\", \"filehandle\": \"", file_name);
    /* filehandle */
    for (i = 0; i < file_handle.data.data_len; i++) {
        printf("%02hhx", file_handle.data.data_val[i]);
    }
    printf("\" }\n");

    return i;
}


/* reverse a FQDN */
char* reverse_fqdn(char *fqdn) {
    int pos;
    char *copy;
    char *ndqf;
    char *tmp;

    /* make a copy of the input so strtok doesn't clobber it */
    copy = strdup(fqdn);

    pos = strlen(copy) + 1;
    ndqf = (char *)malloc(sizeof(char *) * pos);
    if (ndqf) {
        pos--;
        ndqf[pos] = '\0';

        tmp = strtok(copy, ".");

        while (tmp) {
            pos = pos - strlen(tmp);
            memcpy(&ndqf[pos], tmp, strlen(tmp));
            tmp = strtok(NULL, ".");
            if (pos) {
                pos--;
                ndqf[pos] = '.';
            }
        }
    }

    free(copy);

    return ndqf;
}


/* allocate and initialise a target struct */
targets_t *init_target(uint16_t port, unsigned long count, enum outputs format) {
    targets_t *target;
    
    target = calloc(1, sizeof(targets_t));
    target->next = NULL;

    /* set this so that the first comparison will always be smaller */
    target->min = ULONG_MAX;

    /* allocate space for printing out a summary of all ping times at the end */
    if (format == fping) {
        target->results = calloc(count, sizeof(unsigned long));
        if (target->results == NULL) {
            fatalx(3, "Couldn't allocate memory for results!\n");
        }
    }

    target->client_sock = calloc(1, sizeof(struct sockaddr_in));
    target->client_sock->sin_family = AF_INET;
    target->client_sock->sin_port = port;

    return target;
}


/* make a new target, or list of targets if there are multiple DNS entries */
/* return the head of the list */
/* Always store the ip address string in target->ip_address. */
targets_t *make_target(char *target_name, const struct addrinfo *hints, uint16_t port, int dns, int multiple, unsigned long count, enum outputs format) {
    targets_t *target, *first;
    struct addrinfo *addr;
    int getaddr;

    /* first build a blank target */
    target = init_target(port, count, format);

    /* save the head of the list in case of multiple DNS responses */
    first = target;

    /* first try treating the hostname as an IP address */
    if (inet_pton(AF_INET, target_name, &((struct sockaddr_in *)target->client_sock)->sin_addr)) {
        /* reverse dns */
        if (dns) {
            /* don't free the old name because we're using it as the ip_address */
            getaddr = getnameinfo((struct sockaddr *)target->client_sock, sizeof(struct sockaddr_in), target_name, NI_MAXHOST, NULL, 0, NI_NAMEREQD);

            if (getaddr != 0) { /* failure! */
                /* ping and fping return 2 for name resolution failures */
                fatalx(2, "%s: %s\n", target_name, gai_strerror(getaddr));
            }
            target->ndqf = reverse_fqdn(target->name);
        } else {
            /* the IP address is the only thing we have for a name */
            strncpy(target->name, target_name, INET_ADDRSTRLEN);
            /* don't reverse IP addresses */
            target->ndqf = target->name;
        }

        /* the name is already an IP address if inet_pton succeeded */
        /* TODO should ip_address be a pointer in struct target so we don't have to make a copy in this one case? */
        strncpy(target->ip_address, target_name, INET_ADDRSTRLEN);
    /* not an IP address, do a DNS lookup */
    } else {
        /* we don't call freeaddrinfo because we keep a pointer to the sin_addr in the target */
        getaddr = getaddrinfo(target_name, "nfs", hints, &addr);
        if (getaddr == 0) { /* success! */
            /* loop through possibly multiple DNS responses */
            while (addr) {
                target->client_sock->sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;

                /* save the IP address as a string */
                inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, target->ip_address, INET_ADDRSTRLEN);

                /*
                 * always set the hostname based on the original user input
                 * if we need to display IP addresses do that in display logic and use the ip_address
                 */
                /* if reverse lookups enabled */
                if (dns) {
                    getaddr = getnameinfo((struct sockaddr *)target->client_sock, sizeof(struct sockaddr_in), target->name, NI_MAXHOST, NULL, 0, NI_NAMEREQD);
                    /* check for DNS success */
                    if (getaddr == 0) {
                        target->ndqf = reverse_fqdn(target->name);
                    /* otherwise just use the IP address */
                    /* TODO or use the original hostname? */
                    } else {
                        strncpy(target->name, target->ip_address, INET_ADDRSTRLEN);
                        /* FIXME */
                        target->ndqf = target->name;
                    }
                } else {
                    strncpy(target->name, target_name, NI_MAXHOST);
                    target->ndqf = reverse_fqdn(target->name);
                }

                /* multiple results */
                /* with glibc, this can return 127.0.0.1 twice when using "localhost" if there is an IPv6 entry in /etc/hosts
                   as documented here: https://bugzilla.redhat.com/show_bug.cgi?id=496300 */
                /* TODO detect this and skip the second duplicate entry? */
                if (addr->ai_next) {
                    if (multiple) {
                        /* make the next target */
                        target->next = init_target(port, count, format);
                        target = target->next;
                    } else {
                        /* assume that all utilities use -m to check multiple addresses */
                        fprintf(stderr, "Multiple addresses found for %s, using %s (rerun with -m for all)\n", target_name, target->ip_address);
                        break;
                    }
                }

                addr = addr->ai_next;
            }
        } else {
            fprintf(stderr, "getaddrinfo error (%s): %s\n", target->name, gai_strerror(getaddr));
            exit(2); /* ping and fping return 2 for name resolution failures */
        }
    } /* end of DNS */

    /* only return the head of the list */
    return first;
}


/* copy a target struct safely */
/* TODO const */
targets_t *copy_target(targets_t *target, unsigned long count, enum outputs format) {
    struct targets *new_target = calloc(1, sizeof(struct targets));

    /* shallow copy */
    *new_target = *target;

    /* copy the results array */
    if (format == fping) {
        new_target->results = calloc(count, sizeof(unsigned long));
        if (new_target->results == NULL) {
            fatalx(3, "Couldn't allocate memory for results!\n");
        }
        memcpy(new_target->results, target->results, count);
    }

    return new_target;
}


/* append a target (or target list) to the end of a target list */
/* return a pointer to the last element in the newly extended list */
targets_t *append_target(targets_t **head, targets_t *new_target) {
    targets_t *current = *head;

    if (current) {
        /* find the last target in the list */
        while (current->next) {
            current = current->next;
        }
        /* append */
        current->next = new_target;
    /* empty list */
    } else {
        *head = new_target;
    }

    /* go to the end of the list */
    while (current->next) {
        current = current->next;
    }

    return current;
}


/* convert a timeval to microseconds */
unsigned long tv2us(struct timeval tv) {
    return tv.tv_sec * 1000000 + tv.tv_usec;
}


/* convert a timeval to milliseconds */
unsigned long tv2ms(struct timeval tv) {
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


/* convert milliseconds to a timeval */
void ms2tv(struct timeval *tv, unsigned long ms) {
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
}


/* convert milliseconds to a timespec */
void ms2ts(struct timespec *ts, unsigned long ms) {
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000;
}


/* convert a timespec to microseconds */
unsigned long ts2us(const struct timespec ts) {
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


/*convert a timespec to milliseconds */
unsigned long ts2ms(struct timespec ts) {
    unsigned long ms = ts.tv_sec * 1000;
    ms += ts.tv_nsec / 1000000;
    return ms;
}


/* convert a timespec to nanoseconds */
unsigned long long ts2ns(const struct timespec ts) {
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}
