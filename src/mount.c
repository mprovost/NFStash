#include "nfsping.h"

u_int mount_perror(mountstat3 fhs_status) {
    switch (fhs_status) {
        case MNT3_OK:
            fprintf(stderr, "MNT3_OK");
            break;
        case MNT3ERR_NOENT:
            fprintf(stderr, "MNT3ERR_NOENT");
            break;
        case MNT3ERR_ACCES:
            fprintf(stderr, "MNT3ERR_ACCES");
            break;
        case MNT3ERR_NOTDIR:
            fprintf(stderr, "MNT3ERR_NOTDIR");
            break;
        case MNT3ERR_INVAL:
            fprintf(stderr, "MNT3ERR_INVAL");
            break;
        case MNT3ERR_NAMETOOLONG:
            fprintf(stderr, "MNT3ERR_NAMETOOLONG");
            break;
        case MNT3ERR_NOTSUPP:
            fprintf(stderr, "MNT3ERR_NOTSUPP");
            break;
        case MNT3ERR_SERVERFAULT:
            fprintf(stderr, "MNT3ERR_SERVERFAULT");
            break;
        default:
            fprintf(stderr, "UNKNOWN");
            break;
    }
    fprintf(stderr, "\n");
    return fhs_status;
}

int main(int argc, char **argv) {
    mountres3 *mountres;
    FSSTAT3res *fsstatres;
    FSSTAT3args fsstatarg;
    struct rpc_err clnt_err;
    int sock = RPC_ANYSOCK;
    int nfs_sock = RPC_ANYSOCK;
    CLIENT *client;
    struct sockaddr_in client_sock;
    struct sockaddr_in nfs_client_sock;
    struct timeval timeout = NFS_TIMEOUT;
    char *error;
    char hostname[INET_ADDRSTRLEN];
    fhstatus result;
    int intresult;
    const u_long version = 3;
    bool_t dirpath;
    int i;
    char *host;
    char *path;

    client_sock.sin_family = AF_INET;
    host = strtok(argv[1], ":");
    path = strtok(NULL, ":");
    intresult = inet_pton(AF_INET, host, &client_sock.sin_addr);
    inet_ntop(AF_INET, &client_sock.sin_addr, &hostname, INET_ADDRSTRLEN);
    printf("mounting %s on %s\n", path, hostname); 

    /* get mount port from portmapper */
    client_sock.sin_port = htons(pmap_getport(&client_sock, MOUNTPROG, version, IPPROTO_UDP));
    if (client_sock.sin_port == 0) {
        clnt_pcreateerror("pmap_getport");
        exit(1);
    }

    if (path[0] == '/') {
        client = clntudp_create(&client_sock, MOUNTPROG, version, timeout, &sock);
        client->cl_auth = authunix_create_default();

        mountres = mountproc_mnt_3(&path, client);

        if (mountres) {
            if (mountres->fhs_status == MNT3_OK) {
                printf("filehandle: ");
                for (i = 0; i < mountres->mountres3_u.mountinfo.fhandle.fhandle3_len; i++) {
                    printf("%02hhx", mountres->mountres3_u.mountinfo.fhandle.fhandle3_val[i]);
                }
                printf("\n");
            } else {
                /* exit with the nonzero response */
                exit(mount_perror(mountres->fhs_status));
            }
        } else {
           clnt_geterr(client, &clnt_err);
           /* check for authentication errors which probably mean it needs a low port */
           if (clnt_err.re_status == RPC_AUTHERROR)
               printf("Unable to mount filesystem, consider running as root\n");

           clnt_perror(client, "mountproc_mnt_3");
           exit(1);
        }

        fsstatarg.fsroot.data.data_len = mountres->mountres3_u.mountinfo.fhandle.fhandle3_len;
        fsstatarg.fsroot.data.data_val = mountres->mountres3_u.mountinfo.fhandle.fhandle3_val;

        //clnt_destroy(client);
    } else {
        /* hex takes two characters for each byte */
        if (strlen(path) == FHSIZE * 2) {
            fsstatarg.fsroot.data.data_val = calloc(FHSIZE, sizeof(char));
            for (i = 0; i < FHSIZE; i++)
                sscanf(&path[i * 2], "%2hhx", &fsstatarg.fsroot.data.data_val[i]);

                printf("filehandle hex: ");
                for (i = 0; i < 32; i++) {
                    printf("%02hhx", fsstatarg.fsroot.data.data_val[i]);
                }
                printf("\n");
        fsstatarg.fsroot.data.data_len = FHSIZE;
        } else {
            printf("oops! %zi\n", strlen(path));
        }
    }
        
    client_sock.sin_port = htons(NFS_PORT);
    client = clntudp_create(&client_sock, NFS_PROGRAM, version, timeout, &nfs_sock);
    client->cl_auth = authunix_create_default();

    fsstatres = calloc(1, sizeof(FSSTAT3res));
    fsstatres = nfsproc3_fsstat_3(&fsstatarg, client);

    if (fsstatres->status == NFS3_OK) {
        printf("%llu bytes free\n", fsstatres->FSSTAT3res_u.resok.fbytes);
    } else {
        printf("oops\n");
    }
}
