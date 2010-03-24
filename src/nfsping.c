#include "nfsping.h"

volatile sig_atomic_t quitting;

struct timeval start, end;
unsigned int total;
float ms, min, max, avg;
unsigned int sent = 0;
unsigned int received = 0;
char *hostname;
char *ip;

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

void print_summary() {
    if (hostname) {
        printf("--- %s nfsping statistics ---\n", hostname);
    } else {
        printf("--- %s nfsping statistics ---\n", ip);
    }
    printf("%u NULLs sent, %u received, %d lost, time %d ms\n",
        sent, received, sent - received , total);
    printf("rtt min/avg/max/mdev = %.3f/%.3f/%.3f/0.000 ms\n",
        min, avg, max);
}

int finish() {
    gettimeofday(&end, NULL);
    total = tv2ms(end) - tv2ms(start);
    print_summary();
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    CLIENT *client;
    enum clnt_stat status;
    char *error;
    struct timeval timeout, wait;
    struct timeval call_start, call_end;
    struct timespec sleep_time = { 1, 0 };
    struct sockaddr_in *client_sock;
    int sock = RPC_ANYSOCK;
    struct addrinfo hints, *addr, *current;
    int getaddr;
    unsigned long us;
    int ch;
    unsigned long count = 0;
    char *target;

    quitting = 0;
    signal(SIGINT, int_handler);

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    wait.tv_sec = 1;

    while ((ch = getopt(argc, argv, "c:")) != -1) {
        switch(ch) {
            case 'c':
                count = strtoul(optarg, NULL, 10);
        }
    }

    argc -= optind;
    argv += optind;

    target = *argv;

    addr = calloc(1, sizeof(struct addrinfo));
    addr->ai_addr = calloc(1, sizeof(struct sockaddr_in));
    client_sock = (struct sockaddr_in *) addr->ai_addr;
    client_sock->sin_family = AF_INET;
    client_sock->sin_port = htons(NFS_PORT);

    /* first try treating the hostname as an IP address */
    if (inet_pton(AF_INET, target, &client_sock->sin_addr)) {
        hostname = NULL;
        ip = target;
        printf("NFSPING %s\n", target);
    } else {
        /* if that fails, do a DNS lookup */
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM; /* change to SOCK_STREAM for TCP */
        getaddr = getaddrinfo(target, "nfs", &hints, &addr);
        if (getaddr == 0) {
            client_sock->sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            ip = calloc(1, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &client_sock->sin_addr, ip, INET_ADDRSTRLEN);
            hostname = addr->ai_canonname;
            printf("NFSPING %s (%s)\n", hostname, ip);
        } else {
            printf("%s: %s\n", target, gai_strerror(getaddr));
            exit(EXIT_FAILURE);
        }
    }

    client = clntudp_create(client_sock, NFS_PROGRAM, 3, wait, &sock);

    if (client) {
        client->cl_auth = authnone_create();

        gettimeofday(&start, NULL);

        while(1) {
            if (quitting) {
                break;
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
            if (count && sent >= count) {
                break;
            }
            nanosleep(&sleep_time, NULL);
        }
        finish();
    } else {
        clnt_pcreateerror(argv[0]);
        exit(EXIT_FAILURE);
    }
}
