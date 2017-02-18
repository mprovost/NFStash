#include "nfsping.h"
#include "util.h"
#include "rpc.h"
#include <sys/ioctl.h> /* for checking terminal size */

/* Globals! */
extern volatile sig_atomic_t quitting;
int verbose = 0;

enum ping_outputs {
    ping_unset,     /* use as a default for getopt checks */
    ping_ping,      /* classic ping */
    ping_fping,     /* fping compatible */
    ping_unixtime,  /* ping prefixed with unix timestamp */
    ping_graphite,
    ping_statsd,
};

/* local prototypes */
static void usage(void);
static void print_interval(enum ping_outputs, char *, targets_t *, unsigned long, u_long, const struct timespec);
static void print_summary(enum ping_outputs, unsigned long, targets_t *);
static void print_result(enum ping_outputs, char *, targets_t *, unsigned long, u_long, const struct timespec, unsigned long);
static void print_lost(enum ping_outputs, char *, targets_t *, unsigned long, u_long, const struct timespec);
static void print_header(enum ping_outputs, unsigned int, unsigned long, u_long);

/* global config "object" */
static struct config {
    /* -d reverse dns lookups */
    int reverse_dns;
    /* -A print IP addresses */
    int display_ips;
    /* -Q quiet summary interval (seconds) */
    unsigned int summary_interval;
} cfg;

/* default config */
const struct config CONFIG_DEFAULT = {
    .reverse_dns      = 0,
    .display_ips      = 0,
    .summary_interval = 0,
};

/* dispatch table for null function calls, this saves us from a bunch of if statements */
/* array is [protocol number][protocol version] */
/* protocol versions should relate to the corresponding NFS protocol */
/* for example, mount protocol version 1 is used with nfs version 2, so store it at index 2 */

/* waste a bit of memory with a mostly empty array */
/* TODO have another struct to map RPC protocol numbers to lower numbers and get the array size down? */
/* rpc protocol numbers are offset by 100000, ie NFS = 100003 */
static const struct null_procs null_dispatch[][5] = {
    /* mount version 1 was used with nfs v2 */
    [MOUNTPROG - 100000]      [2] = { .proc = mountproc_null_1, .name = "mountproc_null_1", .protocol = "mountv1", .version = 1 },
    /* mount version 2 does exist but it's not that common and there's no easy way to specify it */
    [MOUNTPROG - 100000]      [3] = { .proc = mountproc_null_3, .name = "mountproc_null_3", .protocol = "mountv3", .version = 3 },
    /* nfs v4 has mounting built in */
    /* only one version of portmap protocol */
    [PMAPPROG - 100000]       [2 ... 4] = { .proc = pmapproc_null_2, .name = "pmapproc_null_2", .protocol = "portmap", .version = 2 },
    /* KLM just has one version */
    [KLM_PROG - 100000]       [2 ... 3] = { .proc = klm_null_1, .name = "klm_null_1", .protocol = "klm", .version = 1},
    /* NLM version 3 is used by NFS version 2 */
    /* NLM version 4 is used by NFS version 3 */
    /* v4 has locks integrated into the nfs protocol */
    [NLM_PROG - 100000]       [2] = { .proc = nlm_null_3, .name = "nlm_null_3", .protocol = "nlmv3", .version = 3 },
    [NLM_PROG - 100000]       [3] = { .proc = nlm4_null_4, .name = "nlm4_null_4", .protocol = "nlmv4", .version = 4 },
    /* nfs */
    [NFS_PROGRAM - 100000]    [2] = { .proc = nfsproc_null_2, .name = "nfsproc_null_2" , .protocol = "nfsv2", .version = 2 },
    [NFS_PROGRAM - 100000]    [3] = { .proc = nfsproc3_null_3, .name = "nfsproc3_null_3", .protocol = "nfsv3", .version = 3 },
    [NFS_PROGRAM - 100000]    [4] = { .proc = nfsproc4_null_4, .name = "nfsproc4_null_4", .protocol = "nfsv4", .version = 4 },
    /* nfs acl */
    [NFS_ACL_PROGRAM - 100000][2] = { .proc = aclproc2_null_2, .name = "aclproc2_null_2", .protocol = "nfs_aclv2", .version = 2 },
    [NFS_ACL_PROGRAM - 100000][3] = { .proc = aclproc3_null_3, .name = "aclproc3_null_3", .protocol = "nfs_aclv3", .version = 3 },
    /* nfs v4 has ACLs built in */
    /* NSM network status monitor, only has one version */
    /* call it "status" to match rpcinfo */
    [SM_PROG - 100000]        [2] = { .proc = sm_null_1, .name = "sm_null_1", .protocol = "status", .version = 1 },
    [SM_PROG - 100000]        [3] = { .proc = sm_null_1, .name = "sm_null_1", .protocol = "status", .version = 1 },
    /* Only one version of RQUOTA protocol. Even for NFSv4! */
    [RQUOTAPROG - 100000]     [2 ... 4] = { .proc = rquotaproc_null_1, .name = "rquotaproc_null_1", .protocol = "rquotaproc_null_1", .version = 1},
};


