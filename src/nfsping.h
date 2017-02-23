#ifndef NFSPING_H
#define NFSPING_H

#define _GNU_SOURCE /* for asprintf */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
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

/* HDR Histogram */
#include "hdr/src/hdr_histogram.h"

#define fatal(x...) do { fflush(stdout); fprintf(stderr,x); fflush(stderr); usage(); } while (0)
#define fatalx(x, y...) do { fflush(stdout); fprintf(stderr,y); fflush(stderr); exit(x); } while (0)
#define debug(x...) do { if (verbose) { fflush(stdout); fprintf(stderr,x); fflush(stderr); } } while (0)

/* struct timeval */
/* timeout for RPC requests */
#define NFS_TIMEOUT { 1, 0 } /* 1 second */
/* struct timespec */
/* time to wait between targets */
#define NFS_WAIT { 0, 1000000 } /* 1ms */
/* unsigned long */
/* polling frequency */
#define NFS_HERTZ 10

/* maximum number of digits that can fit in a 64 bit time_t seconds (long long int) for use with strftime() */
/* 9223372036854775807 is LLONG_MAX, add one for a '-' (just in case!) and another for a terminating NUL */
#define TIME_T_MAX_DIGITS 21

/* max length of a uint64 cookie string */
/* ULLONG_MAX = 18446744073709551615 = 20 + NUL */
#define COOKIE_MAX 21

typedef struct targets {
    /* make the first field a pointer so that assigning to {0} works */
    CLIENT *client; /* RPC client */
    char name[NI_MAXHOST]; /* from getnameinfo() */
    char *ndqf; /* reversed name, for Graphite etc */
    char ip_address[INET_ADDRSTRLEN]; /* the IP address as a string, from inet_ntop() */
    char *display_name; /* pointer to which name string to use in output */
    /* TODO statically allocate */
    struct sockaddr_in *client_sock; /* used to store the port number and connect to the RPC client */
    /* for fping output when we need to store the individual results for the summary */
    unsigned long *results;
    unsigned int sent, received;
    unsigned long min, max;
    float avg;
    /* histogram for each interval if using -Q */
    struct hdr_histogram *interval_histogram;
    /* histogram for all results */
    struct hdr_histogram *histogram;
    /* anonymous union to store different types of target data */
    /* TODO make for ping and fping (results etc) */
    /* TODO enum to specify type */
    union {
        struct mount_exports *exports;
        struct nfs_fh_list   *filehandles;
    };

    struct targets *next;
} targets_t;

/* MOUNT protocol filesystem exports */
struct mount_exports {
    char path[MNTPATHLEN];
    /* for fping output when we need to store the individual results for the summary */
    unsigned long *results;
    unsigned long sent, received;
    unsigned long min, max;
    float avg;
    JSON_Value *json_root; /* the JSON object for output */

    struct mount_exports *next;
};

/* extend the entryplus struct returned by READDIRPLUS with a symlink name from READLINK */
typedef struct entrypluslink3 {
    union {
        struct entryplus3; /* anonymous */
        entryplus3 entryplus;
    };
    nfspath3 symlink;

    /* entryplus3 struct has a nextentry member */
    struct entrypluslink3 *next;
} entrypluslink3; 

/* a singly linked list of nfs filehandles */
typedef struct nfs_fh_list {
    char path[MNTPATHLEN];
    /* for fping output when we need to store the individual results for the summary */
    unsigned long *results;
    unsigned long sent, received;
    unsigned long min, max;
    float avg;
    /* the filehandle */
    nfs_fh3 nfs_fh; /* generic name so we can include v2/v4 later */
    /* directory entries */
    entrypluslink3 *entries;

    struct nfs_fh_list *next;
} nfs_fh_list;

/* TODO capitalise? */
enum outputs {
    unset,     /* use as a default for getopt checks */
    ping,      /* classic ping */
    fping,
    unixtime,  /* ping prefixed with unix timestamp */
    showmount, /* nfsmount */
    graphite,
    statsd,
    json
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
