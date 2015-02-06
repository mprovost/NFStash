#include "nfsping.h"

/* print a string message for each NFS status code */
u_int nfs_perror(nfsstat3 status) {
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
                fprintf(stderr, "UNKNOWN\n");
            } else {
                fprintf(stderr, "%s\n", labels_high[status - 10000]);
            }
        } else {
            if (status > NFS3ERR_REMOTE) {
                fprintf(stderr, "UNKNOWN\n");
            } else {
                /* check for missing/empty values */
                if (labels_low[status][0]) {
                    fprintf(stderr, "%s\n", labels_low[status]);
                } else {
                    fprintf(stderr, "UNKNOWN\n");
                }
            }
        }
    }

    /* TODO return an error for UNKNOWN? */
    return status;
}


/* break up a string filehandle into parts */
/* this uses strtok so it will eat the input */
nfs_fh_list *parse_fh(char *input) {
    int i;
    char *tmp;
    char *copy;
    u_int fh_len;
    struct addrinfo *addr;
    struct addrinfo hints = {
        .ai_family = AF_INET,
    };
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
    copy = strdup(input);

    /* chomp the newline */
    if (input[strlen(input) - 1] == '\n')
        input[strlen(input) - 1] = '\0';

    /* split the input string into a hostname (or IP address), path and hex filehandle */
    /* host first */
    tmp = strtok(input, ":");

    if (tmp) {
        /* DNS lookup */
        if (getaddrinfo(tmp, "nfs", &hints, &addr) == 0) {
            next->client_sock = malloc(sizeof(struct sockaddr_in));
            next->client_sock->sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            next->client_sock->sin_family = AF_INET;
            next->client_sock->sin_port = 0; /* use portmapper */

            next->host = strdup(tmp);
            /* path is just used for display */
            tmp = strtok(NULL, ":");
            if (tmp) {
                next->path = strdup(tmp);
                /* the root filehandle in hex */
                if (tmp = strtok(NULL, ":")) {
                    /* hex takes two characters for each byte */
                    fh_len = strlen(tmp) / 2;

                    if (fh_len && fh_len % 2 == 0 && fh_len <= FHSIZE3) {
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
                fprintf(stderr, "Invalid path: %s\n", copy);
                next->path = NULL;
            }
        } else {
            fprintf(stderr, "Invalid hostname: %s\n", copy);
            next->path = NULL;
        }
    } else {
        fprintf(stderr, "Invalid input: %s\n", copy);
        next->path = NULL;
    }

    /* TODO check for junk at end of input string */


    if (next->host && next->path && fh_len) {
        return next;
    } else {
        if (next->client_sock) free(next->client_sock);
        free(next);
        return NULL;
    }
}


/* print an NFS filehandle as a series of hex bytes */
/* this format has to be parsed again so take structs instead of strings to keep random data from being used as inputs */
/* TODO accept path as struct? */
/* print the IP address of the host in case there are multiple DNS results for a hostname */
int print_fh(struct sockaddr *host, char *path, fhandle3 fhandle) {
    int i;
    char ip[INET_ADDRSTRLEN];

    /* get the IP address as a string */
    inet_ntop(AF_INET, &((struct sockaddr_in *)host)->sin_addr, ip, INET_ADDRSTRLEN);

    printf("%s:%s:", ip, path);
    for (i = 0; i < fhandle.fhandle3_len; i++) {
        printf("%02hhx", fhandle.fhandle3_val[i]);
    }
    printf("\n");

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

    return ndqf;
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


/*convert a timespec to milliseconds */
unsigned long ts2ms(struct timespec ts) {
    unsigned long ms = ts.tv_sec * 1000;
    ms += ts.tv_nsec / 1000000;
    return ms;
}
