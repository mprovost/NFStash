#include "nfsping.h"
#include "rpc.h"


void usage() {
    printf("Usage: nfsls [options] [filehandle...]\n\
    -T   use TCP (default UDP)\n"); 

    exit(3);
}


READDIRPLUS3res *do_readdirplus(CLIENT *client, fsroots_t *dir) {
    READDIRPLUS3res *res;
    READDIRPLUS3args args = {
        .dir = dir->fsroot,
        .cookie = 0,
        .dircount = 512,
        .maxcount = 8192,
    };
    entryplus3 *entry;
    struct rpc_err clnt_err;
    int i;

    /* the RPC call */
    res = nfsproc3_readdirplus_3(&args, client);

    if (res) {
        while (res) {
            if (res->status != NFS3_OK) {
                clnt_geterr(client, &clnt_err);                                                                                                                     
                if (clnt_err.re_status)
                    clnt_perror(client, "nfsproc3_readdirplus_3");
                else
                    nfs_perror(res->status);
            } else {
                entry = res->READDIRPLUS3res_u.resok.reply.entries;
                while (entry) {
                    printf("%s:%s", dir->host, entry->name);
                    /* if it's a directory print a trailing slash */
                    if (entry->name_attributes.post_op_attr_u.attributes.type == NF3DIR)
                        printf("/");
                    printf(":");

                    /* print the filehandle in hex */
                    for (i = 0; i < entry->name_handle.post_op_fh3_u.handle.data.data_len; i++) {
                        printf("%02hhx", entry->name_handle.post_op_fh3_u.handle.data.data_val[i]);
                    }
                    printf("\n");

                    args.cookie = entry->cookie;
                    entry = entry->nextentry;
                }
                if (res->READDIRPLUS3res_u.resok.reply.eof) {
                    break;
                } else {
                    memcpy(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE);
                    res = nfsproc3_readdirplus_3(&args, client);
                }
            }
        }
    } else {
        clnt_perror(client, "nfsproc3_readdirplus_3");
    }  

    return res;
}


int main(int argc, char **argv) {
    int ch;
    char input_fh[FHMAX];
    fsroots_t *current, *tail, dummy;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    uint16_t port = htons(NFS_PORT);
    unsigned long version = 3;
    struct timeval timeout = NFS_TIMEOUT;

    struct rpc_err clnt_err;
    READDIRPLUS3res *res;

    while ((ch = getopt(argc, argv, "hT")) != -1) {
        switch(ch) {
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            case 'h':
            default:
                usage();
        }
    }

    dummy.next = NULL;
    tail = &dummy;

    while (fgets(input_fh, FHMAX, stdin)) {
        tail->next = malloc(sizeof(fsroots_t));
        tail = tail->next;
        tail->next = NULL;

        parse_fh(input_fh, tail);

        tail->client_sock->sin_family = AF_INET;
        tail->client_sock->sin_port = htons(NFS_PORT);
    }

    /* skip the first empty struct */
    current = dummy.next;

    /* loop through the list of targets */
    while (current) {
        /* check if we can use the same client connection as the previous target */
        while (client && current) {
            /* get the server address out of the client */
            clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);
            while (current) {
                if (clnt_info.sin_addr.s_addr == current->client_sock->sin_addr.s_addr) {
                    do_readdirplus(client, current);
                    current = current->next;
                } else {
                    client = destroy_rpc_client(client);
                    break;
                }
            }
        }
        if (current) {
            /* connect to server */
            client = create_rpc_client(current->client_sock, &hints, port, NFS_PROGRAM, version, timeout);
            client->cl_auth = authunix_create_default();
        }
    }
}
