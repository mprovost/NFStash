#include "nfsping.h"
#include "rpc.h"
#include "util.h"
#include "stddef.h"

/* local prototypes */
static void usage(void);
static FSSTAT3res *get_fsstat(CLIENT *, nfs_fh_list *);
static int prefix_print(size3, char *, enum byte_prefix);
static int print_df(int, int, char *, char *, FSSTAT3res *, const enum byte_prefix);
static void print_inodes(int, int, char *, char *, FSSTAT3res *);
static char *replace_char(const char *, const char *, const char *);
static void print_format(enum outputs, char *, char *, char *, FSSTAT3res *, struct timeval);

/* globals */
int verbose = 0;

void usage() {
    printf("Usage: nfsdf [options] [filehandle...]\n\
    -g         display sizes in gigabytes\n\
    -G         Graphite format output (default human readable)\n\
    -h         display human readable sizes (default)\n\
    -H         frequency in Hertz (requests per second, default 1)\n\
    -i         display inodes\n\
    -k         display sizes in kilobytes\n\
    -l         loop forever\n\
    -m         display sizes in megabytes\n\
    -p string  prefix for graphite metric names\n\
    -t         display sizes in terabytes\n\
    -S addr    set source address\n\
    -T         use TCP (default UDP)\n\
    -v         verbose output\n");

    exit(3);
}


FSSTAT3res *get_fsstat(CLIENT *client, nfs_fh_list *fs) {
    FSSTAT3args fsstatarg;
    FSSTAT3res  *fsstatres;
    struct rpc_err clnt_err;

    fsstatarg.fsroot = fs->nfs_fh;

    fsstatres = nfsproc3_fsstat_3(&fsstatarg, client);

    if (fsstatres) {
        if (fsstatres->status != NFS3_OK) {
            fprintf(stderr, "%s:%s ", fs->host, fs->path);
            clnt_geterr(client, &clnt_err);
            if (clnt_err.re_status)
                clnt_perror(client, "nfsproc3_fsstat_3");
            else
                nfs_perror(fsstatres->status);
        }
    } else {
        fprintf(stderr, "%s:%s ", fs->host, fs->path);
        clnt_perror(client, "nfsproc3_fsstat_3");
    }

    return fsstatres;
}


int prefix_print(size3 input, char *output, enum byte_prefix prefix) {
    int index;
    size3 shifted;

    /* TODO PETA (and BYTE?) */
    static const char label[] = {
        [KILO] = 'K',
        [MEGA] = 'M',
        [GIGA] = 'G',
        [TERA] = 'T',
    };

    if (prefix == HUMAN) {
        /* try and find the best fit, starting with terabytes and working down */
        prefix = TERA;
        while (prefix) {
            shifted = input >> prefix;
            if (shifted && shifted > 10)
                break;
            prefix -= 10;
        }
    }

    /* TODO check the length */
    index = snprintf(output, 13, "%" PRIu64 "", input >> prefix);

    /* print the label */
    /* FIXME only print this for prefix=0 aka human mode otherwise stuff the prefix in the header */
    output[index] = label[prefix];

    /* all of them end in B(ytes) */
    output[++index] = 'B';
    output[++index] = '\0';

    return index;
}


