#include "nfsping.h"

bool_t xdr_dirpath (XDR *xdrs, dirpath *objp) {
    if (!xdr_string(xdrs, objp, MNTPATHLEN))
        return FALSE;
    return TRUE;
}

bool_t xdr_fhandle(XDR *xdrs, fhandle objp) {
    if (!xdr_opaque(xdrs, objp, FHSIZE))
        return FALSE;
    return TRUE;
}

bool_t xdr_fhstatus (XDR *xdrs, fhstatus *objp) {
    printf("fhs_status = %u\n", objp->fhs_status);
    if (!xdr_u_int(xdrs, &objp->fhs_status))
        return FALSE;

    if (objp->fhs_status == 0) {
        if (!xdr_fhandle(xdrs, objp->fhstatus_u.fhs_fhandle))
            return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv) {
    enum clnt_stat status;
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

    client_sock.sin_family = AF_INET;
    client_sock.sin_port = port;
    printf("mounting %s on %s\n", argv[2], argv[1]);
    intresult = inet_pton(AF_INET, argv[1], &client_sock.sin_addr);
    inet_ntop(AF_INET, &client_sock.sin_addr, &hostname, INET_ADDRSTRLEN);

    client = clntudp_create(&client_sock, MOUNTPROG, version, timeout, &sock);

    //result = mountproc_mnt_1(&argv[2], client);

    status = clnt_call(client, MOUNTPROC_MNT, (xdrproc_t) xdr_dirpath, &argv[2], (xdrproc_t) xdr_fhstatus, &result, timeout);

    if (status == RPC_SUCCESS) {
        printf("filehandle: %d\n", result.fhs_status);
    } else {
        clnt_geterr(client, &clnt_err);
        clnt_perror(client, "clnt_call");
    }
}
