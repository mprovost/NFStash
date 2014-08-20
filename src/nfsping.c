#include "nfsping.h"
#include "util.h"
#include "rpc.h"
#include "string.h"

volatile sig_atomic_t quitting;

void int_handler(int sig) {
    quitting = 1;
}

void usage() {
    struct timeval  timeout    = NFS_TIMEOUT;
    struct timespec wait_time  = NFS_WAIT;
    struct timespec sleep_time = NFS_SLEEP;

    printf("Usage: nfsping [options] [targets...]\n\
    -A         show IP addresses\n\
    -c n       count of pings to send to target\n\
    -C n       same as -c, output parseable format\n\
    -d         reverse DNS lookups for targets\n\
    -D         print timestamp (unix time) before each line\n\
    -g string  prefix for graphite metric names (default \"nfs\")\n\
    -i n       interval between targets (in ms, default %lu)\n\
    -l         loop forever\n\
    -m         use multiple target IP addresses if found\n\
    -M         use the portmapper (default: NFS no, mount yes)\n\
    -n         check the mount protocol (default NFS)\n\
    -o         output format ([G]raphite, [S]tatsd, Open[T]sdb, default human readable)\n\
    -p n       pause between pings to target (in ms, default %lu)\n\
    -P n       specify port (default: NFS %i)\n\
    -q         quiet, only print summary\n\
    -r n       reconnect to server every n pings\n\
    -S addr    set source address\n\
    -t n       timeout (in ms, default %lu)\n\
    -T         use TCP (default UDP)\n\
    -V n       specify NFS version (2 or 3, default 3)\n",
    ts2ms(wait_time), ts2ms(sleep_time), NFS_PORT, tv2ms(timeout));

    exit(3);
}

void print_summary(targets_t targets) {
    targets_t *target = &targets;
    double loss;

    while (target) {
        loss = (target->sent - target->received) / (double)target->sent * 100;
        fprintf(stderr, "%s : xmt/rcv/%%loss = %u/%u/%.0f%%, min/avg/max = %.2f/%.2f/%.2f\n",
            target->name, target->sent, target->received, loss, target->min / 1000.0, target->avg / 1000.0, target->max / 1000.0);
        target = target->next;
    }
}

