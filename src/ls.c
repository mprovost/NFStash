#include "nfsping.h"
#include "rpc.h"
#include "util.h"
#include <sys/stat.h> /* for file mode bits */

/* local prototypes */
static void usage(void);
static entryplus3 *do_readdirplus(CLIENT *, char *, char *, nfs_fh3);
char *lsperms(char *, ftype3, mode3);
int print_long_listing(entryplus3 *);

/* globals */
int verbose = 0;

/* global config "object" */
static struct config {
    /* ls -a */
    int all;
    /* ls -l */
    int long_listing;
    /* NFS version */
    unsigned long version;
    struct timeval timeout;
} cfg;

/* default config */
const struct config CONFIG_DEFAULT = {
    .all = 0,
    .long_listing = 0,
    .version = 3,
    .timeout = NFS_TIMEOUT,
};

void usage() {
    printf("Usage: nfsls [options]\n\
    -a       print hidden files\n\
    -h       display this help and exit\n\
    -l       print long listing\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n"); 

    exit(3);
}


/* do readdirplus calls to get a full list of directory entries */
entryplus3 *do_readdirplus(CLIENT *client, char *host, char *path, nfs_fh3 dir) {
    READDIRPLUS3res *res;
    entryplus3 *res_entry, *current, dummy;
    READDIRPLUS3args args = {
        .dir = dir,
        .cookie = 0,
        .dircount = 512,
        .maxcount = 8192,
    };
    struct rpc_err clnt_err;

    dummy.nextentry = NULL;
    current = &dummy;

    /* the RPC call */
    res = nfsproc3_readdirplus_3(&args, client);

    if (res) {
        /* loop through results, might take multiple calls for the whole directory */
        while (res) {
            if (res->status == NFS3_OK) {
                res_entry = res->READDIRPLUS3res_u.resok.reply.entries;
                while (res_entry) {
                    current->nextentry = malloc(sizeof(entryplus3));
                    current = current->nextentry;
                    current->nextentry = NULL;
                    current = memcpy(current, res_entry, sizeof(entryplus3));

                    args.cookie = res_entry->cookie;

                    res_entry = res_entry->nextentry;
                }
                if (res->READDIRPLUS3res_u.resok.reply.eof) {
                    break;
                } else {
                    /* TODO does this need a copy? */
                    memcpy(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE);
                    res = nfsproc3_readdirplus_3(&args, client);
                }
            } else {
                fprintf(stderr, "%s:%s: ", host, path);
                clnt_geterr(client, &clnt_err);
                if (clnt_err.re_status)
                    clnt_perror(client, "nfsproc3_readdirplus_3");
                else
                    nfs_perror(res->status);
                break;
            }
        }
    } else {
        clnt_perror(client, "nfsproc3_readdirplus_3");
    }  

    return dummy.nextentry;
}


/* generate a string of the file type and permissions bits of a file like ls -l */
/* based on http://stackoverflow.com/questions/10323060/printing-file-permissions-like-ls-l-using-stat2-in-c */
char *lsperms(char *bits, ftype3 type, mode3 mode) {
    const char *rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};

    switch (type) {
        case NF3REG:
            bits[0] = '-';
            break;
        case NF3DIR:
            bits[0] = 'd';
            break;
        case NF3BLK:
            bits[0] = 'b';
            break;
        case NF3CHR:
            bits[0] = 'c';
            break;
        case NF3LNK:
            bits[0] = 'l';
            break;
        case NF3SOCK:
            bits[0] = 's';
            break;
        case NF3FIFO:
            bits[0] = 'p';
            break;
        default:
            /* Unknown type -- possibly a regular file? */
            bits[0] = '?';
    }

    strcpy(&bits[1], rwx[(mode >> 6)& 7]);
    strcpy(&bits[4], rwx[(mode >> 3)& 7]);
    strcpy(&bits[7], rwx[(mode & 7)]);

    if (mode & S_ISUID)
        bits[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID)
        bits[6] = (mode & S_IXGRP) ? 's' : 'l';
    if (mode & S_ISVTX)
        bits[9] = (mode & S_IXUSR) ? 't' : 'T';

    bits[10] = '\0';

    return(bits);
};


/* ls -l */
/* TODO -F to print trailing slash for directories */
/* TODO find the longest size and justify that column */
int print_long_listing(entryplus3 *entries) {
    /* string for storing permissions bits */
    /* needs to be 11 with the file type */
    char bits[11];

    return printf("%s %lu %" PRIu64 " %s\n",
        /* permissions bits */
        lsperms(bits, entries->name_attributes.post_op_attr_u.attributes.type, entries->name_attributes.post_op_attr_u.attributes.mode),
        /* number of links */
        entries->name_attributes.post_op_attr_u.attributes.nlink,
        /* file size */
        entries->name_attributes.post_op_attr_u.attributes.size,
        entries->name);
}


int main(int argc, char **argv) {
    int ch;
    int count = 0;
    char   *input_fh  = NULL;
    size_t  input_len = 0;
    char *file_name;
    targets_t dummy = { 0 };
    targets_t *targets = &dummy;
    targets_t *current;
    struct nfs_fh_list *filehandle;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    entryplus3 *entries;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    cfg = CONFIG_DEFAULT;

    while ((ch = getopt(argc, argv, "ahlS:Tv")) != -1) {
        switch(ch) {
            /* list hidden files */
            case 'a':
                cfg.all = 1;
                break;
            /* long listing */
            case 'l':
                cfg.long_listing = 1;
                break;
            /* source ip address for packets */
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
            default:
                usage();
        }
    }

    /* no arguments, use stdin */
    while (getline(&input_fh, &input_len, stdin) != -1) {
        current = parse_fh(targets, input_fh, 0, 0, ping);
    }

    /* skip the dummy entry */
    targets = targets->next;
    current = targets;

    while (current) {
        if (current->client == NULL) {
            /* connect to server */
            current->client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, cfg.version, cfg.timeout, src_ip);
            auth_destroy(current->client->cl_auth);
            current->client->cl_auth = authunix_create_default();
        }

        if (current->client) {
            filehandle = current->filehandles;

            while (filehandle) {
                entries = do_readdirplus(current->client, current->name, filehandle->path, filehandle->nfs_fh);

                while (entries) {
                    count++;

                    /* first check for hidden files */
                    if (cfg.all == 0) {
                        if (entries->name[0] == '.') {
                            entries = entries->nextentry;
                            continue;
                        }
                    }

                    /* if there is no filehandle (/dev, /proc, etc) don't print */
                    /* none of the other utilities can do anything without a filehandle */
                    /* TODO unless -a ? */
                    if (entries->name_handle.post_op_fh3_u.handle.data.data_len) {
                        if (cfg.long_listing) {
                            print_long_listing(entries);
                        } else {
                            /* if it's a directory print a trailing slash */
                            /* TODO this seems to be 0 sometimes */
                            if (entries->name_attributes.post_op_attr_u.attributes.type == NF3DIR) {
                                /* NUL + / */
                                file_name = malloc(strlen(entries->name) + 2);
                                strncpy(file_name, entries->name, strlen(entries->name));
                                file_name[strlen(file_name)] = '/';
                            } else {
                                file_name = entries->name;
                            }

                            print_nfs_fh3(current->name, current->ip_address, filehandle->path, file_name, entries->name_handle.post_op_fh3_u.handle);
                        }

                        /* TODO free(file_name) */
                    }

                    entries = entries->nextentry;
                }

                filehandle = filehandle->next;
            } /* while (filehandle) */
        }

        current = current->next;
    } /* while (current) */


    /* return success if we saw any entries */
    /* TODO something better! */
    if (count) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
