#include <stdio.h>
#include <stdlib.h>
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
#include <rpcsvc/nfs_prot.h>
#include <rpcsvc/mount.h>

/* struct timeval */
#define NFS_TIMEOUT { 2, 500000 };
/* struct timespec */
#define NFS_WAIT { 0, 25000000 };
/* struct timespec */
#define NFS_SLEEP { 1, 0 };

/* this isn't really a standard */
#define MOUNT_PORT 4046
/* handle 64 bit filehandles */
#define FHSIZE3 64

enum mountstat3 {
    MNT3_OK = 0,                 /* no error */
    MNT3ERR_PERM = 1,            /* Not owner */
    MNT3ERR_NOENT = 2,           /* No such file or directory */
    MNT3ERR_IO = 5,              /* I/O error */
    MNT3ERR_ACCES = 13,          /* Permission denied */
    MNT3ERR_NOTDIR = 20,         /* Not a directory */
    MNT3ERR_INVAL = 22,          /* Invalid argument */
    MNT3ERR_NAMETOOLONG = 63,    /* Filename too long */
    MNT3ERR_NOTSUPP = 10004,     /* Operation not supported */
    MNT3ERR_SERVERFAULT = 10006  /* A failure on the server */
};

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
