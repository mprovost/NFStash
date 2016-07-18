#include "nfsping.h"
#include "rpc.h"
#include "util.h"
#include <sys/stat.h> /* for file mode bits */
#include <pwd.h> /* getpwuid() */
#include <grp.h> /* getgrgid() */
#include <math.h> /* for log10() */
#include <libgen.h> /* basename() */

/* local prototypes */
static void usage(void);
static entryplus3 *do_getattr(CLIENT *, char *, nfs_fh_list *);
static entryplus3 *do_readdirplus(CLIENT *, char *, nfs_fh_list *);
char *lsperms(char *, ftype3, mode3);
int print_long_listing(targets_t *);
int print_filehandles(entryplus3 *, char *, char *, char *);

/* globals */
int verbose = 0;

/* global config "object" */
static struct config {
    /* ls -a */
    int listdot;
    /* ls -l */
    int long_listing;
    /* NFS version */
    unsigned long version;
    struct timeval timeout;
} cfg;

/* default config */
const struct config CONFIG_DEFAULT = {
    .listdot = 0,
    .long_listing = 0,
    .version = 3,
    .timeout = NFS_TIMEOUT,
};

void usage() {
    /* TODO -A to show IP addresses */
    printf("Usage: nfsls [options]\n\
    -a       print hidden files\n\
    -h       display this help and exit\n\
    -l       print long listing\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n"); 

    exit(3);
}


/* do a getattr to get attributes for a single file */
/* return a single directory entry so we can share code with do_readdirplus() */
entryplus3 *do_getattr(CLIENT *client, char *host, nfs_fh_list *fh) {
    GETATTR3res *res;
    GETATTR3args args = {
        .object = fh->nfs_fh
    };
    /* make an empty directory entry for the result */
    entryplus3 *res_entry = calloc(1, sizeof(entryplus3));
    struct rpc_err clnt_err;
    /* filename */
    char *base;
    /* directory */
    char *path = fh->path;

    /* the RPC call */
    res = nfsproc3_getattr_3(&args, client);

    if (res) {
        if (res->status == NFS3_OK) {
            /* the path we were given is a filename, so chop it up */
            /* first make a copy of the path in case basename() modifies it */
            base = strndup(fh->path, MNTPATHLEN);
            /* get the base filename */
            base = basename(base);
            /* get the path component(s) */
            path = dirname(fh->path);
            strncpy(fh->path, path, MNTPATHLEN);
            /* copy just the filename into the result entry */
            res_entry->name = strndup(base, NFS_MAXNAMLEN);

            /* copy the pointer to the file attributes into the blank directory entry */
            res_entry->name_attributes.post_op_attr_u.attributes = res->GETATTR3res_u.resok.obj_attributes;
            res_entry->name_attributes.attributes_follow = 1;
            /* copy the inode number */
            res_entry->fileid = res->GETATTR3res_u.resok.obj_attributes.fileid;
            /* copy the filehandle */
            memcpy(&res_entry->name_handle.post_op_fh3_u.handle, &fh->nfs_fh, sizeof(nfs_fh3));
            res_entry->name_handle.handle_follows = 1;
        } else {
            fprintf(stderr, "%s:%s: ", host, path);
            clnt_geterr(client, &clnt_err);
            if (clnt_err.re_status)
                clnt_perror(client, "nfsproc3_getattr_3");
            else
                nfs_perror(res->status);
        }
    } else {
        clnt_perror(client, "nfsproc3_getattr_3");
    }  

    return res_entry;
}