int print_df(int offset, int width, char *host, char *path, FSSTAT3res *fsstatres, const enum byte_prefix prefix) {
    /* 13 is enough for a petabyte in kilobytes, plus three for the label and a trailing NUL */
    char total[16];
    char used[16];
    char avail[16];
    double capacity;

    if (fsstatres && fsstatres->status == NFS3_OK) {
        /* format results by prefix */
        prefix_print(fsstatres->FSSTAT3res_u.resok.tbytes, total, prefix);
        prefix_print(fsstatres->FSSTAT3res_u.resok.tbytes - fsstatres->FSSTAT3res_u.resok.fbytes, used, prefix);
        prefix_print(fsstatres->FSSTAT3res_u.resok.fbytes, avail, prefix);

        /* percent full */
        capacity = (1 - ((double)fsstatres->FSSTAT3res_u.resok.fbytes / fsstatres->FSSTAT3res_u.resok.tbytes)) * 100;

        printf("%s:%-*s %*s %*s %*s %7.0f%%\n",
            host, offset, path, width, total, width, used, width, avail, capacity);
    } else {
        /* get_fsstat will print the rpc error */
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


/* TODO human readable output ie 4M, use the -h flag */
/* base 10 vs base 2? (should this be an option for bytes as well?) */
void print_inodes(int offset, int width, char *host, char *path, FSSTAT3res *fsstatres) {
    double capacity;

    /* percent used */
    capacity = (1 - ((double)fsstatres->FSSTAT3res_u.resok.ffiles / fsstatres->FSSTAT3res_u.resok.tfiles)) * 100;

    printf("%s:%-*s %*" PRIu64 " %*" PRIu64 " %*" PRIu64 " %7.0f%%\n",
        host,
        offset, path,
        width, fsstatres->FSSTAT3res_u.resok.tfiles,
        width, fsstatres->FSSTAT3res_u.resok.tfiles - fsstatres->FSSTAT3res_u.resok.ffiles,
        width, fsstatres->FSSTAT3res_u.resok.ffiles,
        capacity);
}


char *replace_char(const char *str, const char *old, const char *new)
{
    char *ret, *r;
    const char *p, *q;
    size_t oldlen = strlen(old);
    size_t count = 0, retlen, newlen = strlen(new);
    int samesize = (oldlen == newlen);

    if (!samesize) {
        for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
            count++;
        /* This is undefined if p - str > PTRDIFF_MAX */
        retlen = p - str + strlen(p) + count * (newlen - oldlen);
    } else
        retlen = strlen(str);

    if ((ret = malloc(retlen + 1)) == NULL)
        return NULL;

    r = ret, p = str;
    while (1) {
        if (!samesize && !count--)
            break;
        if ((q = strstr(p, old)) == NULL)
            break;
        ptrdiff_t l = q - p;
        memcpy(r, p, l);
        r += l;
        memcpy(r, new, newlen);
        r += newlen;
        p = q + oldlen;
    }
    strcpy(r, p);

    return ret;
}

/* formatted output ie graphite */
/* TODO escape dots and spaces (replace with underscores) in paths */
void print_format(enum outputs format, char *prefix, char *host, char *path, FSSTAT3res *fsstatres, struct timeval now) {
    char *ndqf;
    char *bad_characters[] = {
        " ", ".", "-", "/"
    };
    int index = 0;
    int number_of_chars = sizeof(bad_characters) / sizeof(bad_characters[0]);

    for (index = 0; index < number_of_chars; index++) {
        path = replace_char(path, bad_characters[index], "_");
    }

    struct sockaddr_in sock;

    /* first try treating the hostname as an IP address */
    if (inet_pton(AF_INET, host, &(sock.sin_addr))) {
        /* don't reverse an IP address */
        ndqf = host;
    } else {
        ndqf = reverse_fqdn(host);
    }

    switch (format) {
        case graphite:
            printf("%s.%s.df.%s.tbytes %" PRIu64 " %li\n", prefix, ndqf, path, fsstatres->FSSTAT3res_u.resok.tbytes, now.tv_sec);
            printf("%s.%s.df.%s.fbytes %" PRIu64 " %li\n", prefix, ndqf, path, fsstatres->FSSTAT3res_u.resok.fbytes, now.tv_sec);
            printf("%s.%s.df.%s.tfiles %" PRIu64 " %li\n", prefix, ndqf, path, fsstatres->FSSTAT3res_u.resok.tfiles, now.tv_sec);
            printf("%s.%s.df.%s.ffiles %" PRIu64 " %li\n", prefix, ndqf, path, fsstatres->FSSTAT3res_u.resok.ffiles, now.tv_sec);
            break;
        default:
            fatal("Unsupported format\n");
    }
}


int main(int argc, char **argv) {
    int ch;
    int inodes = 0;
    enum byte_prefix prefix = NONE;
    enum outputs format = unset;
    char output_prefix[255] = "nfs";
    int width  = 0;
    char *input_fh = NULL;
    size_t n = 0; /* for getline() */
    nfs_fh_list *filehandles, *current, fh_dummy;
    int loop = 0;
    unsigned int maxpath = 0;
    unsigned int maxhost = 0;
    CLIENT *client = NULL;
    struct sockaddr_in clnt_info;
    unsigned long version = 3;
    struct timespec sleep_time;
    unsigned long hertz = NFS_HERTZ;
    struct timeval timeout = NFS_TIMEOUT;
    struct timeval call_start, call_end;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    FSSTAT3res *fsstatres;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    /* The longest output for each column (up to 9 petabytes in KB) is 13 digits plus two for label*/
    /* TODO struct so we can put in a label and a width */
    static const int prefix_width[] = {
        /* if we're using human output the column will never be longer than 4 digits plus two for label */
        [HUMAN] = 6,
        /* 9PB in KB = 9.8956e12 */
        [KILO] = 13,
        /* 9PB in MB = 9.664e+9 */
        [MEGA] = 10,
        /* 9PB in GB = 9.437e+6 */
        [GIGA] = 7,
        /* 9PB in TB = 9216 */
        [TERA] = 4,
    };


    while ((ch = getopt(argc, argv, "gGhH:iklmp:S:tTv")) != -1) {
        switch(ch) {
            /* display gigabytes */
            case 'g':
                if (prefix != NONE) {
                    fatal("Can't specify multiple units!\n");
                } else {
                    prefix = GIGA;
                }
                break;
            /* Graphite output */
            case 'G':
                if (prefix != NONE) {
                    fatal("Can't specify units and -G!\n");
                } else {
                    format = graphite;
                }
                break;
            /* display human readable (default, set below) */
            case 'h':
                if (prefix != NONE) {
                    fatal("Can't specify multiple units!\n");
                } else {
                    prefix = HUMAN;
                }
                break;
            /* polling frequency */
            case 'H':
                /* TODO check for reasonable values */
                hertz = strtoul(optarg, NULL, 10);
                break;
            /* display inodes */
            case 'i':
                inodes = 1; 
                break;
            /* display kilobytes */
            case 'k':
                if (prefix != NONE) {
                    fatal("Can't specify multiple units!\n");
                } else {
                    prefix = KILO;
                }
                break;
            /* loop forever */
            case 'l':
                loop = 1;
                if (format == unset) {
                    format = ping;
                }
                break;
            /* display megabytes */
            case 'm':
                if (prefix != NONE) {
                    fatal("Can't specify multiple units!\n");
                } else {
                    prefix = MEGA;
                }
                break;
            /* prefix to use for graphite metrics */
            case 'p':
                strncpy(output_prefix, optarg, sizeof(output_prefix));
                break;
            /* specify source address */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("nfsping: Invalid source IP address!\n");
                }
                break;
            /* display terabytes */
            case 't':
                if (prefix != NONE) {
                    fatal("Can't specify multiple units!\n");
                } else {
                    prefix = TERA;
                }
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            /* verbose */
            case 'v':
                verbose = 1;
                break;
            /* have to keep -h available for human readable output */
            case '?':
            default:
                usage();
        }
    }

    /* default to human readable */
    if (prefix == NONE) {
        prefix = HUMAN;
    }

    /* default output */
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

    current = &fh_dummy;
    filehandles = current;

    /* check if we don't have any command line targets */
    if (optind == argc) {
        if (getline(&input_fh, &n, stdin) == -1) {
            input_fh = NULL;
        }
    } else {
        input_fh = argv[optind];
    }
    
    /* first parse all of the input filehandles into a list 
     * this gives us the longest path so we can lay out the output */
    while (input_fh) {

        current->next = parse_fh(input_fh);
        current = current->next;

        /* save the longest host/paths for display formatting */
        if (current) {
            if (strlen(current->path) > maxpath)
                maxpath = strlen(current->path);

            if (strlen(current->host) > maxhost)
                maxhost = strlen(current->host);
        }

        /* parse next argument or line from stdin */
        if (optind == argc) {
            if (getline(&input_fh, &n, stdin) == -1) {
                input_fh = NULL;
            }
        } else {
            optind++;
            if (optind < argc) {
                input_fh = argv[optind];
            } else {
                input_fh = NULL;
            }
        }
    }

    if (format == ping) {
        /* header */
        /* Print the header before sending any RPCs, this means we have to guess about the size of the results
           but it lets the user know that the program is running. Then we can print the results as they come in
           which will also give a visual indication of which filesystems are slow to respond */
        /* TODO check max line length < 80 or truncate path (or -w option for wide?) */
        if (inodes) {
            /* with 32 bit inodes you can have 2^32 per filesystem = 4294967296 = 10 digits */
            /* ZFS can have 2^48 inodes which is 15 digits */
            /* let's assume 32 bits is enough for now */
            width = 10;
        } else {
            width = prefix_width[prefix];
        }
        /* extra space for gap between columns */
        width++;

        /* FIXME print prefix in total column */
        printf("%-*s %*s %*s %*s capacity\n",
            maxhost + maxpath + 1, "Filesystem", width, "total", width, "used", width, "avail");
    }

    do {
        /* reset to start of list */
        /* skip the first empty struct */
        current = filehandles->next;

        while (current) {
            /* see if we can reuse the previous client connection */
            if (client) {
                if (clnt_info.sin_addr.s_addr != current->client_sock->sin_addr.s_addr) {
                    client = destroy_rpc_client(client);
                }
            }

            /* otherwise make a new connection */
            if (client == NULL) {
                current->client_sock->sin_family = AF_INET;
                current->client_sock->sin_port = htons(NFS_PORT);

                client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, version, timeout, src_ip);
                /* don't use default AUTH_NONE */
                auth_destroy(client->cl_auth);
                /* set up AUTH_SYS */
                client->cl_auth = authunix_create_default();

                /* look up the address that was used to connect to the server */
                clnt_control(client, CLGET_SERVER_ADDR, (char *)&clnt_info);
            }

            /* first time marker */
            gettimeofday(&call_start, NULL);
            /* the rpc call */
            fsstatres = get_fsstat(client, current);
            /* second time marker */
            gettimeofday(&call_end, NULL);

            if (fsstatres && fsstatres->status == NFS3_OK) {
                if (format == ping) {
                    if (inodes) {
                        print_inodes(maxpath, width, current->host, current->path, fsstatres);
                    } else {
                        print_df(maxpath, width, current->host, current->path, fsstatres, prefix);
                    }
                } else {
                    print_format(format, output_prefix, current->host, current->path, fsstatres, call_end);
                }
            }

            if (loop) {
                nanosleep(&sleep_time, NULL);
            }

            current = current->next;
        }
    } while (loop);

    return EXIT_SUCCESS;
}
