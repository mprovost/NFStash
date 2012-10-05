#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
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
/* local copies */
#include "nfs_prot.h"
#include "mount.h"

/* struct timeval */
#define NFS_TIMEOUT { 2, 500000 };
/* struct timespec */
#define NFS_WAIT { 0, 25000000 };
/* struct timespec */
#define NFS_SLEEP { 1, 0 };

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