/* do readdirplus calls to get a full list of directory entries */
entryplus3 *do_readdirplus(CLIENT *client, char *host, nfs_fh_list *fh) {
    char *path = fh->path;
    READDIRPLUS3res *res;
    entryplus3 dummy = { 0 };
    entryplus3 *res_entry, *current = &dummy;
    /* TODO make the dircount/maxcount into options */
    READDIRPLUS3args args = {
        .dir = fh->nfs_fh,
        .cookie = 0,
        .dircount = 512,
        .maxcount = 8192,
    };
    struct rpc_err clnt_err;


    /* the RPC call */
    res = nfsproc3_readdirplus_3(&args, client);

    if (res) {
        /* loop through results, might take multiple calls for the whole directory */
        while (res) {
            if (res->status == NFS3_OK) {
                res_entry = res->READDIRPLUS3res_u.resok.reply.entries;

                /* loop through the directory entries in the RPC result */
                while (res_entry) {
                    /* first check for hidden files */
                    if (cfg.listdot == 0 && res_entry->name[0] == '.') {
                        /* terminate the list in case this is the last entry */
                        current->nextentry = NULL;
                        /* skip adding it to the list */
                        res_entry = res_entry->nextentry;
                        continue;
                    }

                    current->nextentry = calloc(1, sizeof(entryplus3));
                    current = current->nextentry;
                    current = memcpy(current, res_entry, sizeof(entryplus3));

                    args.cookie = res_entry->cookie;

                    res_entry = res_entry->nextentry;
                }

                /* check for the end of directory */
                if (res->READDIRPLUS3res_u.resok.reply.eof) {
                    break;
                /* do another RPC call for more entries */
                } else {
                    /* TODO does this need a copy? */
                    memcpy(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE);
                    res = nfsproc3_readdirplus_3(&args, client);

                    if (res == NULL) {
                        clnt_perror(client, "nfsproc3_readdirplus_3");
                    }
                }
            } else {
                /* it's a file, do a getattr instead */
                if (res->status == NFS3ERR_NOTDIR) {
                    current->nextentry = do_getattr(client, host, fh);

                    /* there's only a single entry for a file so exit the loop */
                    break;
                } else {
                    fprintf(stderr, "%s:%s: ", host, path);
                    clnt_geterr(client, &clnt_err);
                    if (clnt_err.re_status)
                        clnt_perror(client, "nfsproc3_readdirplus_3");
                    else
                        nfs_perror(res->status);
                    break;
                }
            }
        }
    } else {
        clnt_perror(client, "nfsproc3_readdirplus_3");
    }  

    return dummy.nextentry;
}


/* generate a string of the file type and permissions bits of a file like ls -l */
/* based on http://stackoverflow.com/questions/10323060/printing-file-permissions-like-ls-l-using-stat2-in-c */
char *lsperms(char *bits, ftype3 type, mode3 mode) {
    const char *rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};

    switch (type) {
        case NF3REG:
            bits[0] = '-';
            break;
        case NF3DIR:
            bits[0] = 'd';
            break;
        case NF3BLK:
            bits[0] = 'b';
            break;
        case NF3CHR:
            bits[0] = 'c';
            break;
        case NF3LNK:
            bits[0] = 'l';
            break;
        case NF3SOCK:
            bits[0] = 's';
            break;
        case NF3FIFO:
            bits[0] = 'p';
            break;
        default:
            /* Unknown type -- possibly a regular file? */
            bits[0] = '?';
    }

    strcpy(&bits[1], rwx[(mode >> 6)& 7]);
    strcpy(&bits[4], rwx[(mode >> 3)& 7]);
    strcpy(&bits[7], rwx[(mode & 7)]);

    if (mode & S_ISUID)
        bits[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID)
        bits[6] = (mode & S_IXGRP) ? 's' : 'l';
    if (mode & S_ISVTX)
        bits[9] = (mode & S_IXUSR) ? 't' : 'T';

    bits[10] = '\0';

    return(bits);
};


/* ls -l */
/* loop through a list of directory entries printing a long listing for each */
/* our format:
   drwxr-xr-x 2 root root     20480 2016-02-12 22:58:43 dumpy known_hosts
 */
