#include "nfsping.h"
#include "util.h"
#include "rpc.h"

/* local prototypes */
static void usage(void);
static void print_summary(targets_t *);
static void print_fping_summary(targets_t *);
static void print_output(enum outputs, char *, targets_t *, unsigned long, u_long, const struct timespec, unsigned long);
static void print_lost(enum outputs, char *, targets_t *, unsigned long, u_long, const struct timespec);


/* Globals! */
extern volatile sig_atomic_t quitting;
int verbose = 0;

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


void print_summary(targets_t *targets) {
    targets_t *target = targets;
    double loss;

    while (target) {
        loss = (target->sent - target->received) / (double)target->sent * 100;

        /* check if this is still set to the default value */
        /* that means we never saw any responses */
        if (target->min == ULONG_MAX) {
            target->min = 0;
        }

        fprintf(stderr, "%s : xmt/rcv/%%loss = %u/%u/%.0f%%",
            target->display_name, target->sent, target->received, loss);
        /* only print times if we got any responses */
        if (target->received) {
            fprintf(stderr, ", min/avg/max = %.2f/%.2f/%.2f",
                target->min / 1000.0, target->avg / 1000.0, target->max / 1000.0);
        }
        fprintf(stderr, "\n");

        target = target->next;
    }
}


/* TODO ip as parameter */
/* TODO target output spacing */
/* print a parseable summary string when finished in fping-compatible format */
void print_fping_summary(targets_t *targets) {
    targets_t *target = targets;
    unsigned long i;

    while (target) {
        fprintf(stderr, "%s :", target->display_name);
        for (i = 0; i < target->sent; i++) {
            if (target->results[i])
                fprintf(stderr, " %.2f", target->results[i] / 1000.0);
            else
                fprintf(stderr, " -");
        }
        fprintf(stderr, "\n");
        target = target->next;
    }
}


/* print formatted output after each ping */
void print_output(enum outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, const struct timespec now, unsigned long us) {
    double loss;
    char epoch[TIME_T_MAX_DIGITS]; /* the largest time_t seconds value, plus a terminating NUL */
    struct tm *secs;

    switch (format) {
        case unset:
            fatal("No format!\n");
            break;
        case unixtime:
            /* get the epoch time in seconds in the local timezone */
            /* TODO should we be doing everything in UTC? */
            /* strftime needs a struct tm so use localtime to convert from time_t */
            secs = localtime(&now.tv_sec);
            strftime(epoch, sizeof(epoch), "%s", secs);
            printf("[%s.%06li] ", epoch, now.tv_nsec / 1000);
            /* fall through to ping output, this just prepends the current time */
            /*FALLTHROUGH*/
        case ping:
        case fping:
            loss = (target->sent - target->received) / (double)target->sent * 100;
            printf("%s : [%u], %03.2f ms (%03.2f avg, %.0f%% loss)\n", target->display_name, target->sent - 1, us / 1000.0, target->avg / 1000.0, loss);
            break;
        case graphite:
            printf("%s.%s.%s.usec %lu %li\n",
                prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol, us, now.tv_sec);
            break;
        case statsd:
            printf("%s.%s.%s:%03.2f|ms\n",
                prefix, target->ndqf, null_dispatch[prognum_offset][version].protocol, us / 1000.0);
            break;
        case json:
            fatal("JSON output not implemented!\n");
            break;
        case showmount:
            fatal("showmount output not implemented!\n");
            break;
    }

    fflush(stdout);
}


