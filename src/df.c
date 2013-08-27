#include "nfsping.h"

void usage() {
    printf("Usage: nfsdf [options] [filehandle...]\n\
    -g    display sizes in gigabytes\n\
    -i    display inodes\n\
    -k    display sizes in kilobytes\n\
    -m    display sizes in megabytes\n\
    -t    display sizes in terabytes\n\
    ");

    exit(3);
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
    /* TODO PETA (and BYTE?) */
    /* FIXME only print this for prefix=0 aka human mode otherwise stuff the prefix in the header */
    /* TODO replace with enum? */
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


int print_df(int offset, int width, char *host, char *path, FSSTAT3res *fsstatres, const int prefix) {
    /* 13 is enough for a petabyte in kilobytes, plus three for the label and a trailing NUL */
    char total[16];
    char used[16];
    char avail[16];
    double capacity;

    if (fsstatres && fsstatres->status == NFS3_OK) {
        /* format results by prefix */
        prefix_print(fsstatres->FSSTAT3res_u.resok.tbytes, total, prefix);
        prefix_print(fsstatres->FSSTAT3res_u.resok.tbytes - fsstatres->FSSTAT3res_u.resok.fbytes, used, prefix);
        prefix_print(fsstatres->FSSTAT3res_u.resok.fbytes, avail, prefix);

        /* percent full */
        capacity = (1 - ((double)fsstatres->FSSTAT3res_u.resok.fbytes / fsstatres->FSSTAT3res_u.resok.tbytes)) * 100;

        printf("%-*s %*s %*s %*s %7.0f%%\n",
            offset, path, width, total, width, used, width, avail, capacity);
    } else {
        /* get_fsstat will print the rpc error */
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


void print_inodes(int offset, int width, char *host, char *path, FSSTAT3res *fsstatres) {
    double capacity;

    /* percent used */
    capacity = (1 - ((double)fsstatres->FSSTAT3res_u.resok.ffiles / fsstatres->FSSTAT3res_u.resok.tfiles)) * 100;

    printf("%-*s %*" PRIu64 " %*" PRIu64 " %*" PRIu64 " %7.0f%%\n",
        offset, path,
        width, fsstatres->FSSTAT3res_u.resok.tfiles,
        width, fsstatres->FSSTAT3res_u.resok.tfiles - fsstatres->FSSTAT3res_u.resok.ffiles,
        width, fsstatres->FSSTAT3res_u.resok.ffiles,
        capacity);
}


int main(int argc, char **argv) {
    char *error;
    int ch;
    int inodes = 0;
    int prefix = 0;
    int width  = 0;
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
    /* Print the header before sending any RPCs, this means we have to guess about the size of the results
       but it lets the user know that the program is running. Then we can print the results as they come in
       which will also give a visual indication of which filesystems are slow to respond */
    /* TODO check max line length < 80 or truncate path (or -w option for wide?) */
    if (inodes) {
        /* with 32 bit inodes you can have 2^32 per filesystem = 4294967296 = 10 digits */
        /* ZFS can have 2^48 inodes which is 15 digits */
        /* let's assume 32 bits is enough for now */
        width = 10;
    } else {
        /* The longest output for each column (up to 9 petabytes in KB) is 13 digits plus two for label*/
        /* TODO enum! */
        switch (prefix) {
            case KILO:
                /* 9PB in KB = 9.8956e12 */
                width = 13;
                break;
            case MEGA:
                /* 9PB in MB = 9.664e+9 */
                width = 10;
                break;
            case GIGA:
                /* 9PB in GB = 9.437e+6 */
                width = 7;
                break;
            case TERA:
                /* 9PB in TB = 9216 */
                width = 4;
                break;
            default:
                /* if we're using human output the column will never be longer than 4 digits plus two for label */
                width = 6;
        }
    }
    /* extra space for gap between columns */
    width++;

    /* FIXME print prefix in total column */
    printf("%-*s %*s %*s %*s capacity\n",
        maxpath, "Filesystem", width, "total", width, "used", width, "avail");
    
    /* skip the first empty struct */
    current = dummy.next;
    while (current) {
        fsstatres = get_fsstat(current->client_sock, &current->fsroot);
        if (fsstatres && fsstatres->status == NFS3_OK) {
            if (inodes)
                print_inodes(maxpath, width, current->host, current->path, fsstatres);
            else
                print_df(maxpath, width, current->host, current->path, fsstatres, prefix);
        }
        current = current->next;
    }

    return EXIT_SUCCESS;
}