/* TODO print milliseconds response time - how to format for directories with a single readdirplus? multiple readdirplus? ..? */
/* TODO -F to print trailing slash for directories */
/* TODO cache/memoise uid/gid lookups */
/* BSD has functions user_from_uid() and group_from_gid() */
/* gnulib has getuser() and getgroup() */
int print_long_listing(targets_t *targets) {
    targets_t *target = targets;
    struct nfs_fh_list *fh;
    entryplus3 *current;
    /* shortcut */
    struct fattr3 attributes;
    /* number of lines of output */
    int count = 0;
    /* string for storing permissions bits */
    /* needs to be 11 with the file type */
    char bits[11];
    struct passwd *passwd;
    struct group  *group;
    struct tm     *mtime;
    /* timestamp in ISO 8601 format */
    /* 2000-12-25 22:23:34 + terminating NULL */
    char buf[20];
    /* sizes for justifying columns */
    /* set these to 1 so log10() doesn't have 0 as an input and returns -HUGE_VAL */
    /* we're always going to need one space to output "0" */
    unsigned long maxlinks = 1;
    size3         maxsize  = 1;
    size_t        maxuser  = 0;
    size_t        maxgroup = 0;
    size_t        maxhost  = 0;


    /* first loop through all targets and entries to find longest strings for justifying columns */
    while (target) {
        /* find the longest hostname */
        maxhost = strlen(target->name) > maxhost ? strlen(target->name) : maxhost;

        fh = target->filehandles;

        while (fh) {
            current = fh->entries;

            while (current) {
                count++;

                attributes = current->name_attributes.post_op_attr_u.attributes;

                maxlinks = attributes.nlink > maxlinks ? attributes.nlink : maxlinks;

                maxsize = attributes.size > maxsize ? attributes.size : maxsize;

                /* TODO cache this ! */
                passwd = getpwuid(attributes.uid);
                maxuser = strlen(passwd->pw_name) > maxuser ? strlen(passwd->pw_name) : maxuser;

                /* TODO cache this ! */
                group  = getgrgid(attributes.gid);
                maxgroup = strlen(group->gr_name) > maxgroup ? strlen(group->gr_name) : maxgroup;

                current = current->nextentry;
            } /* while (current) */

            fh = fh->next;
        } /* while (fh) */

        target = target->next;
    } /* while (target) */

    /* calculate the maximum string widths */
    maxlinks = floor(log10(abs(maxlinks))) + 1;
    maxsize  = floor(log10(abs(maxsize))) + 1;

    /* now loop through and print each entry */

    /* reset to start of target list */
    target = targets;

    while(target) {
        fh = target->filehandles;

        while (fh) {
            current = fh->entries;

            while (current) {
                attributes = current->name_attributes.post_op_attr_u.attributes;

                /* look up username and group locally */
                /* TODO -n option to keep uid/gid */
                /* TODO check for NULL return value */
                passwd = getpwuid(attributes.uid);
                group  = getgrgid(attributes.gid);

                /* format to ISO 8601 timestamp */
                /* this converts an unsigned 32 bit seconds to a signed 32 bit time_t which doesn't always do what is expected! */
                /* TODO detect values greater than 32 bit signed max and treat them as signed? */
                /* Solaris has a setting nfs_allow_preepoch_time for this, make it into an option? */
                mtime = localtime(&attributes.mtime.seconds);
                /* TODO check return value, should always be 19 */
                strftime(buf, 20, "%Y-%m-%d %H:%M:%S", mtime);

                /* have to cast size_t to int for compiler warning (-Wformat) */
                /* printf only accepts ints for field widths with * */
                printf("%s %*lu %-*s %-*s %*" PRIu64 " %s %-*s %s\n",
                    /* permissions bits */
                    lsperms(bits, attributes.type, attributes.mode),
                    /* number of links */
                    (int)maxlinks, attributes.nlink,
                    /* username */
                    (int)maxuser, passwd->pw_name,
                    /* group */
                    (int)maxgroup, group->gr_name,
                    /* file size */
                    (int)maxsize, attributes.size,
                    /* date + time */
                    buf,
                    /* hostname */
                    (int)maxhost, target->name,
                    /* filename */
                    current->name);

                current = current->nextentry;
            }

            fh = fh->next;
        }

        target = target->next;
    }


    return count;
}


