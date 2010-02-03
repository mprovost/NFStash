#include "nfsping.h"

volatile sig_atomic_t quitting;

/* convert a timeval to microseconds */
unsigned long tv2us(struct timeval tv) {
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

/* convert a timeval to milliseconds */
unsigned long tv2ms(struct timeval tv) {
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void int_handler(int sig) {
    quitting = 1;
}

void print_summary(char *hostname, unsigned int sent, unsigned int received, unsigned int ms, float min, float max, float avg) {
    printf("--- %s nfsping statistics ---\n", hostname);
    printf("%u NULLs sent, %u received, %d lost, time %d ms\n",
        sent, received, sent - received , ms);
    printf("rtt min/avg/max/mdev = %.3f/%.3f/%.3f/0.000 ms\n",
        min, avg, max);
}

int main(int argc, char **argv) {
    CLIENT *client;
    enum clnt_stat status;
    char *error;
    struct timeval timeout, wait;
    struct timeval start, end, call_start, call_end;
    struct timespec sleep_time = { 1, 0 };
    struct sockaddr_in *client_sock;
    int sock = RPC_ANYSOCK;
    struct addrinfo hints, *addr, *current;
    int getaddr;
    unsigned int sent = 0;
    unsigned int received = 0;
    unsigned long us;
    unsigned int total;
    float ms, min, max, avg;

    quitting = 0;
    signal(SIGINT, int_handler);

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    wait.tv_sec = 1;

    addr = calloc(1, sizeof(struct addrinfo));
    addr->ai_addr = calloc(1, sizeof(struct sockaddr_in));
    client_sock = (struct sockaddr_in *) addr->ai_addr;
    client_sock->sin_family = AF_INET;
    client_sock->sin_port = htons(NFS_PORT);
    if (inet_pton(AF_INET, argv[1], &client_sock->sin_addr)) {
        client = clntudp_create(client_sock, NFS_PROGRAM, 3, wait, &sock);
    } else {
        getaddr = getaddrinfo(argv[1], "nfs", &hints, &addr);
        printf("invalid address!\n");
        exit(1);
    }

    if (client) {
        client->cl_auth = authnone_create();

        gettimeofday(&start, NULL);

        while(1) {
            if (quitting) {
                gettimeofday(&end, NULL);
                total = tv2ms(end) - tv2ms(start);
                print_summary(argv[1], sent, received, total, min, max, avg); 
                exit(EXIT_SUCCESS);
            }
            gettimeofday(&call_start, NULL);
            status = clnt_call(client, NFSPROC_NULL, (xdrproc_t) xdr_void, NULL, (xdrproc_t) xdr_void, error, timeout);
            gettimeofday(&call_end, NULL);
            sent++;

            if (status == RPC_SUCCESS) {
                received++;
                us = tv2us(call_end) - tv2us(call_start);
                ms = us / 1000.0;
                printf("%03.3f ms\n", ms);

                /* first result is a special case */
                if (received == 1) {
                    min = max = avg = ms;
                } else {
                    if (ms < min) min = ms;
                    if (ms > max) max = ms;
                    /* calculate the average time */
                    avg = (avg * (received - 1) + ms) / received;
                }
            } else {
                clnt_perror(client, argv[0]);
            }
            nanosleep(&sleep_time, NULL);
        }
    } else {
        clnt_pcreateerror(argv[0]);
    }
}


