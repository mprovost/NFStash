#include "nfsping.h"
#include "util.h"
#include "rpc.h"
#include "nagios.h"

/* globals */
int verbose = 0;

int main(int argc, char **argv) {
    void                *status = NULL;
    CLIENT              *client;
    struct sockaddr_in  sock    = {
        .sin_family     = AF_INET,
        .sin_port       = 0,
    };
    struct addrinfo     hints   = {
        .ai_family      = AF_INET,
        /* default to UDP */
        .ai_socktype    = SOCK_DGRAM,
    };
    /* source ip address for packets */
    struct sockaddr_in  src_ip = {
        .sin_family     = AF_INET,
        .sin_addr       = INADDR_ANY,
    };
    uint16_t            port    = NFS_PORT;
    unsigned long       prognum = NFS_PROGRAM;
    /* default to NFS v3 */
    unsigned long       version = 3;
    struct timeval      timeout = NFS_TIMEOUT;

    if (inet_pton(AF_INET, argv[1], &sock.sin_addr)) {

        client = create_rpc_client(&sock, &hints, prognum, version, timeout, src_ip);

        if (client) {
            status = nfsproc3_null_3(NULL, client);
        }
    }

    if (status) {
        return STATE_OK;
    } else {
        return STATE_CRITICAL;
    }
}
