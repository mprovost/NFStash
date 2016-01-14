#ifndef NFSPING_H
#define NFSPING_H

#define _GNU_SOURCE /* for asprintf */
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
#include <arpa/inet.h>
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
/* TODO do these need to be included in all utilities? */
#include "rpcsrc/nfs_prot.h"
#include "rpcsrc/mount.h"
#include "rpcsrc/pmap_prot.h"
#include "rpcsrc/nlm_prot.h"
#include "rpcsrc/nfsv4_prot.h"
#include "rpcsrc/nfs_acl.h"
#include "rpcsrc/sm_inter.h"
#include "rpcsrc/rquota.h"
#include "rpcsrc/klm_prot.h"

/* BSD timespec functions */
#include "src/timespec.h"

/* Parson JSON */
#include "parson/parson.h"

#define fatal(x...) do { fflush(stdout); fprintf(stderr,x); fflush(stderr); usage(); } while (0)
#define fatalx(x, y...) do { fflush(stdout); fprintf(stderr,y); fflush(stderr); exit(x); } while (0)
#define debug(x...) do { if (verbose) { fflush(stdout); fprintf(stderr,x); fflush(stderr); } } while (0)

/* struct timeval */
/* timeout for RPC requests, keep it the same (or lower) than the sleep time below */
#define NFS_TIMEOUT { 1, 0 };
/* struct timespec */
/* time to wait between pings */
#define NFS_WAIT { 0, 25000000 };
/* struct timespec */
/* polling interval */
#define NFS_SLEEP { 1, 0 };

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
    char *path;
    struct sockaddr_in *client_sock;
    CLIENT *client;
    unsigned long *results;
    unsigned int sent, received;
    unsigned long min, max;
    float avg;
    /* the JSON object for output */
    JSON_Value *json_root;
    struct targets *next;
} targets_t;

/* a singly linked list of nfs filehandles */
typedef struct nfs_fh_list {
    char *host;
    struct sockaddr_in *client_sock;
    char *path;
    nfs_fh3 nfs_fh; /* generic name so we can include v2/v4 later */
    struct nfs_fh_list *next;
} nfs_fh_list;

/* TODO capitalise? */
enum outputs {
    human,
    fping,
    graphite,
    statsd,
    unixtime
};

/* for NULL procedure function pointers */
typedef void *(*proc_null_t)(void *, CLIENT *);

struct null_procs {
    /* function pointer */
    proc_null_t proc;
    /* store the name as a string for error messages */
    char *name;
    /* protocol name for output functions */
    char *protocol;
    /* protocol version */
    u_long version;
};

#endif /* NFSPING_H */
