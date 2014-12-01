#include "nfsping.h"
#include "rpc.h"
#include "util.h"

int verbose = 0;

void usage() {
    printf("Usage: nfslock [options] [filehandle...]\n\
    -h       display this help and exit\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n"); 

    exit(3);
}


int main(int argc, char **argv) {
    int ch;
    char *input_fh;
    fsroots_t *current;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    int version = 4;
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };
    nlm4_testres *res = NULL;
    nlm4_testargs testargs = {
        .cookie    = 0,
        .exclusive = FALSE,
    };

    while ((ch = getopt(argc, argv, "hTv")) != -1) {
        switch(ch) {
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
        /* make it the max size not the length of the current string because we'll reuse it for all filehandles */
        input_fh = malloc(sizeof(char) * FHMAX);
        fgets(input_fh, FHMAX, stdin);
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
                current->client_sock->sin_port = 0;
                /* connect to server */
                client = create_rpc_client(current->client_sock, &hints, NLM_PROG, version, timeout, src_ip);
                client->cl_auth = authunix_create_default();
            }

            testargs.alock.caller_name = "nfsping";
            testargs.alock.svid = 666;
            //memcpy(&lock->fh, NFS_FH(file_inode(fl->fl_file)), sizeof(struct nfs_fh));
            /* copy the filehandle */
            memcpy(&testargs.alock.fh, &current->fsroot, sizeof(nfs_fh3));
            testargs.alock.oh.n_bytes = "666@nfsping";
            testargs.alock.oh.n_len = 11;
            testargs.alock.l_offset = 0;
            testargs.alock.l_len = 8;

            if (client) {
                res = nlm4_test_4(&testargs, client);
            }

            if (res) {
                switch (res->stat.stat) {
                    case nlm4_failed:
                        printf("failed\n");
                        break;
                    case nlm4_granted:
                        printf("granted\n");
                        break;
                    case nlm4_denied:
                        printf("denied\n");
                        break;
                    case nlm4_denied_nolocks:
                        printf("nolocks\n");
                        break;
                    case nlm4_denied_grace_period:
                        printf("grace\n");
                        break;
                }
            }

            /* cleanup */
            free(current->client_sock);
            free(current);
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