void usage() {
    struct timeval  timeout    = NFS_TIMEOUT;
    struct timespec wait_time  = NFS_WAIT;

    printf("Usage: nfsping [options] [targets...]\n\
    -a         check the NFS ACL protocol (default NFS)\n\
    -A         show IP addresses (default hostnames)\n\
    -c n       count of pings to send to target\n\
    -C n       same as -c, output parseable format\n\
    -d         reverse DNS lookups for targets\n\
    -D         print timestamp (unix time) before each line\n\
    -E         StatsD format output (default human readable)\n\
    -g string  prefix for Graphite/StatsD metric names (default \"nfsping\")\n\
    -G         Graphite format output (default human readable)\n\
    -h         display this help and exit\n\
    -H n       frequency in Hertz (pings per second, default %i)\n\
    -i n       interval between sending packets (in ms, default %lu)\n\
    -K         check the kernel lock manager (KLM) protocol (default NFS)\n\
    -l         loop forever\n\
    -L         check the network lock manager (NLM) protocol (default NFS)\n\
    -m         use multiple target IP addresses if found (implies -A)\n\
    -M         use the portmapper (default: NFS/ACL no, mount/NLM/NSM/rquota yes)\n\
    -n         check the mount protocol (default NFS)\n\
    -N         check the portmap protocol (default NFS)\n\
    -P n       specify port (default: NFS %i, portmap %i)\n\
    -q         quiet, only print summary\n\
    -Q n       same as -q, but show summary every n seconds\n\
    -R         don't reconnect to server every ping\n\
    -s         check the network status monitor (NSM) protocol (default NFS)\n\
    -S addr    set source address\n\
    -t n       timeout (in ms, default %lu)\n\
    -T         use TCP (default UDP)\n\
    -u         check the rquota protocol (default NFS)\n\
    -v         verbose output\n\
    -V n       specify NFS version (2/3/4, default 3)\n",
    NFS_HERTZ, ts2ms(wait_time), NFS_PORT, PMAPPORT, tv2ms(timeout));

    exit(3);
}


