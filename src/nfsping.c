#include "nfsping.h"

/* convert a timeval to microseconds */
unsigned long tv2us(struct timeval tv) {
    return tv.tv_sec * 1000000 + tv.tv_usec;
}


int main(int argc, char **argv) {
    CLIENT *client;
    enum clnt_stat status;
    char *error;
    struct timeval timeout;
    struct timeval start, end;
    unsigned long us;
    float ms;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    client = clnt_create(argv[1], NFS_PROGRAM, 3, "udp");

    if (client) {
        client->cl_auth = authnone_create();

        while(1) {
            gettimeofday(&start, NULL);
            status = clnt_call(client, 0, (xdrproc_t) xdr_void, NULL, (xdrproc_t) xdr_void, error, timeout);
            gettimeofday(&end, NULL);

            if (status != RPC_SUCCESS) {
                clnt_perror(client, argv[0]);
            } else {
                us = tv2us(end) - tv2us(start);
                ms = us / 1000.0;
                printf("%03.3f ms\n", ms);
            }
            sleep(1);
        }
    } else {
        clnt_pcreateerror(argv[0]);
    }
}
