#include "util.h"
#include "nfsping.h"


/* globals */
volatile sig_atomic_t quitting = 0;


/* handle control-c */
void sigint_handler(int sig) {
    if (sig == SIGINT) {
        quitting = 1;
    }
}


/* print a string message for each NFS status code prepended with string "s" and a colon */
/* returns that original status unless there was illegal input and then -1 */
int nfs_perror(nfsstat3 status, const char *s) {
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
                fprintf(stderr, "%s: UNKNOWN\n", s);
            } else {
                fprintf(stderr, "%s: %s\n", s, labels_high[status - 10000]);
            }
        } else {
            if (status > NFS3ERR_REMOTE) {
                status = -1;
                fprintf(stderr, "%s: UNKNOWN\n", s);
            } else {
                /* check for missing/empty values */
                if (labels_low[status][0]) {
                    fprintf(stderr, "%s: %s\n", s, labels_low[status]);
                } else {
                    status = -1;
                    fprintf(stderr, "%s: UNKNOWN\n", s);
                }
            }
        }
    }

    return status;
}


/* break up a JSON filehandle into parts */
/* this uses parson */
/* port should be in host byte order (ie 2049) */
targets_t *parse_fh(targets_t *head, char *input, uint16_t port, unsigned long count, enum outputs format) {
    unsigned int i;
    const char *tmp;
    u_int fh_len = 0;
    JSON_Value  *root_value;
    JSON_Object *filehandle;
    targets_t *current = NULL;
    targets_t *retval = NULL; /* return this */
    struct nfs_fh_list *fh;
    struct sockaddr_in sock;

    /* sanity check */
    if (strlen(input) == 0) {
        fprintf(stderr, "No input!\n");
        return NULL;
    }

    root_value = json_parse_string(input);
    /* TODO if root isn't object, bail */
    filehandle = json_value_get_object(root_value);

    /* first find the IP address so we can find or create a new target */
    tmp = json_object_get_string(filehandle, "ip");

    if (tmp) {
        /* convert the IP string back into a network address */
        if (inet_pton(AF_INET, tmp, &sock.sin_addr)) {
            /* see if there's already a target for this IP, or make a new one */
            current = find_or_make_target(head, &sock, port, count, format);

            /* TODO reverse DNS lookup? */

            /* then find the hostname */
            /* don't do any DNS resolution, so the hostname is used for display only */
            tmp = json_object_get_string(filehandle, "host");

            /* TODO if there isn't a hostname, try and resolve it from the IP? */
            /* TODO compare it to the IP address from JSON input and error if they don't match? */

            if (tmp) {
                /* TODO check length against NI_MAXHOST */
                strncpy(current->name, tmp, NI_MAXHOST);

                /* reverse the hostname */
                current->ndqf = reverse_fqdn(current->name);

                /* path is just used for display */
                tmp = json_object_get_string(filehandle, "path");

                if (tmp) {
                    /* allocate a new filehandle struct */
                    fh = nfs_fh_list_new(current);

                    /* TODO check length aginst MNTPATHLEN */
                    strncpy(fh->path, tmp, MNTPATHLEN);

                    /* the root filehandle in hex */
                    tmp = json_object_get_string(filehandle, "filehandle");

                    if (tmp) {
                        /* TODO break this out into a function string_to_nfs_fh3() */

                        /* hex takes two characters for each byte */
                        fh_len = strlen(tmp) / 2;

                        /* check that it's an even number */
                        if (fh_len && fh_len <= FHSIZE3 && (strlen(tmp) % 2 == 0)) {
                            fh->nfs_fh.data.data_len = fh_len;
                            fh->nfs_fh.data.data_val = malloc(fh_len);

                            /* convert from the hex string to a byte array */
                            for (i = 0; i <= fh->nfs_fh.data.data_len; i++) {
                                sscanf(&tmp[i * 2], "%2hhx", &fh->nfs_fh.data.data_val[i]);
                            }

                            /* set the return value */
                            retval = current;
                        } else {
                            fprintf(stderr, "Invalid filehandle: %s\n", tmp);
                        }
                    } else {
                        fprintf(stderr, "No filehandle found!\n");
                    }
                } else {
                    fprintf(stderr, "No path found!\n");
                }
            } else {
                /* TODO reverse DNS lookup? */
                fprintf(stderr, "No host found!\n");
            }
        } else {
            fprintf(stderr, "Invalid IP address: %s\n", tmp);
        }
    } else {
        fprintf(stderr, "No ip found!\n");
    }

    /* cleanup if we allocated a result but failed to set a return value */
    if (retval == NULL && current) {
        /* TODO need a free_target() function! */
        if (current->client_sock) free(current->client_sock);
        free(current);
    }

    return retval;
}


