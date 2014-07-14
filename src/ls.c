#include "nfsping.h"
#include "rpc.h"


void usage() {
    printf("Usage: nfsls [options] [filehandle...]\n\
    -a       print hidden files\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n"); 

    exit(3);
}


/* do readdirplus calls to get a full list of directory entries */
entryplus3 *do_readdirplus(CLIENT *client, fsroots_t *dir) {
    READDIRPLUS3res *res;
    entryplus3 *entry, *current, dummy;
    READDIRPLUS3args args = {
        .dir = dir->fsroot,
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
                entry = res->READDIRPLUS3res_u.resok.reply.entries;
                while (entry) {
                    current->nextentry = malloc(sizeof(entryplus3));
                    current = current->nextentry;
                    current->nextentry = NULL;
                    current = memcpy(current, entry, sizeof(entryplus3));

                    args.cookie = entry->cookie;

                    entry = entry->nextentry;
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
    char *input_fh;
    fsroots_t current;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    unsigned long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    entryplus3 *entry;
    int i;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    while ((ch = getopt(argc, argv, "ahS:T")) != -1) {
        switch(ch) {
            /* list hidden files */
            case 'a':
                all = 1;
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fprintf(stderr, "nfsping: Invalid source IP address!\n");
                    exit(3);
                }
                break; 
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            case 'h':
            default:
                usage();
        }
    }

    /* no arguments, use stdin */
    if (optind == argc) {
        /* make it the max size not the length of the current string because we'll reuse it for all filehandles */
        input_fh = malloc(sizeof(char) * FHMAX);
        fgets(input_fh, FHMAX, stdin);
    /* first argument */
    } else {
        input_fh = argv[optind];
    }
        
    while (input_fh) {
        if (parse_fh(input_fh, &current)) {
            current.client_sock->sin_family = AF_INET;
            current.client_sock->sin_port = htons(NFS_PORT);

            /* check if we can use the same client connection as the previous target */
            /* get the server address out of the client */
            if (client) {
                clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);
                if (clnt_info.sin_addr.s_addr != current.client_sock->sin_addr.s_addr) {
                    client = destroy_rpc_client(client);
                }
            }

            if (client == NULL) {
                /* connect to server */
                client = create_rpc_client(current.client_sock, &hints, NFS_PROGRAM, version, timeout, src_ip);
                client->cl_auth = authunix_create_default();
            }

            entry = do_readdirplus(client, &current);
            while (entry) {
                /* first check for hidden files */
                if (all == 0) {
                    if (entry->name[0] == '.') {
                        entry = entry->nextentry;
                        continue;
                    }
                }
                printf("%s:%s", current.host, current.path);
                /* if the path doesn't already end in /, print one now */
                if (current.path[strlen(current.path) - 1] != '/')
                    printf("/");
                /* the filename */
                printf("%s", entry->name);
                /* if it's a directory print a trailing slash */
                /* TODO this seems to be 0 sometimes */
                if (entry->name_attributes.post_op_attr_u.attributes.type == NF3DIR)
                    printf("/");
                printf(":");

                /* print the filehandle in hex */
                for (i = 0; i < entry->name_handle.post_op_fh3_u.handle.data.data_len; i++) {
                    printf("%02hhx", entry->name_handle.post_op_fh3_u.handle.data.data_val[i]);
                }
                printf("\n");

                entry = entry->nextentry;
            }
        }

        /* get the next filehandle*/
        if (optind == argc) {
            input_fh = fgets(input_fh, FHMAX, stdin);
        } else {
            optind++;
            if (optind < argc) {
                input_fh = argv[optind];
            } else {
                input_fh = NULL;
            }
        }
    }
}