/* loop through a list of directory entries printing a JSON filehandle for each */
int print_filehandles(entryplus3 *entries, char *host, char *ip_address, char *path) {
    entryplus3 *current = entries;
    int count = 0;
    /* space for a string in case it's a directory and we add a trailing slash */
    /* leave room for NULL + / */
    char file_name[NFS_MAXNAMLEN + 2];
    /* pointer to which filename string to use */
    char *file_name_p;

    while (current) {
        count++;

        /* if there is no filehandle (/dev, /proc, etc) don't print */
        /* none of the other utilities can do anything without a filehandle */
        if (current->name_handle.post_op_fh3_u.handle.data.data_len) {
            /* if it's a directory print a trailing slash */
            /* TODO do we even need to do this (unless -F?)? print_nfs_fh3 already adds a / after the path */
            /* TODO this seems to be 0 sometimes */
            if (current->name_attributes.post_op_attr_u.attributes.type == NF3DIR) {
                /* filename should only be max plus null, leave room for the extra / */
                strncpy(file_name, current->name, NFS_MAXNAMLEN + 1);
                file_name[strlen(file_name)] = '/';
                file_name_p = file_name;
            /* not a directory, just use the received filename */
            } else {
                file_name_p = current->name;
            }

            /* TODO print milliseconds */
            print_nfs_fh3(host, ip_address, path, file_name_p, current->name_handle.post_op_fh3_u.handle);
        }

        current = current->nextentry;
    }

    return count;
}


int main(int argc, char **argv) {
    int ch;
    int count = 0;
    char   *input_fh  = NULL;
    size_t  input_len = 0;
    targets_t dummy = { 0 };
    targets_t *targets = &dummy;
    targets_t *current;
    struct nfs_fh_list *filehandle;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM,
    };
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };

    cfg = CONFIG_DEFAULT;

    while ((ch = getopt(argc, argv, "ahlS:Tv")) != -1) {
        switch(ch) {
            /* list hidden files */
            case 'a':
                cfg.listdot = 1;
                break;
            /* long listing */
            case 'l':
                cfg.long_listing = 1;
                break;
            /* source ip address for packets */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
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
            case 'h':
            default:
                usage();
        }
    }

    /* no arguments, use stdin */
    while (getline(&input_fh, &input_len, stdin) != -1) {
        current = parse_fh(targets, input_fh, 0, 0, ping);
    }

    /* skip the dummy entry */
    targets = targets->next;
    current = targets;

    /* set timezone for date output */
    /* TODO only with long_listing set? */
    tzset();

    /* first go through and send RPCs to each filehandle in each target */
    while (current) {
        if (current->client == NULL) {
            /* connect to server */
            current->client = create_rpc_client(current->client_sock, &hints, NFS_PROGRAM, cfg.version, cfg.timeout, src_ip);
            auth_destroy(current->client->cl_auth);
            current->client->cl_auth = authunix_create_default();
        }

        if (current->client) {
            filehandle = current->filehandles;

            while (filehandle) {
                /* TODO check for a trailing slash and then call do_getattr directly */
                filehandle->entries = do_readdirplus(current->client, current->name, filehandle);

                /*
                TODO make an outputs enum with json/longform/ping/fping/graphite/statsd
                print_output function to switch
                iterate through entries and call print_nfs_fh3 if json
                some option to print raw request results in JSON including cookie (-d?)
                */

                if (cfg.long_listing == 0) {
                    print_filehandles(filehandle->entries, current->name, current->ip_address, filehandle->path);
                }

                filehandle = filehandle->next;
            } /* while (filehandle) */
        }

        current = current->next;
    } /* while (current) */


    /* pass the whole list for printing long listing */
    if (cfg.long_listing) {
        print_long_listing(targets);
    }


    /* return success if we saw any entries */
    /* TODO something better! */
    if (count) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
