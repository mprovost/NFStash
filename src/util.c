#include "nfsping.h"

u_int nfs_perror(nfsstat3 status) {
    switch(status) {
        case NFS3_OK:
            /* not an error */
            break;
        case NFS3ERR_PERM:
            fprintf(stderr, "NFS3ERR_PERM");
            break;
        case NFS3ERR_NOENT:
            fprintf(stderr, "NFS3ERR_NOENT");
            break;
        case NFS3ERR_IO:
            fprintf(stderr, "NFS3ERR_IO");
            break;
        case NFS3ERR_NXIO:
            fprintf(stderr, "NFS3ERR_NXIO");
            break;
        case NFS3ERR_ACCES:
            fprintf(stderr, "NFS3ERR_ACCES");
            break;
        case NFS3ERR_EXIST:
            fprintf(stderr, "NFS3ERR_EXIST");
            break;
        case NFS3ERR_XDEV:
            fprintf(stderr, "NFS3ERR_XDEV");
            break;
        case NFS3ERR_NODEV:
            fprintf(stderr, "NFS3ERR_NODEV");
            break;
        case NFS3ERR_NOTDIR:
            fprintf(stderr, "NFS3ERR_NOTDIR");
            break;
        case NFS3ERR_ISDIR:
            fprintf(stderr, "NFS3ERR_ISDIR");
            break;
        case NFS3ERR_INVAL:
            fprintf(stderr, "NFS3ERR_INVAL");
            break;
        case NFS3ERR_FBIG:
            fprintf(stderr, "NFS3ERR_FBIG");
            break;
        case NFS3ERR_NOSPC:
            fprintf(stderr, "NFS3ERR_NOSPC");
            break;
        case NFS3ERR_ROFS:
            fprintf(stderr, "NFS3ERR_ROFS");
            break;
        case NFS3ERR_MLINK:
            fprintf(stderr, "NFS3ERR_MLINK");
            break;
        case NFS3ERR_NAMETOOLONG:
            fprintf(stderr, "NFS3ERR_NAMETOOLONG");
            break;
        case NFS3ERR_NOTEMPTY:
            fprintf(stderr, "NFS3ERR_NOTEMPTY");
            break;
        case NFS3ERR_DQUOT:
            fprintf(stderr, "NFS3ERR_DQUOT");
            break;
        case NFS3ERR_STALE:
            fprintf(stderr, "NFS3ERR_STALE");
            break;
        case NFS3ERR_REMOTE:
            fprintf(stderr, "NFS3ERR_REMOTE");
            break;
        case NFS3ERR_BADHANDLE:
            fprintf(stderr, "NFS3ERR_BADHANDLE");
            break;
        case NFS3ERR_NOT_SYNC:
            fprintf(stderr, "NFS3ERR_NOT_SYNC");
            break;
        case NFS3ERR_BAD_COOKIE:
            fprintf(stderr, "NFS3ERR_BAD_COOKIE");
            break;
        case NFS3ERR_NOTSUPP:
            fprintf(stderr, "NFS3ERR_NOTSUPP");
            break;
        case NFS3ERR_TOOSMALL:
            fprintf(stderr, "NFS3ERR_TOOSMALL");
            break;
        case NFS3ERR_SERVERFAULT:
            fprintf(stderr, "NFS3ERR_SERVERFAULT");
            break;
        case NFS3ERR_BADTYPE:
            fprintf(stderr, "NFS3ERR_BADTYPE");
            break;
        case NFS3ERR_JUKEBOX:
            fprintf(stderr, "NFS3ERR_JUKEBOX");
            break;
    }

    if (status)
        fprintf(stderr, "\n");
    return status;
}


/* break up a string filehandle into parts */
size_t parse_fh(char *input, fsroots_t **next) {
    int i;
    fsroots_t *fsroot;
    char *tmp;
    /* keep a copy of the original input around for error messages */
    char *orig_input = input;
    u_int fsroot_len;

    fsroot = malloc(sizeof(fsroots_t));
    fsroot->client_sock = malloc(sizeof(struct sockaddr_in));

    /* split the input string into a host (IP address), path and hex filehandle */
    /* host first */
    tmp = strtok(input, ":");
    if (tmp && inet_pton(AF_INET, tmp, &((struct sockaddr_in *)fsroot->client_sock)->sin_addr)) {
        fsroot->host = tmp;
        /* path is just used for display */
        tmp = strtok(NULL, ":");
        if (tmp) {
            fsroot->path = tmp;
            /* the root filehandle in hex */
            if (tmp = strtok(NULL, ":")) {
                /* hex takes two characters for each byte */
                fsroot_len = strlen(tmp) / 2;

                if (fsroot_len && fsroot_len % 2 == 0 && fsroot_len <= FHSIZE3) {
                    fsroot->fsroot.data.data_len = fsroot_len;
                    fsroot->fsroot.data.data_val = malloc(fsroot_len);

                    /* convert from the hex string to a byte array */
                    for (i = 0; i <= fsroot->fsroot.data.data_len; i++) {
                        sscanf(&tmp[i * 2], "%2hhx", &fsroot->fsroot.data.data_val[i]);
                    }
                } else {
                    fprintf(stderr, "Invalid filehandle: %s\n", orig_input);
                    fsroot->path = NULL;
                }
            } else {
                fprintf(stderr, "Invalid fsroot: %s\n", orig_input);
                fsroot->path = NULL;
            }
        } else {
            fprintf(stderr, "Invalid path: %s\n", orig_input);
            fsroot->path = NULL;
        }
    } else {
        fprintf(stderr, "Invalid hostname: %s\n", orig_input);
        fsroot->path = NULL;
    }

    /* TODO check for junk at end of input string */


    if (fsroot->path) {
        //fsroot->next = *next;
        *next = fsroot;
        return strlen(fsroot->path);
    } else {
        free(fsroot->client_sock);
        free(fsroot);
        return 0;
    }
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
