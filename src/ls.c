#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static entryplus3 *do_readdirplus(CLIENT *, nfs_fh_list *);

/* globals */
int verbose = 0;

void usage() {
    printf("Usage: nfsls [options] [filehandle...]\n\
    -a       print hidden files\n\
    -h       display this help and exit\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n"); 

    exit(3);
}


/* do readdirplus calls to get a full list of directory entries */
entryplus3 *do_readdirplus(CLIENT *client, nfs_fh_list *dir) {
    READDIRPLUS3res *res;
    entryplus3 *res_entry, *current, dummy;
    READDIRPLUS3args args = {
        .dir = dir->nfs_fh,
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
                fprintf(stderr, "%s:%s: ", dir->host, dir->path);
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


int main(int argc, char **argv) {
    int ch;
    int all = 0;
    int count = 0;
    char   *input_fh  = NULL;
    size_t  input_len = 0;
    char *file_name;
    nfs_fh_list *current;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    unsigned long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    entryplus3 *entries;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    while ((ch = getopt(argc, argv, "ahS:Tv")) != -1) {
        switch(ch) {
            /* list hidden files */
            case 'a':
                all = 1;
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
    if (optind == argc) {
        if (getline(&input_fh, &input_len, stdin) == -1) {
            input_fh = NULL;
        }
    /* first argument */
    } else {
        input_fh = argv[optind];
    }

    while (input_fh) {

        current = parse_fh(input_fh);

        if (current) {
            /* check if we can use the same client connection as the previous target */
            /* get the server address out of the client */
            if (client) {
                clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);
                if (clnt_info.sin_addr.s_addr != current->client_sock->sin_addr.s_addr) {
                    client = destroy_rpc_client(client);
                }
            }

            if (client == NULL) {
                current->client_sock->sin_family = AF_INET;
                current->client_sock->sin_port = htons(NFS_PORT);
                /* connect to server */
                client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, version, timeout, src_ip);
                auth_destroy(client->cl_auth);
                client->cl_auth = authunix_create_default();
            }

            if (client) {
                entries = do_readdirplus(client, current);

                while (entries) {
                    count++;
                    /* first check for hidden files */
                    if (all == 0) {
                        if (entries->name[0] == '.') {
                            entries = entries->nextentry;
                            continue;
                        }
                    }
                    /* if there is no filehandle (/dev, /proc, etc) don't print */
                    /* none of the other utilities can do anything without a filehandle */
                    /* TODO unless -a ? */
                    if (entries->name_handle.post_op_fh3_u.handle.data.data_len) {
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

                        print_nfs_fh3((struct sockaddr *)current->client_sock, current->path, file_name, entries->name_handle.post_op_fh3_u.handle);

                        /* TODO free(file_name) */
                    }

                    entries = entries->nextentry;
                }
            }
            /* cleanup */
            free(current->client_sock);
            free(current);
        }

        /* get the next filehandle*/
        if (optind == argc) {
            if (getline(&input_fh, &input_len, stdin) == -1) {
                input_fh = NULL;
            }
        } else {
            optind++;
            if (optind < argc) {
                input_fh = argv[optind];
            } else {
                input_fh = NULL;
            }
        }
    }

    /* return success if we saw any entries */
    /* TODO something better! */
    if (count) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