/* print an interval summary (-Q) for a target */
/* fping format prints to stderr for compatibility */
void print_interval(enum ping_outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, const struct timespec now) {
    struct tm *secs;
    char epoch[TIME_T_MAX_DIGITS]; /* the largest time_t seconds value, plus a terminating NUL */
    unsigned int lost = target->sent - target->received;
    /* TODO check for division by zero */
    double loss = lost / (double)target->sent * 100;

    switch (format) {
        case ping_unset:
            fatal("No format!\n");
            break;
        case ping_unixtime:
            /* get the epoch time in seconds in the local timezone */
            /* TODO should we be doing everything in UTC? */
            /* strftime needs a struct tm so use localtime to convert from time_t */
            secs = localtime(&now.tv_sec);
            strftime(epoch, sizeof(epoch), "%s", secs);
            printf("[%s.%06li] ", epoch, now.tv_nsec / 1000);
            /* fall through to ping output, this just prepends the current time */
            /*FALLTHROUGH*/
        case ping_fping:
            /* fping output with -Q looks like:
               [13:27:03]
               localhost : xmt/rcv/%loss = 3/3/0%, min/avg/max = 0.02/0.04/0.06
             */
            secs = localtime(&now.tv_sec);
            fprintf(stderr, "[%2.2d:%2.2d:%2.2d]\n",
                secs->tm_hour, secs->tm_min, secs->tm_sec);
            fprintf(stderr, "%s : xmt/rcv/%%loss = %u/%u/%.0f%%",
                target->display_name, target->sent, target->received, loss);

            /* only print times if we got any responses */
            if (target->received) {
                fprintf(stderr, ", min/avg/max = %.2f/%.2f/%.2f",
                    target->min / 1000.0, target->avg / 1000.0, target->max / 1000.0);
            }

            fprintf(stderr, "\n");
            break;
        /* our own format */
        case ping_ping:
            /* only print times if we got any responses */
            if (target->received) {
                printf("%s : %3u %7.3f %7.3f %7.3f %7.3f %7.3f ms\n",
                    target->display_name,
                    target->received,
                    hdr_min(target->interval_histogram) / 1000.0,
                    /* median not mean! */
                    hdr_value_at_percentile(target->interval_histogram, 50.0) / 1000.0,
                    hdr_value_at_percentile(target->interval_histogram, 90.0) / 1000.0,
                    hdr_value_at_percentile(target->interval_histogram, 99.0) / 1000.0,
                    hdr_max(target->interval_histogram) / 1000.0);
            }
            break;
        /* don't just print each individual result, try and emulate statsd aggregates */
        case ping_graphite:
            /* total count of requests this interval */
            printf("%s.%s.%s.count %u %li\n",
                prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                target->sent,
                now.tv_sec);

            /* lost */
            /* only print if we lost any packets this interval */
            if (lost) {
                printf("%s.%s.%s.lost %u %li\n",
                    prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                    lost,
                    now.tv_sec);
            }

            /* the histogram will be empty if there weren't any results */
            if (target->received) {
                /* max */
                printf("%s.%s.%s.usec.upper %.2f %li\n",
                    prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                    hdr_max(target->interval_histogram) / 1000.0,
                    now.tv_sec);

                /* min */
                printf("%s.%s.%s.usec.lower %.2f %li\n",
                    prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                    hdr_min(target->interval_histogram) / 1000.0,
                    now.tv_sec);

                /* sum */
                /* there's no way to get the sum of values from the histogram */

                /* mean */
                printf("%s.%s.%s.usec.mean %.2f %li\n",
                    prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                    hdr_mean(target->interval_histogram) / 1000.0,
                    now.tv_sec);

                /* sum_95th */
                /* there's no way to get the sum of values at a percentile from the histogram */

                /* 95th */
                printf("%s.%s.%s.usec.upper_95th %.2f %li\n",
                    prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                    hdr_value_at_percentile(target->interval_histogram, 95.0) / 1000.0,
                    now.tv_sec);

                /* mean_95th */
                /* there's no way to get the mean of values at a percentile from the histogram */
            }
            break;
        /* statsd output */
        /* it's unclear what to send to statsd - we're already aggregating latencies */
        /* for now just send a few counters */
        case ping_statsd:
            /* counter of pings sent */
            printf("%s.%s.%s.count:%u|c\n",
                prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                target->sent);
            /* only send lost packets if there were any */
            if (lost) {
                printf("%s.%s.%s.lost:%u|c\n",
                    prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol,
                    lost);
            }
            break;
    }
}


/* print a final summary before exiting */
/* fping format prints to stderr for compatibility */
void print_summary(enum ping_outputs format, unsigned long total_sent, targets_t *targets) {
    targets_t *current = targets;
    unsigned long i;

    while (current) {
        /* print a parseable summary string in fping-compatible format */
        if (format == ping_fping) {
            fprintf(stderr, "%s :", current->display_name);
            for (i = 0; i < total_sent; i++) {
                if (current->results[i]) {
                    fprintf(stderr, " %.2f", current->results[i] / 1000.0);
                } else {
                    fprintf(stderr, " -");
                }
            }
            fprintf(stderr, "\n");
        } else if (format == ping_ping) {
            /* blank line to separate from results */
            /* TODO only if !quiet */
            printf("\n");

            printf("%s :\n", current->display_name);
            hdr_percentiles_print(current->histogram, stdout, 5, 1000.0, CLASSIC);
        }

        current = current->next;
    }
}