/* print missing packets for formatted output */
void print_lost(enum outputs format, char *prefix, targets_t *target, unsigned long prognum_offset, u_long version, const struct timespec now) {
    /* send to stdout even though it could be considered an error, presumably these are being piped somewhere */
    /* stderr prints the errors themselves which can be discarded */
    /* todo switch (format) */
    if (format == graphite || format == statsd) {
        printf("%s.%s.", prefix, target->ndqf);
        printf("%s", null_dispatch[prognum_offset][version].protocol);
        if (format == graphite) {
            printf(".lost 1 %li\n", now.tv_sec);
        } else if (format == statsd) {
            /* send it as a counter */
            printf(".lost:1|c\n");
        }
    }
    fflush(stdout);
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
    enum outputs format = unset;
    char prefix[255] = "nfsping";
    targets_t *targets;
    targets_t *target;
    targets_t target_dummy;
    int ch;
    unsigned long count = 0;
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
                        case unset:
                        case fping:
                            format = fping;
                            break;
                        case ping:
                            fatal("Can't specify both -c and -C!\n");
                            break;
                        case unixtime:
                            fatal("Can't specify both -D and -C!\n");
                            break;
                        case statsd:
                            fatal("Can't specify both -E and -C!\n");
                            break;
                        case graphite:
                            fatal("Can't specify both -G and -C!\n");
                            break;
                        case json:
                            /* no -J option in nfsping */
                            fatal("-J not implemented!\n");
                            break;
                        case showmount:
                            /* no -e option in nfsping */
                            fatal("-e not implemented!\n");
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
                        case unset:
                        case ping:
                            format = ping;
                            break;
                        case fping:
                            fatal("Can't specify both -C and -c!\n");
                            break;
                        case json:
                            /* no -J option in nfsping */
                            fatal("-J not implemented!\n");
                            break;
                        case showmount:
                            /* no -e option in nfsping */
                            fatal("-e not implemented!\n");
                            break;
                        /* other fornats are ok, don't change format though */
                        case unixtime:
                        case statsd:
                        case graphite:
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
                    case unset:
                    case ping:
                    case unixtime:
                        format = unixtime;
                        break;
                    case fping:
                        /* TODO this should probably work, maybe a format=fpingunix? */
                        fatal("Can't specify both -C and -D!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -D!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -D!\n");
                        break;
                    case json:
                        /* no -J option in nfsping */
                        fatal("-J not implemented!\n");
                        break;
                    case showmount:
                        /* no -e option in nfsping */
                        fatal("-e not implemented!\n");
                        break;
                }
                break;
            /* [E]tsy's StatsD output */
            case 'E':
                switch (format) {
                    case unset:
                    case ping:
                    case statsd:
                        format = statsd;
                        break;
                    case fping:
                        fatal("Can't specify both -C and -E!\n");
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -E!\n");
                        break;
                    case graphite:
                        fatal("Can't specify both -G and -E!\n");
                        break;
                    case json:
                        /* no -J option in nfsping */
                        fatal("-J not implemented!\n");
                        break;
                    case showmount:
                        /* no -e option in nfsping */
                        fatal("-e not implemented!\n");
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
                    case unset:
                    case ping:
                    case graphite:
                        format = graphite;
                        break;
                    case fping:
                        fatal("Can't specify both -C and -G!\n");
                        break;
                    case unixtime:
                        fatal("Can't specify both -D and -G!\n");
                        break;
                    case statsd:
                        fatal("Can't specify both -E and -G!\n");
                        break;
                    case json:
                        /* no -J option in nfsping */
                        fatal("-J not implemented!\n");
                        break;
                    case showmount:
                        /* no -e option in nfsping */
                        fatal("-e not implemented!\n");
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
                        case ping:
                        case unixtime:
                            fatal("Can't specify both -c and -l!\n");
                            break;
                        case fping:
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
    if (format == unset) {
        format = ping;
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
    if (count == 0 && loop == 0 && format != ping) {
        fatal("Can't specify output format without ping count!\n");
    }

    /* mark the first non-option argument */
    first = optind;

    /* check if we don't have any targets */
    if (first == argc) {
        usage();
    }

    /* pointer to head of list */
    target = &target_dummy;
    targets = target;

    /* process the targets from the command line */
    for (index = optind; index < argc; index++) {
        if (format == fping) {
            target->next = make_target(argv[index], &hints, port, cfg.reverse_dns, cfg.display_ips, multiple, count);
        } else {
            /* don't allocate space for storing results */
            target->next = make_target(argv[index], &hints, port, cfg.reverse_dns, cfg.display_ips, multiple, 0);
        }
        target = target->next;
    }

    /* skip the first dummy entry */
    targets = targets->next;
    target = targets;

    /* check that the total waiting time between targets isn't going to cause us to miss our frequency (Hertz) */
    if (wait_time.tv_sec || wait_time.tv_nsec) {
        /* add up the wait interval for each target */
        while (target) {
            timespecadd(&wait_time, &sleepy, &sleepy);

            target = target->next;
        }

        if (timespeccmp(&sleepy, &sleep_time, >=)) {
            fatal("wait interval (-i) doesn't allow polling frequency (-H)!\n");
        }
    }

    /* the main loop */
    while(1) {
        target = targets;

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

            /* check for success */
            if (status) {
                target->received++;

                /* check if we're looping */
                if (count || loop) {
                    /* calculate elapsed microseconds */
                    /* TODO make internal calcs in nanoseconds? */
                    timespecsub(&call_end, &call_start, &call_elapsed);
                    us = ts2us(call_elapsed);

                    if (us < target->min) target->min = us;
                    if (us > target->max) target->max = us;
                    /* calculate the average time */
                    target->avg = (target->avg * (target->received - 1) + us) / target->received;

                    if (format == fping)
                        target->results[target->sent - 1] = us;

                    if (!quiet) {
                        /* use the start time for the call since some calls may not return */
                        /* if there's an error we use print_lost() but stay consistent with timing */
                        print_output(format, prefix, target, prognum_offset, version, wall_clock, us);
                    }
                } else {
                    printf("%s is alive\n", target->display_name);
                }
            /* something went wrong */
            } else {
                /* use the start time since the call may have timed out */
                print_lost(format, prefix, target, prognum_offset, version, wall_clock);

                if (target->client) {
                    fprintf(stderr, "%s : ", target->display_name);
                    clnt_geterr(target->client, &clnt_err);
                    clnt_perror(target->client, null_dispatch[prognum_offset][version].name);
                    fprintf(stderr, "\n");
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
        /* check the first target */
        /* TODO do we even need to store the sent number for each target or just once globally? */
        if ((count && targets->sent < count) || loop) {
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
        } else {
            break;
        }

        /* check if we should print a periodic summary */
        if (cfg.summary_interval) {
            if (targets->sent % (hertz * cfg.summary_interval) == 0) {
                print_summary(targets);
            }
        }

    } /* while(1) */

    fflush(stdout);

    /* only print summary if looping */
    if (count || loop) {
        /* these print to stderr */
        if (!quiet && (format == ping || format == fping || format == unixtime))
            fprintf(stderr, "\n");
        /* don't print summary for formatted output */
        if (format == fping)
            print_fping_summary(targets);
        else if (format == ping || format == unixtime)
            print_summary(targets);
    }

    /* loop through the targets and find any that didn't get a response
     * exit with a failure if there were any missing responses */
    target = targets;
    while (target) {
        if (target->received < target->sent)
            exit(EXIT_FAILURE);
        else
            target = target->next;
    }
    /* otherwise exit successfully */
    exit(EXIT_SUCCESS);
}
