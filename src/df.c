#include "nfsping.h"

void usage() {
    printf("Usage: nfsdf [options] [filehandle...]\n\
    -i    display inodes\n\
    ");

    exit(3);
}

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
            fsroot->path = strdup(tmp);
            /* the root filehandle in hex */
            if (tmp = strtok(NULL, ":")) {
                /* hex takes two characters for each byte */
                fsroot_len = strlen(tmp) / 2;

                /* sanity check the hex - make sure it's even and a valid size */
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
        fsroot->next = *next;
        *next = fsroot;
        return strlen(fsroot->path);
    } else {
        free(fsroot->client_sock);
        free(fsroot);
        return 0;
    }
}


FSSTAT3res *get_fsstat(struct sockaddr_in *client_sock, nfs_fh3 *fsroot) {
    CLIENT client;
    FSSTAT3args fsstatarg;
    FSSTAT3res  *fsstatres;
    const u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    int nfs_sock = RPC_ANYSOCK;
    struct rpc_err clnt_err;

    client_sock->sin_family = AF_INET;
    client_sock->sin_port = htons(NFS_PORT);

    client = *clntudp_create(client_sock, NFS_PROGRAM, version, timeout, &nfs_sock);
    client.cl_auth = authunix_create_default();

    fsstatarg.fsroot = *fsroot;

    fsstatres = nfsproc3_fsstat_3(&fsstatarg, &client);

    if (fsstatres) {
        if (fsstatres->status != NFS3_OK) {
            clnt_geterr(&client, &clnt_err);
            if (clnt_err.re_status)
                clnt_perror(&client, "nfsproc3_fsstat_3");
            else
                nfs_perror(fsstatres->status);
        }
    } else {
        clnt_perror(&client, "nfsproc3_fsstat_3");
    }

    return fsstatres;
}


int prefix_print(size3 input, char *output, int prefix) {
    int index;
    size3 shifted;

    if (prefix == 0) {
        /* try and find the best fit, starting with terabytes and working down */
        prefix = TERA;
        while (prefix) {
            shifted = input >> prefix;
            if (shifted && shifted > 10)
                break;
            prefix -= 10;
        }
    }
   
    /* TODO check the length */
    index = snprintf(output, 13, "%" PRIu64 "", input >> prefix); 

    /* print the label */
    /* TODO PETA */
    switch (prefix) {
        case KILO:
            output[index] = 'K';
            break;
        case MEGA:
            output[index] = 'M';
            break;
        case GIGA:
            output[index] = 'G';
            break;
        case TERA:
            output[index] = 'T';
            break;
    }
    /* all of them end in B(ytes) */
    output[++index] = 'B';
    output[++index] = '\0';

    return index;
}


int print_df(int offset, char *host, char *path, FSSTAT3res *fsstatres, const int inodes, const int prefix) {
    int len;
    /* 13 is enough for a petabyte in kilobytes, plus three for the label and a trailing NUL */
    char total[16];
    char used[16];
    char avail[16];
    double capacity;
    char line[81];
    size_t max_path_len;
    char *path_spacing;

    if (fsstatres && fsstatres->status == NFS3_OK) {
        if (inodes) {
            printf("%" PRIu64 "\n", fsstatres->FSSTAT3res_u.resok.tfiles - fsstatres->FSSTAT3res_u.resok.ffiles);
        } else {
            /* format results by prefix */
            prefix_print(fsstatres->FSSTAT3res_u.resok.tbytes, total, prefix);
            prefix_print(fsstatres->FSSTAT3res_u.resok.tbytes - fsstatres->FSSTAT3res_u.resok.fbytes, used, prefix);
            prefix_print(fsstatres->FSSTAT3res_u.resok.fbytes, avail, prefix);

            /* percent full */
            capacity = (1 - ((double)fsstatres->FSSTAT3res_u.resok.fbytes / fsstatres->FSSTAT3res_u.resok.tbytes)) * 100;

            printf("%-*s %*s %*s %*s %7.0f%%\n",
                offset, path, 7, total, 7, used, 7, avail, capacity);
        }
    } else {
        /* get_fsstat will print the rpc error */
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


int main(int argc, char **argv) {
    char *error;
    int ch;
    int inodes = 0;
    int prefix = 0;
    char input_fh[FHMAX];
    fsroots_t *current, *tail, dummy;
    int maxpath = 0;
    int pathlen = 0;
    int maxhost = 0;
    FSSTAT3res *fsstatres;

    while ((ch = getopt(argc, argv, "ghikmt")) != -1) {
        switch(ch) {
        /*TODO check for multiple prefixes */
            /* display gigabytes */
            case 'g':
                prefix = GIGA;
                break;
            /* display inodes */
            case 'i':
                inodes = 1; 
                break;
            case 'k':
                /* display kilobytes */
                prefix = KILO;
                break;
            case 'm':
                /* display megabytes */
                prefix = MEGA;
                break;
            case 't':
                /* display terabytes */
                prefix = TERA;
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* first parse all of the input filehandles into a list 
     * this gives us the longest path so we can lay out the output */
    dummy.next = NULL;
    tail = &dummy;

    /* check if we don't have any command line targets */
    if (optind == argc) {
        /* use stdin */

        while (fgets(input_fh, FHMAX, stdin)) {
            /* chomp the newline */
            if (input_fh[strlen(input_fh) - 1] == '\n')
                input_fh[strlen(input_fh) - 1] = '\0';

            pathlen = parse_fh(input_fh, &(tail->next));
            tail = tail->next;

            if (pathlen > maxpath)
                maxpath = pathlen;

            if (strlen(tail->host) > maxhost)
                maxhost = strlen(tail->host);
        }
    } else {
        while (optind < argc) {
            pathlen = parse_fh(argv[optind], &(tail->next));
            tail = tail->next;

            if (pathlen > maxpath)
                maxpath = pathlen;

            if (strlen(tail->host) > maxhost)
                maxhost = strlen(tail->host);

            optind++;
        }
    }

    /* header */
    /* TODO check max line length < 80 or truncate path (or -w option for wide?) */
    /* The longest output for each column (up to 9 petabytes in KB) is 13 digits plus two for label*/
    if (prefix) {
        printf("%-*s %*s %*s %*s capacity\n",
            maxpath, "Filesystem", 16, "total", 16, "used", 16, "avail");
    /* if we're using human output the column will never be longer than 4 digits plus two for label */
    } else {
        printf("%-*s %*s %*s %*s capacity\n",
            maxpath, "Filesystem", 7, "total", 7, "used", 7, "avail");
    }

    /* skip the first empty struct */
    current = dummy.next;
    while (current) {
        fsstatres = get_fsstat(current->client_sock, &current->fsroot);
        print_df(maxpath, current->host, current->path, fsstatres, inodes, prefix);
        current = current->next;
    }

            /* reverse lookup */
            //inet_ntop(AF_INET, &(fsroot->client_sock.sin_addr), fsroot->hostname, INET_ADDRSTRLEN);


    return EXIT_SUCCESS;
}