/* print formatted output after each ping */
void print_result(enum ping_outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, const struct timespec now, unsigned long us) {
    double loss = (target->sent - target->received) / (double)target->sent * 100;
    char epoch[TIME_T_MAX_DIGITS]; /* the largest time_t seconds value, plus a terminating NUL */
    struct tm *secs;

    switch (format) {
        case ping_unset:
            fatal("No format!\n");
            break;
        case ping_unixtime:
            /* get the epoch time in seconds in the local timezone */
            /* strftime needs a struct tm so use localtime to convert from time_t */
            secs = localtime(&now.tv_sec);
            strftime(epoch, sizeof(epoch), "%s", secs);
            printf("[%s.%06li] ", epoch, now.tv_nsec / 1000);
            /* fall through to fping output, this just prepends the current time */
            /*FALLTHROUGH*/
        case ping_fping:
            printf("%s : [%u], %03.2f ms (%03.2f avg, %.0f%% loss)\n",
                target->display_name, target->sent - 1, us / 1000.0, target->avg / 1000.0, loss);
            break;
        case ping_ping:
            /* TODO print the hostname and (ip address) */
            printf("%s : %7.3f %7.3f %7.3f %7.3f %7.3f %7.3f ms\n",
                target->display_name,
                us / 1000.0,
                hdr_min(target->interval_histogram) / 1000.0,
                /* median not mean! */
                hdr_value_at_percentile(target->interval_histogram, 50.0) / 1000.0,
                hdr_value_at_percentile(target->interval_histogram, 90.0) / 1000.0,
                hdr_value_at_percentile(target->interval_histogram, 99.0) / 1000.0,
                hdr_max(target->interval_histogram) / 1000.0);
            break;
        case ping_graphite:
            printf("%s.%s.%s.usec %lu %li\n",
                prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol, us, now.tv_sec);
            break;
        case ping_statsd:
            printf("%s.%s.%s:%03.2f|ms\n",
                prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol, us / 1000.0);
            break;
    }

    fflush(stdout);
}


/* print missing packets for formatted output */
void print_lost(enum ping_outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, const struct timespec now) {
    /* send to stdout even though it could be considered an error, presumably these are being piped somewhere */
    /* stderr prints the errors themselves which can be discarded */
    /* todo switch (format) */
    if (format == ping_graphite || format == ping_statsd) {
        printf("%s.%s.", prefix, target->ndqf);
        printf("%s", null_dispatch[prognum_offset][version].protocol);
        if (format == ping_graphite) {
            printf(".lost 1 %li\n", now.tv_sec);
        } else if (format == ping_statsd) {
            /* send it as a counter */
            printf(".lost:1|c\n");
        }
    }
    fflush(stdout);
}


/* prints a header line */
void print_header(enum ping_outputs format, unsigned int maxhost, unsigned long prognum_offset, u_long version) {
    /* column spacing */
    int spacing = 7;

    if (format == ping_ping) {
        /* check that the biggest hostname isn't smaller than the protocol name */
        maxhost = (strlen(null_dispatch[prognum_offset][version].protocol) > maxhost) ?
            strlen(null_dispatch[prognum_offset][version].protocol) : maxhost;

        printf("%-*s   ",
            maxhost,
            null_dispatch[prognum_offset][version].protocol);

        if (cfg.summary_interval) {
            printf("rcv ");
        } else {
            printf("    RTT ");
        }

        printf("%*s %*s %*s %*s %*s\n",
            spacing, "min",
            spacing, "p50",
            spacing, "p90",
            spacing, "p99",
            spacing, "max");
    }
}


