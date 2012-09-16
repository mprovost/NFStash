#include "nfsping.h"

int main(int argc, char **argv) {
    mountres3 *mountres;
    struct rpc_err clnt_err;
    int sock = RPC_ANYSOCK;
    CLIENT *client;
    struct sockaddr_in client_sock;
    struct timeval timeout = NFS_TIMEOUT;
    char *error;
    char hostname[INET_ADDRSTRLEN];
    fhstatus result;
    int intresult;
    u_long version = 3;
    uint16_t port = htons(MOUNT_PORT);
    bool_t dirpath;
    int i;

    client_sock.sin_family = AF_INET;
    client_sock.sin_port = port;
    printf("mounting %s on %s\n", argv[2], argv[1]);
    intresult = inet_pton(AF_INET, argv[1], &client_sock.sin_addr);
    inet_ntop(AF_INET, &client_sock.sin_addr, &hostname, INET_ADDRSTRLEN);

    client = clntudp_create(&client_sock, MOUNTPROG, version, timeout, &sock);

    mountres = mountproc_mnt_3(&argv[2], client);

    printf("fhs_status = %u\n", mountres->fhs_status);
    if (mountres->fhs_status == MNT3_OK) {
        printf("filehandle: 0x");
        for (i = 0; i < mountres->mountres3_u.mountinfo.fhandle.fhandle3_len; i++) {
            printf("%02hhx", mountres->mountres3_u.mountinfo.fhandle.fhandle3_val[i]);
        }
        printf("\n");
    }

    clnt_destroy(client);

    client_sock.sin_port = htons(NFS_PORT);
    client = clntudp_create(&client_sock, NFS_PROGRAM, version, timeout, &sock);

}