/* TODO target output spacing */
/* print a parseable summary string when finished in fping-compatible format */
void print_fping_summary(targets_t targets) {
    targets_t *target = &targets;
    unsigned long i;

    while (target) {
        fprintf(stderr, "%s :", target->name);
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
void print_output(enum outputs format, char *prefix, targets_t *target, unsigned long prognum, struct timeval now, unsigned long us) {
    double loss;

    if (format == unixtime) {
        /* FIXME these casts to long aren't great */
        printf("[%ld.%06ld] ", (long)now.tv_sec, (long)now.tv_usec);
    }

    if (format == human || format == fping || format == unixtime) {
        loss = (target->sent - target->received) / (double)target->sent * 100;
        printf("%s : [%u], %03.2f ms (%03.2f avg, %.0f%% loss)\n", target->name, target->sent - 1, us / 1000.0, target->avg / 1000.0, loss);
    } else if (format == graphite) {
        if (prognum == MOUNTPROG) {
            printf("%s.%s.mount.usec %lu %li\n", prefix, target->ndqf, us, now.tv_sec);
        } else {
            printf("%s.%s.ping.usec %lu %li\n", prefix, target->ndqf, us, now.tv_sec);
        }
    } else if (format == statsd) {
        /* statsd only takes milliseconds */
        if (prognum == MOUNTPROG) {
            printf("nfsping.%s.mount:%03.2f|ms\n", target->ndqf, us / 1000.0 );
        } else {
            printf("nfsping.%s.ping:%03.2f|ms\n", target->ndqf, us / 1000.0 );
        }
    }
}


/* print missing packets for formatted output */
void print_lost(enum outputs format, char *prefix, targets_t *target, unsigned long prognum, struct timeval now) {
    /* send to stdout even though it could be considered an error, presumably these are being piped somewhere */
    /* stderr prints the errors themselves which can be discarded */
    if (format == graphite || format == statsd) {
        printf("%s.%s.", prefix, target->name);
        if (prognum == MOUNTPROG) {
            printf("mount");
        } else {
            printf("ping");
        }
        if (format == graphite) {
            printf(".lost 1 %li\n", now.tv_sec);
        } else if (format == statsd) {
            /* send it as a counter */
            printf(".lost:1|c\n");
        }
    }
}


/* make a new target */
targets_t *make_target(char *name, unsigned long prognum, uint16_t port, enum outputs format) {
    targets_t *target;

    target = calloc(1, sizeof(targets_t));
    target->next = NULL;
    target->name = name;
    if (format == graphite)
        target->ndqf = reverse_fqdn(target->name);

    target->client_sock = calloc(1, sizeof(struct sockaddr_in));
    target->client_sock->sin_family = AF_INET;

    /* if we're checking mount instead of nfs, default to using the portmapper */
    if (prognum == MOUNTPROG && port == htons(NFS_PORT)) {
        target->client_sock->sin_port = 0;
    } else {
        target->client_sock->sin_port = port;
    }

    /* set this so that the first comparison will always be smaller */
    target->min = ULONG_MAX;

    return target;
}


int main(int argc, char **argv) {
    void *status;
    char *error;
    struct timeval timeout = NFS_TIMEOUT;
    struct timeval call_start, call_end;
    struct timespec sleep_time = NFS_SLEEP;
    struct timespec wait_time = NFS_WAIT;
    uint16_t port = htons(NFS_PORT);
    unsigned long prognum = NFS_PROGRAM;
    struct addrinfo hints = {0}, *addr;
    struct rpc_err clnt_err;
    int getaddr;
    unsigned long us;
    enum outputs format = human;
    char prefix[255] = "nfs";
    targets_t *targets;
    targets_t *target;
    targets_t target_dummy;
    int ch;
    unsigned long count = 0;
    unsigned long reconnect = 0;
    /* command-line options */
    int dns = 0, loop = 0, ip = 0, quiet = 0, multiple = 0;
    /* default to NFS v3 */
    u_long version = 3;
    int first, index;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    /* listen for ctrl-c */
    quitting = 0;
    signal(SIGINT, int_handler);

    hints.ai_family = AF_INET;
    /* default to UDP */
    hints.ai_socktype = SOCK_DGRAM;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "Ac:C:dDg:hi:lmnMo:p:P:qr:S:t:TV:")) != -1) {
        switch(ch) {
            /* show IP addresses */
            case 'A':
                ip = 1;
                break;
            /* number of pings per target, parseable summary */
            case 'C':
                if (format == human) {
                    format = fping;
                } else {
                    fprintf(stderr, "nfsping: Can't specify both -C and -o!\n");
                    usage();
                }
                /* fall through to regular count */
            /* number of pings per target */
            case 'c':
                count = strtoul(optarg, NULL, 10);
                if (count == 0) {
                    fprintf(stderr, "nfsping: zero count, nothing to do!\n");
                    exit(3);
                }
                break;
            /* do reverse dns lookups for IP addresses */
            case 'd':
                dns = 1;
                break;
            case 'D':
                if (format == human) {
                    format = unixtime;
                /* TODO this should probably work, maybe a format=fpingunix? */
                } else if (format == fping) {
                    fprintf(stderr, "nfsping: Can't specify both -C and -D!\n");
                    usage();
                } else {
                    fprintf(stderr, "nfsping: Can't specify both -D and -o!\n");
                    usage();
                }
                break;
            /* prefix to use for graphite metrics */
            case 'g':
                strncpy(prefix, optarg, sizeof(prefix));
                break;
            /* interval between targets */
            case 'i':
                ms2ts(&wait_time, strtoul(optarg, NULL, 10));
                break;
            /* loop forever */
            case 'l':
                loop = 1;
                break;
            /* use multiple IP addresses if found */
            /* TODO in this case do we also want to default to showing IP addresses instead of names? */
            case 'm':
                multiple = 1;
                break;
            /* use the portmapper */
            case 'M':
                /* check if it's been changed from the default by the -P option */
                if (port == htons(NFS_PORT)) {
                    port = 0;
                } else {
                    fprintf(stderr, "nfsping: Can't specify both port and portmapper!\n");
                    exit(3);
                }
                break;
            /* check mount protocol */
            case 'n':
                prognum = MOUNTPROG;
                break;
            /* output format */
            case 'o':
                if (format == fping) {
                    fprintf(stderr, "nfsping: Can't specify both -C and -o!\n");
                    usage();
                } else if (format == unixtime) {
                    fprintf(stderr, "nfsping: Can't specify both -D and -o!\n");
                    usage();
                }

                if (strcmp(optarg, "G") == 0) {
                    format = graphite;
                } else if (strcmp(optarg, "S") == 0) {
                    format = statsd;
                } else if (strcmp(optarg, "T") == 0) {
                    format = opentsdb;
                } else {
                    fprintf(stderr, "nfsping: unknown output format \"%s\"!\n", optarg);
                    usage();
                }
                break;
            /* time between pings to target */
            case 'p':
                ms2ts(&sleep_time, strtoul(optarg, NULL, 10));
                break;
            /* specify NFS port */
            case 'P':
                /* check for the portmapper option */
                if (port) {
                    port = htons(strtoul(optarg, NULL, 10));
                } else {
                    fprintf(stderr, "nfsping: Can't specify both portmapper and port!\n");
                    exit(3);
                }    
                break;
            /* quiet, only print summary */
            /* TODO error if output also specified? */
            case 'q':
                quiet = 1;
                break;
            case 'r':
                reconnect = strtoul(optarg, NULL, 10);
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fprintf(stderr, "nfsping: Invalid source IP address!\n");
                    exit(3);
                }
                break;
            /* timeout */
            case 't':
                ms2tv(&timeout, strtoul(optarg, NULL, 10));
                if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
                    fprintf(stderr, "nfsping: zero timeout!\n");
                    exit(3);
                }
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            /* specify NFS version */
            case 'V':
                version = strtoul(optarg, NULL, 10);
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* output formatting doesn't make sense for the simple check */
    if (count == 0 && loop == 0 && format != human) {
        fprintf(stderr, "Can't specify output format without ping count!\n");
        exit(3);
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
        target->next = make_target(argv[index], prognum, port, format);
        target = target->next;

        /* first try treating the hostname as an IP address */
        if (inet_pton(AF_INET, target->name, &((struct sockaddr_in *)target->client_sock)->sin_addr)) {
            /* if we have reverse lookups enabled */
            if (dns) {
                target->name = calloc(1, NI_MAXHOST);
                getaddr = getnameinfo((struct sockaddr *)target->client_sock, sizeof(struct sockaddr_in), target->name, NI_MAXHOST, NULL, 0, 0);
                if (getaddr > 0) { /* failure! */
                    fprintf(stderr, "%s: %s\n", target->name, gai_strerror(getaddr));
                    exit(2); /* ping and fping return 2 for name resolution failures */
                }
                target->ndqf = reverse_fqdn(target->name);
            } else {
                /* don't reverse an IP address */
                target->ndqf = target->name;
            }
            target->client = create_rpc_client(target->client_sock, &hints, prognum, version, timeout, src_ip);
            if (target->client == NULL)
                exit(EXIT_FAILURE);
        } else {
            /* if that fails, do a DNS lookup */
            /* we don't call freeaddrinfo because we keep a pointer to the sin_addr in the target */
            getaddr = getaddrinfo(target->name, "nfs", &hints, &addr);
            if (getaddr == 0) { /* success! */
                /* loop through possibly multiple DNS responses */
                while (addr) {
                    target->client_sock->sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;

                    if (ip) {
                        target->name = calloc(1, INET_ADDRSTRLEN);
                        inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, target->name, INET_ADDRSTRLEN);
                    }
                    target->ndqf = reverse_fqdn(target->name);

                    target->client = create_rpc_client(target->client_sock, &hints, prognum, version, timeout, src_ip);
                    if (target->client == NULL)
                        exit(EXIT_FAILURE);

                    /* multiple results */
                    if (addr->ai_next) {
                        if (multiple) {
                            /* create the next target */
                            target->next = make_target(argv[index], prognum, port, format);
                            target = target->next;
                        } else {
                            /* we have to look up the IP address if we haven't already for the warning */
                            if (!ip) {
                                target->name = calloc(1, INET_ADDRSTRLEN);
                                inet_ntop(AF_INET, &((struct sockaddr_in *)addr->ai_addr)->sin_addr, target->name, INET_ADDRSTRLEN);
                            }
                            fprintf(stderr, "Multiple addresses found for %s, using %s\n", argv[index], target->name);
                            /* if we're not using the IP address again we can free it */
                            if (!ip) {
                                free(target->name);
                                target->name = argv[index];
                            }
                            target->ndqf = reverse_fqdn(target->name);
                            break;
                        }
                    }
                    addr = addr->ai_next;
                }
            } else {
                fprintf(stderr, "getaddrinfo error (%s): %s\n", target->name, gai_strerror(getaddr));
                exit(2); /* ping and fping return 2 for name resolution failures */
            }
        }
    }

    /* skip the first dummy entry */
    targets = targets->next;

    /* allocate space for printing out a summary of all ping times at the end */
    if (format == fping) {
        target = targets;
        while (target) {
            target->results = calloc(count, sizeof(unsigned long));
            if (target->results == NULL) {
                fprintf(stderr, "nfsping: couldn't allocate memory for results!\n");
                exit(3);
            }
            target = target->next;
        }
    }

    /* the main loop */
    while(1) {
        if (quitting) {
            break;
        }

        /* reset back to start of list */
        target = targets;

        while (target) {
            /* first time marker */
            gettimeofday(&call_start, NULL);
            /* the actual ping */
            if (prognum == MOUNTPROG)
                /* TODO version 2 */
                status = mountproc_null_3(NULL, target->client);
            else
                /* TODO version 2 */
                /* this might work for both versions */
                status = nfsproc3_null_3(NULL, target->client);
            /* second time marker */
            gettimeofday(&call_end, NULL);
            target->sent++;

            if (status != NULL) {
                target->received++;

                /* check if we're not looping */
                if (!count && !loop) {
                    printf("%s is alive\n", target->name);
                    target = target->next;
                    continue;
                }

                us = tv2us(call_end) - tv2us(call_start);

                /* TODO discard first ping in case of ARP delay? Only for TCP for handshake? */
                if (us < target->min) target->min = us;
                if (us > target->max) target->max = us;
                /* calculate the average time */
                target->avg = (target->avg * (target->received - 1) + us) / target->received;

                if (format == fping)
                    target->results[target->sent - 1] = us;

                if (!quiet) {
                    /* TODO estimate the time by getting the midpoint of call_start and call_end? */
                    print_output(format, prefix, target, prognum, call_end, us);
                    fflush(stdout);
                }
            /* something went wrong */
            } else {
                fprintf(stderr, "%s : ", target->name);
                clnt_geterr(target->client, &clnt_err);
                if (prognum == MOUNTPROG)
                    clnt_perror(target->client, "mountproc_null_3");
                else
                    clnt_perror(target->client, "nfsproc3_null_3");
                fflush(stderr);

                print_lost(format, prefix, target, prognum, call_end);
                fflush(stdout);

                /* TODO is this needed with portmapper on by default? */
                /* mount port isn't very standard so print a warning */
                if (prognum == MOUNTPROG && target->client_sock->sin_port && clnt_err.re_status == RPC_CANTRECV) {
                    fprintf(stderr, "Unable to contact mount port, consider using portmapper (-M)\n");
                }
                if (!count && !loop) {
                    printf("%s is dead\n", target->name);
                }
            }

            target = target->next;
            if (target)
                nanosleep(&wait_time, NULL);
        }

        /* reset back to start of list */
        /* do this at the end of the loop not the start so we can check if we're done or need to sleep */
        target = targets;

        /* if we're not looping we can exit now */
        if (!count && !loop) {
            /* loop through the targets, if we find any that errored, exit with a failure */
            while (target) {
                if (target->received)
                    target = target->next;
                else
                    exit(EXIT_FAILURE);
            }
            /* didn't find any failures */
            exit(EXIT_SUCCESS);
        }

        /* check the first target */
        if (count && targets->sent >= count) {
            break;
        }

        /* see if we should disconnect and reconnect */
        if (reconnect && targets->sent % reconnect == 0) {
            destroy_rpc_client(target->client);
            target->client = create_rpc_client(target->client_sock, &hints, prognum, version, timeout, src_ip);
            if (target->client == NULL)
                exit(EXIT_FAILURE);
        }

        nanosleep(&sleep_time, NULL);
    }
    fflush(stdout);
    /* these print to stderr */
    if (!quiet && (format == human || format == fping || format == unixtime))
        fprintf(stderr, "\n");
    /* don't print summary for formatted output */
    if (format == fping)
        print_fping_summary(*targets);
    else if (format == human || format == unixtime)
        print_summary(*targets);
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