/* convert an NFS filehandle to a string */
char *nfs_fh3_to_string(nfs_fh3 file_handle) {
    unsigned int i;
    /* allocate space for output string */
    /* 2 characters per byte plus NULL */
    char *str = calloc((file_handle.data.data_len * 2) + 1, sizeof(char));

    for (i = 0; i < file_handle.data.data_len; i++) {
        /* each input byte is two output bytes (in hex) */
        sprintf(&str[i * 2], "%02hhx", file_handle.data.data_val[i]);
    }

    /* terminating NUL */
    str[i * 2] = '\0';
   
    return str;
}


/* reverse a FQDN */
/* check to see if it's an IP address and if so, don't reverse it */
/* TODO const */
char* reverse_fqdn(char *fqdn) {
    int pos;
    char *copy;
    char *ndqf;
    char *tmp;
    struct sockaddr_in sock; /* for inet_pton */

    /* check for IP addresses */
    if (inet_pton(AF_INET, fqdn, &(sock.sin_addr))) {
        ndqf = fqdn;
    } else {
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
    }

    return ndqf;
}


/* allocate and initialise a target struct */
/* port should be in host byte order (ie 2049) */
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
    /* convert the port to network byte order */
    target->client_sock->sin_port = htons(port);

    return target;
}


/* take a string hostname and make a new target, or list of targets if there are multiple DNS entries */
/* return the head of the list */
/* Always store the ip address string in target->ip_address. */
/* port should be in host byte order (ie 2049) */
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
                /* copy the IP address */
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
/* FIXME needs to deep copy the data (mounts, filehandle, etc) */
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

    return new_target;
}


/* create a new empty filehandle struct at the end of the current filehandle list in a target */
/* return a pointer to the newly added filehandle */
nfs_fh_list *nfs_fh_list_new(targets_t *target) {
    /* head of the list */
    nfs_fh_list *current = target->filehandles;
    nfs_fh_list *new_fh = calloc(1, sizeof(struct nfs_fh_list));

    /* set this so that the first comparison will always be smaller */
    new_fh->min = ULONG_MAX;
   
    if (current) {
        /* find the last fh in the list */
        while (current->next) {
            current = current->next;
        }
        /* append */
        current->next = new_fh;
    } else {
        target->filehandles = new_fh;
    }

    return new_fh;
}


/* take the head of a list of targets, search for a match by IP address */
/* return NULL pointer if no match */
targets_t *find_target_by_ip(targets_t *head, struct sockaddr_in *ip_address) {
    struct targets *current = head;

    while (current) {
        /* compare the IP addresses */
        if (current->client_sock && current->client_sock->sin_addr.s_addr == ip_address->sin_addr.s_addr) {
            return current;
        } else {
            current = current->next;
        }
    }

    return NULL;
}


/*
    take a hostname argument, so have to figure out if it's an IP or resolve to possibly multiple hostnames
    once we have an IP address, check the current list to see if there's a match
    so, what if there are multiple IPs? have to do the DNS resolution outside so we can loop through and return each one
    then need separate functions (or a function that understands different formats like ping/mount/filehandle) to create specific data structures
    this can happen multiple times if we have multiple filehandles with the same hostname
    so maybe match on hostname and not IP address so we can skip doing DNS resolution each time?
    but then we'd need another level of data structure: hostname-> IPs -> filehandles
    and once the main loop is running we don't actually care about hostnames
    or just use the IP address from the JSON
    in that case we still need a different function to do the DNS resolution for nfsping and nfsmount
    shortcut that function if there's an IP address in the JSON
    maybe init_target()?
    move the data structure initialisation into that function
    two top level functions: make_target_by_ip and make_target_by_name
    have make_target append to existing target list?
    make_target_by_name just does DNS resolution and then calls make_target_by_ip?
    separate function to find target by IP in list
 */
/* port should be in host byte order (ie 2049) */
targets_t *find_or_make_target(targets_t *head, struct sockaddr_in *ip_address, uint16_t port, unsigned long count, enum outputs format) {
    targets_t *current;
    
    /* first look for a duplicate in the target list */
    current = find_target_by_ip(head, ip_address);

    /* not found */
    if (current == NULL) {
        /* make a blank one */
        current = init_target(port, count, format);

        /* copy the IP address */
        /* TODO should this be another argument to init_target()? */
        current->client_sock->sin_addr = ip_address->sin_addr;

        /* save the IP address as a string */
        inet_ntop(AF_INET, &((struct sockaddr_in *)ip_address)->sin_addr, current->ip_address, INET_ADDRSTRLEN);

        /* add it to the end of the target list */
        append_target(&head, current);
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