int main(int argc, char **argv) {
    void *status;
    struct timeval timeout = NFS_TIMEOUT;
    struct timespec wall_clock, call_start, call_end, call_elapsed, loop_start, loop_end, loop_elapsed, sleep_time;
    struct timespec sleepy = { 0 };
    /* polling frequency */
    unsigned long hertz = NFS_HERTZ;
    struct timespec wait_time = NFS_WAIT;
    uint16_t port = NFS_PORT;
    unsigned long prognum = NFS_PROGRAM;
    unsigned long prognum_offset = NFS_PROGRAM - 100000;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    struct rpc_err clnt_err;
    unsigned long us;
    /* default to unset so we can check in getopt */
    enum ping_outputs format = ping_unset;
    char prefix[255] = "nfsping";
    targets_t target_dummy = { 0 };
    /* pointer to head of list */
    targets_t *target = &target_dummy;
    targets_t *targets = target;
    int ch;
    unsigned long count = 0;
    unsigned long loop_count = 0;
    unsigned long total_sent = 0;
    unsigned long total_recv = 0;
    /* default to reconnecting to server each round */
    unsigned long reconnect = 1;
    /* command-line options */
    int loop = 0, quiet = 0, multiple = 0;
    /* default to NFS v3 */
    u_long version = 3;
    int first, index;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = INADDR_ANY
    };
    struct winsize winsz;
    unsigned short rows = 0; /* number of rows in terminal window */
    unsigned int maxhost = 0; /* has to be int not size_t for printf width */

    cfg = CONFIG_DEFAULT;

    /* listen for ctrl-c */
    quitting = 0;
    signal(SIGINT, sigint_handler);

    /* don't quit on (TCP) broken pipes */
    signal(SIGPIPE, SIG_IGN);

    /* no arguments passed */
    if (argc == 1)
        usage();


    while ((ch = getopt(argc, argv, "aAc:C:dDEg:GhH:i:KlLmMnNP:qQ:RsS:t:TuvV:")) != -1) {
        switch(ch) {
            /* NFS ACL protocol */
            case 'a':
                if (prognum == NFS_PROGRAM) {
                    prognum = NFS_ACL_PROGRAM;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* show IP addresses */
            case 'A':
                if (cfg.reverse_dns) {
                    /* if they've specified -A, override the implied -d from -m */
                    if (multiple) {
                        cfg.display_ips = 0;
                    } else {
                        fatal("Can't specify both -d and -A!\n");
                    }
                } else {
                    cfg.display_ips = 1;
                }
                break;
            /* number of pings per target, parseable summary */
            case 'C':
                if (loop) {
                    fatal("Can't specify both -l and -C!\n");
                } else {
                    switch (format) {
                        case ping_unset:
                        case ping_fping:
                            format = ping_fping;
                            break;
                        case ping_ping:
                            fatal("Can't specify both -c and -C!\n");
                            break;
                        case ping_unixtime:
                            fatal("Can't specify both -D and -C!\n");
                            break;
                        case ping_statsd:
                            fatal("Can't specify both -E and -C!\n");
                            break;
                        case ping_graphite:
                            fatal("Can't specify both -G and -C!\n");
                            break;
                    }
                }

                count = strtoul(optarg, NULL, 10);
                if (count == 0 || count == ULONG_MAX) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            /* number of pings per target */
            case 'c':
                if (loop) {
                    fatal("Can't specify both -l and -c!\n");
                } else {
                    switch (format) {
                        case ping_unset:
                        case ping_ping:
                            format = ping_ping;
                            break;
                        case ping_fping:
                            fatal("Can't specify both -C and -c!\n");
                            break;
                        /* other fornats are ok, don't change format though */
                        case ping_unixtime:
                        case ping_statsd:
                        case ping_graphite:
                            break;
                    }
                }

                count = strtoul(optarg, NULL, 10);
                if (count == 0 || count == ULONG_MAX) {
                    fatal("Zero count, nothing to do!\n");
                }
                break;
            /* do reverse dns lookups for IP addresses */
            case 'd':
                if (cfg.display_ips) {
                    /* check if inherited DNS lookups from -m */
                    if (multiple) {
                        /* if they've specified -d, override the implied -A from -m */
                        cfg.display_ips = 0;
                        cfg.reverse_dns = 1;
                    } else {
                        fatal("Can't specify both -A and -d!\n");
                    }
                } else {
                    cfg.reverse_dns = 1;
                }
                break;
            case 'D':
                switch (format) {
                    case ping_unset:
                    case ping_ping:
                    case ping_unixtime:
                        format = ping_unixtime;
                        break;
                    case ping_fping:
                        /* TODO this should probably work, maybe a format=fpingunix? */
                        fatal("Can't specify both -C and -D!\n");
                        break;
                    case ping_statsd:
                        fatal("Can't specify both -E and -D!\n");
                        break;
                    case ping_graphite:
                        fatal("Can't specify both -G and -D!\n");
                        break;
                }
                break;
            /* [E]tsy's StatsD output */
            case 'E':
                switch (format) {
                    case ping_unset:
                    case ping_ping:
                    case ping_statsd:
                        format = ping_statsd;
                        break;
                    case ping_fping:
                        fatal("Can't specify both -C and -E!\n");
                        break;
                    case ping_unixtime:
                        fatal("Can't specify both -D and -E!\n");
                        break;
                    case ping_graphite:
                        fatal("Can't specify both -G and -E!\n");
                        break;
                }
                break;
            /* prefix to use for graphite metrics */
            case 'g':
                strncpy(prefix, optarg, sizeof(prefix));
                break;
            /* Graphite output */
            case 'G':
                switch (format) {
                    case ping_unset:
                    case ping_ping:
                    case ping_graphite:
                        format = ping_graphite;
                        break;
                    case ping_fping:
                        fatal("Can't specify both -C and -G!\n");
                        break;
                    case ping_unixtime:
                        fatal("Can't specify both -D and -G!\n");
                        break;
                    case ping_statsd:
                        fatal("Can't specify both -E and -G!\n");
                        break;
                }
                break;
            /* polling frequency */
            case 'H':
                /* TODO check for reasonable values */
                hertz = strtoul(optarg, NULL, 10);
                break;
            /* interval between targets */
            case 'i':
                ms2ts(&wait_time, strtoul(optarg, NULL, 10));
                break;
            case 'K':
                if (prognum == NFS_PROGRAM) {
                    prognum = KLM_PROG;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* loop forever */
            case 'l':
                if (count) {
                    switch (format) {
                        case ping_ping:
                        case ping_unixtime:
                            fatal("Can't specify both -c and -l!\n");
                            break;
                        case ping_fping:
                            fatal("Can't specify both -C and -l!\n");
                            break;
                        /* shouldn't get here */
                        default:
                            fatal("Can't loop and count!\n");
                            break;
                    }
                } else {
                    loop = 1;
                }
                break;
            /* check network lock manager protocol */
            case 'L':
                if (prognum == NFS_PROGRAM) {
                    prognum = NLM_PROG;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* use multiple IP addresses if found */
            /* in this case we also want to default to showing IP addresses instead of names */
            case 'm':
                multiple = 1;
                /* implies -A to use IP addresses so output isn't ambiguous */
                /* unless -d already set */
                if (cfg.reverse_dns == 0) {
                    cfg.display_ips = 1;
                }
                break;
            /* use the portmapper */
            case 'M':
                /* portmap can't use portmapper ! */
                if (prognum == PMAPPROG) {
                    fatal("Portmap can't use portmapper!\n");
                } else {
                    /* check if it's been changed from the default by the -P option */
                    if (port == NFS_PORT) {
                        port = 0;
                    } else {
                        fatal("Can't specify both port and portmapper!\n");
                    }
                }
                break;
            /* check mount protocol */
            case 'n':
                if (prognum == NFS_PROGRAM) {
                    prognum = MOUNTPROG;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* check portmap protocol */
            case 'N':
                if (port == 0) {
                    fatal("Portmap can't use portmapper!\n");
                }
                if (prognum == NFS_PROGRAM) {
                    prognum = PMAPPROG;
                    port = PMAPPORT; /* 111 */
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* specify port */
            case 'P':
                /* check if we've set -M */
                if (port == 0) {
                    fatal("Can't specify both port and portmapper!\n");
                }
                /* leave port in host byte order */
                port = strtoul(optarg, NULL, 10);
                break;
            /* quiet, only print summary */
            /* TODO error if output also specified? */
            case 'q':
                quiet = 1;
                break;
            /* quiet with regular summaries */
            case 'Q':
                quiet = 1;
                errno = 0;
                cfg.summary_interval = strtoul(optarg, NULL, 10);
                if (errno) {
                    fatal("Invalid interval for -Q!\n");
                }
                break;
            case 'R':
                /* don't reconnect to server each round */
                reconnect = 0;
                break;
            /* check NSM */
            case 's':
                if (prognum == NFS_PROGRAM) {
                    prognum = SM_PROG;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            /* timeout */
            case 't':
                ms2tv(&timeout, strtoul(optarg, NULL, 10));
                if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
                    fatal("Zero timeout!\n");
                }
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            /* check rquota protocol */
            /* previously -Q */
            case 'u':
                if (prognum == NFS_PROGRAM) {
                    prognum = RQUOTAPROG;
                } else {
                    fatal("Only one protocol!\n");
                }
                break;
            /* verbose */
            case 'v':
                verbose = 1;
                break;
            /* specify NFS version */
            case 'V':
                version = strtoul(optarg, NULL, 10);
                /* TODO check version <5 */
                if (version == 0 || version == ULONG_MAX) {
                    fatal("Illegal version %lu\n", version);
                }
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* default */
    if (format == ping_unset) {
        format = ping_ping;
    }

    /* calculate the sleep_time based on the frequency */
    /* check for a frequency of 1, that's a simple case */
    /* this doesn't support frequencies lower than 1Hz */
    if (hertz == 1) {
        sleep_time.tv_sec = 1;
        sleep_time.tv_nsec = 0;
    } else {
        sleep_time.tv_sec = 0;
        /* nanoseconds */
        sleep_time.tv_nsec = 1000000000 / hertz;
    }

    /* calculate this once */
    prognum_offset = prognum - 100000;

    /* check null_dispatch table for supported versions for all protocols */
    if (null_dispatch[prognum_offset][version].proc == 0) {
        fatal("Illegal version %lu\n", version);
    }

    /* set the default port to portmapper if not already set */
    if (port == NFS_PORT) {
        switch (prognum) {
            /* don't change NFS port */
            case NFS_PROGRAM:
                /* do nothing */
                break;
            /* ACL usually runs on 2049 alongside NFS so leave port as default */
            case NFS_ACL_PROGRAM:
                /* do nothing */
                break;
            /* portmapper doesn't use the portmapper to find itself */
            case PMAPPROG:
                port = PMAPPORT; /* 111 */
                break;
            default:
                /* use portmapper for everything else */
                port = 0;
        }
    }

    /* output formatting doesn't make sense for the simple check */
    if (count == 0 && loop == 0 && format != ping_ping) {
        fatal("Can't specify output format without ping count!\n");
    }

    /* mark the first non-option argument */
    first = optind;

    /* check if we don't have any targets */
    if (first == argc) {
        usage();
    }

    /* process the targets from the command line */
    for (index = optind; index < argc; index++) {
        if (format == ping_fping) {
            /* allocate space for all results */
            make_target(targets, argv[index], &hints, port, cfg.reverse_dns, cfg.display_ips, multiple, timeout, NULL, count);
        } else {
            /* don't allocate space for storing results */
            make_target(targets, argv[index], &hints, port, cfg.reverse_dns, cfg.display_ips, multiple, timeout, NULL, 0);
        }
    }

    /* skip the first dummy entry */
    targets = targets->next;
    target = targets;

    while (target) {
        /* find the longest name for output spacing */
        maxhost = (strlen(target->display_name) > maxhost) ? strlen(target->display_name) : maxhost;

        /* check that the total waiting time between targets isn't going to cause us to miss our frequency (Hertz) */
        if (wait_time.tv_sec || wait_time.tv_nsec) {
            /* add up the wait interval for each target */
            timespecadd(&wait_time, &sleepy, &sleepy);

            if (timespeccmp(&sleepy, &sleep_time, >=)) {
                fatal("wait interval (-i) doesn't allow polling frequency (-H)!\n");
            }
        }

        target = target->next;
    }

    /* reset to start of target list */
    target = targets;

    /* print a header at the start */
    if ((!quiet && (count || loop)) || cfg.summary_interval) {
        print_header(format, maxhost, prognum_offset, version);
    }

    /* the main loop */
    while(target) {
        loop_count++;

        /* find the current number of rows in the terminal for printing the header once per screen */
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz);
        rows = winsz.ws_row;

        /* grab the starting time of each loop */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &loop_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &loop_start);
#endif

        while (target) {
            /* reset */
            status = NULL;

            /* check if we were disconnected (TCP) or if this is the first iteration */
            if (target->client == NULL) {
                /* try and (re)connect */
                target->client = create_rpc_client(target->client_sock, &hints, prognum, null_dispatch[prognum_offset][version].version, timeout, src_ip);
            }

            /* now see if we're connected */
            if (target->client) {
                /* grab the wall clock time for output */
                /* use the start time of the request */
                /* the call_start timer is more important so do this first so we're not measuring the time this call takes */
                clock_gettime(CLOCK_REALTIME, &wall_clock);

                /* first time marker */
                /* the MONOTONIC clocks don't record the actual time but are good for measuring elapsed time accurately */
#ifdef CLOCK_MONOTONIC_RAW
                clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
                clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif
                /* the actual ping */
                /* use a dispatch table instead of switch */
                /* doublecheck that the procedure exists, should have been checked above */
                if (null_dispatch[prognum_offset][version].proc) {
                    status = null_dispatch[prognum_offset][version].proc(NULL, target->client);
                } else {
                    fatal("Illegal version: %lu\n", version);
                }

                /* second time marker */
#ifdef CLOCK_MONOTONIC_RAW
                clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
                clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif
            } /* else not connected */

            /* count this no matter what to stop from looping in case server isn't listening */
            target->sent++;
            total_sent++;

            /* print a header for every screen of output */
            if (!quiet && (count || loop) && (total_sent % rows == 0)) {
                print_header(format, maxhost, prognum_offset, version);
            }

            /* check for success */
            if (status) {
                target->received++;
                total_recv++;

                /* check if we're looping */
                if (count || loop) {
                    /* calculate elapsed microseconds */
                    /* TODO make internal calcs in nanoseconds? */
                    timespecsub(&call_end, &call_start, &call_elapsed);
                    us = ts2us(call_elapsed);

                    if (format == ping_fping) {
                        if (us < target->min) target->min = us;
                        if (us > target->max) target->max = us;
                        /* calculate the average time */
                        target->avg = (target->avg * (target->received - 1) + us) / target->received;

                        /* store the result for the final output */
                        target->results[total_sent - 1] = us;
                    } else {
                        hdr_record_value(target->histogram, us);
                        /* TODO hdr_add()? */
                        hdr_record_value(target->interval_histogram, us);
                    }

                    if (!quiet) {
                        /* use the start time for the call since some calls may not return */
                        /* if there's an error we use print_lost() but stay consistent with timing */
                        print_result(format, prefix, target, prognum_offset, version, wall_clock, us);
                    }
                } else {
                    printf("%s is alive\n", target->display_name);
                }
            /* something went wrong */
            } else {
                /* use the start time since the call may have timed out */
                print_lost(format, prefix, target, prognum_offset, version, wall_clock);

                if (target->client) {
                    /* TODO make a string to pass to clnt_perror */
                    fprintf(stderr, "%s : ", target->display_name);
                    clnt_geterr(target->client, &clnt_err);
                    clnt_perror(target->client, null_dispatch[prognum_offset][version].name);
                    fflush(stderr);

                    /* check for broken pipes or reset connections and try and reconnect next time */
                    if (clnt_err.re_errno == EPIPE || ECONNRESET) {
                        target->client = destroy_rpc_client(target->client);
                    }
                } /* TODO else? */

                if (!count && !loop) {
                    printf("%s is dead\n", target->display_name);
                }
            }

            /* check if we should print a periodic summary */
            /* This doesn't use an actual timer, it just sees if we've sent the expected number of packets based on the configured hertz. We should be pretty close. */
            if (cfg.summary_interval && (loop_count % (hertz * cfg.summary_interval) == 0)) {
                print_interval(format, prefix, target, prognum_offset, version, wall_clock);

                /* reset target counters */
                target->sent = 0;
                target->received = 0;
                if (format == ping_fping) {
                    target->min = ULONG_MAX;
                    target->max = 0;
                    target->avg = 0;
                } else {
                    hdr_reset(target->interval_histogram);
                }
            }

            /* see if we should disconnect and reconnect */
            if (reconnect) {
                target->client = destroy_rpc_client(target->client);
            }

            target = target->next;

            /* pause between targets */
            if (target && (wait_time.tv_sec || wait_time.tv_nsec)) {
                nanosleep(&wait_time, NULL);
            }
        } /* while(target) */

        /* see if we've been signalled */
        if (quitting) {
            break;
        }

        /* at the end of the targets list, see if we need to loop */
        if ((count && loop_count < count) || loop) {
            /* sleep between rounds */
            /* measure how long the current round took, and subtract that from the sleep time */
            /* this tries to ensure that each polling round takes the same time */
#ifdef CLOCK_MONOTONIC_RAW
            clock_gettime(CLOCK_MONOTONIC_RAW, &loop_end);
#else
            clock_gettime(CLOCK_MONOTONIC, &loop_end);
#endif
            timespecsub(&loop_end, &loop_start, &loop_elapsed);
            debug("Polling took %lld.%.9lds\n", (long long)loop_elapsed.tv_sec, loop_elapsed.tv_nsec);
            /* don't sleep if we went over the sleep_time */
            if (timespeccmp(&loop_elapsed, &sleep_time, >)) {
                debug("Slow poll, not sleeping\n");
            } else {
                timespecsub(&sleep_time, &loop_elapsed, &sleepy);
                debug("Sleeping for %lld.%.9lds\n", (long long)sleepy.tv_sec, sleepy.tv_nsec);
                nanosleep(&sleepy, NULL);
            }

            /* reset to start of target list */
            target = targets;
        }
    } /* while(target) */

    fflush(stdout);

    /* only print summary if looping */
    if (count || loop) {
        print_summary(format, total_sent, targets);
    }

    /* exit with a failure if there were any missing responses */
    if (total_recv < total_sent) {
        exit(EXIT_FAILURE);
    } else {
        /* otherwise exit successfully */
        exit(EXIT_SUCCESS);
    }
}
