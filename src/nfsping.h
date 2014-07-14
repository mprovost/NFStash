#ifndef NFSPING_H
#define NFSPING_H

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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <errno.h>

/* local copies */
#include "nfs_prot.h"
#include "mount.h"

/* struct timeval */
#define NFS_TIMEOUT { 2, 500000 };
/* struct timespec */
#define NFS_WAIT { 0, 25000000 };
/* struct timespec */
#define NFS_SLEEP { 1, 0 };

/* filehandle string length, including IP address, path string, colon separators and NUL */
/* xxx.xxx.xxx.xxx:/path:hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh\0 */
/* this allows for 64 byte filehandles but most are 32 byte */
#define FHMAX 16 + 1 + MNTPATHLEN + 1 + FHSIZE3 * 2 + 1

/* for shifting */
enum byte_prefix {
    NONE  = -1,
    HUMAN = 0,
    KILO  = 10,
    MEGA  = 20,
    GIGA  = 30,
    TERA  = 40,
    PETA  = 50
};

typedef struct targets {
    char *name;
    char *ndqf; /* reversed name */
    struct sockaddr_in *client_sock;
    CLIENT *client;
    unsigned long *results;
    struct targets *next;
    unsigned int sent, received;
    unsigned long min, max;
    float avg;
} targets_t;

typedef struct fsroots {
    char *host;
    struct sockaddr_in *client_sock;
    char *path;
    nfs_fh3 fsroot;
    struct fsroots *next;
} fsroots_t;

/* TODO capitalise? */
enum outputs {
    human,
    fping,
    graphite,
    statsd,
    opentsdb,
    unixtime
};

#endif /* NFSPING_H */
