#include <rpc/rpc.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>

#define NFS_PROGRAM 100003
#define NFS_PORT    2049
#define NFSPROC_NULL 0

typedef struct results {
    unsigned long us;
    struct results *next;
} results_t;

typedef struct targets {
    char *name;
    struct sockaddr_in *client_sock;
    CLIENT *client;
    struct results *results;
    struct results *current;
    struct targets *next;
    unsigned int sent, received;
    float min, max, avg;
} targets_t;
