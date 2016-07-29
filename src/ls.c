#include "nfsping.h"
#include "rpc.h"
#include "util.h"
#include "xdr_copy.h"
#include <sys/stat.h> /* for file mode bits */
#include <pwd.h> /* getpwuid() */
#include <grp.h> /* getgrgid() */
#include <math.h> /* for log10() */
#include <libgen.h> /* basename() */

/* local prototypes */
static void usage(void);
static char *do_readlink(CLIENT *, char *, char *, nfs_fh3);
static entrypluslink3 *do_getattr(CLIENT *, char *, nfs_fh_list *);
static entrypluslink3 *do_readdirplus(CLIENT *, char *, nfs_fh_list *);
static char *lsperms(char *, ftype3, mode3);
static int print_long_listing(targets_t *);
static void print_nfs_fh3(char *, char *, char *, char *, nfs_fh3, const unsigned long);
static int print_filehandles(targets_t *, nfs_fh_list *, const unsigned long);

/* globals */
extern volatile sig_atomic_t quitting;
int verbose = 0;

/* output formats */
enum ls_formats {
    ls_unset,
    ls_ping,
    ls_longform,
    ls_json,
};

/* global config "object" */
static struct config {
    /* output format */
    enum ls_formats format;
    /* ls -d */
    int listdir;
    /* ls -a */
    int listdot;
    /* -A */
    int display_ips;
    /* -L */
    int loop;
    /* -c */
    unsigned long count;
    /* NFS version */
    unsigned long version;
    struct timeval timeout;
} cfg;

/* default config */
const struct config CONFIG_DEFAULT = {
    .format       = ls_unset,
    .listdir      = 0,
    .listdot      = 0,
    .display_ips  = 0,
    .loop         = 0,
    .count        = 0,
    .version      = 3,
    .timeout      = NFS_TIMEOUT,
};

void usage() {
    printf("Usage: nfsls [options]\n\
List NFS files and directories from stdin\n\n\
    -a       print hidden files\n\
    -A       show IP addresses\n\
    -d       list actual directory not contents\n\
    -h       display this help and exit\n\
    -H       frequency in Hertz (requests per second, default %i)\n\
    -l       print long listing\n\
    -L       loop forever\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n",
    NFS_HERTZ); 

    exit(3);
}


/* do a readlink to look up symlinks */
/* return a char * to the string with the symlink name */
/* the other calls should already have the attributes */
char *do_readlink(CLIENT *client, char *host, char *path, nfs_fh3 fh) {
    READLINK3res *res;
    READLINK3args args = {
        .symlink = fh
    };
    const char *proc = "nfsproc3_readlink_3";
    /* the result */
    char *symlink = NULL; /* return a NULL pointer to signal failure */
    struct rpc_err clnt_err;

    res = nfsproc3_readlink_3(&args, client);

    if (res) {
        if (res->status == NFS3_OK) {
            /* make a copy of the symlink to return */
            symlink = strdup(res->READLINK3res_u.resok.data);
        } else {
            fprintf(stderr, "%s:%s: ", host, path);
            clnt_geterr(client, &clnt_err);
            clnt_err.re_status ? clnt_perror(client, proc) : nfs_perror(res->status, proc);
        }
   
        /* free the result */
        xdr_free((xdrproc_t)xdr_READLINK3res, (char *)res);
    } else {
        clnt_perror(client, proc);
    }

    return symlink;
}


/* do a getattr to get attributes for a single file */
/* return a single directory entry so we can share code with do_readdirplus() */
entrypluslink3 *do_getattr(CLIENT *client, char *host, nfs_fh_list *fh) {
    GETATTR3res *res;
    /* shortcut */
    struct fattr3 attributes;
    GETATTR3args args = {
        .object = fh->nfs_fh
    };
    const char *proc = "nfsproc3_getattr_3";
    /* the result */
    entrypluslink3 *res_entry = NULL;
    struct rpc_err clnt_err;
    /* filename */
    char *base;
    /* directory */
    char *path = fh->path;

    /* the RPC call */
    res = nfsproc3_getattr_3(&args, client);

    if (res) {
        if (res->status == NFS3_OK) {
            attributes = res->GETATTR3res_u.resok.obj_attributes;

            /* check if the result is a directory
             * if it is then do a readdirplus to get its entries
             * do_readdirplus() can also call do_getattr() but only if the result isn't a directory so it shouldn't loop
             */
            if (attributes.type == NF3DIR && cfg.listdir == 0) {
                /* do a readdirplus */
                res_entry = do_readdirplus(client, host, fh);

            /* not a directory, or we're listing the directory itself */
            } else {
                /* make an empty directory entry for the result */
                res_entry = calloc(1, sizeof(entrypluslink3));

                /* the path we were given is a filename, so chop it up */
                /* first make a copy of the path in case basename() modifies it */
                base = strndup(fh->path, MNTPATHLEN);
                /* get the base filename */
                base = basename(base);

                /* if it's a directory print a trailing slash (like ls -F) */
                if (attributes.type == NF3DIR) {
                    /* make space for the filename plus / plus NULL */
                    res_entry->name = calloc(strlen(base) + 2, sizeof(char));
                    strncpy(res_entry->name, base, strlen(base));
                    /* add a trailing slash */
                    res_entry->name[strlen(res_entry->name)] = '/';
                /* if it's a symlink, do another RPC to look up the target */
                } else if (attributes.type == NF3LNK) {
                    res_entry->symlink = do_readlink(client, host, fh->path, fh->nfs_fh);
                    res_entry->name = strdup(base);
                } else {
                    /* just use the received filename */
                    res_entry->name = strdup(base);
                }

                /* get the path component(s) */
                path = dirname(fh->path);
                strncpy(fh->path, path, MNTPATHLEN);

                /* copy the pointer to the file attributes into the blank directory entry */
                res_entry->name_attributes.post_op_attr_u.attributes = attributes;
                res_entry->name_attributes.attributes_follow = 1;

                /* copy the inode number */
                res_entry->fileid = attributes.fileid;

                /* copy the filehandle */
                memcpy(&res_entry->name_handle.post_op_fh3_u.handle, &fh->nfs_fh, sizeof(nfs_fh3));
                res_entry->name_handle.handle_follows = 1;
            }
        } else {
            fprintf(stderr, "%s:%s: ", host, path);
            /* return a NULL entry to signal failure */
            res_entry = NULL;

            clnt_geterr(client, &clnt_err);
            clnt_err.re_status ? clnt_perror(client, proc) : nfs_perror(res->status, proc);
        }
   
        /* free the result */
        xdr_free((xdrproc_t)xdr_GETATTR3res, (char *)res);
    } else {
        clnt_perror(client, proc);
    }

    return res_entry;
}


/* do readdirplus calls to get a full list of directory entries */
/* returns NULL if no entries found */
entrypluslink3 *do_readdirplus(CLIENT *client, char *host, nfs_fh_list *fh) {
    READDIRPLUS3res *res;
    /* results from server */
    entryplus3 *res_entry;
    /* our list of entries */
    entrypluslink3 dummy = {
        .next = NULL /* make sure this is NULL in case we don't return any entries */
    };
    entrypluslink3 *current = &dummy;
    /* TODO make the dircount/maxcount into options */
    READDIRPLUS3args args = {
        .dir = fh->nfs_fh,
        .cookie = 0,
        .cookieverf =  { 0 },
        .dircount = 1024,
        .maxcount = 8192,
    };
    /* an empty cookieverf for comparison */
    const char emptyverf[NFS3_COOKIEVERFSIZE] = { 0 };
    const char *proc = "nfsproc3_readdirplus_3";
    struct rpc_err clnt_err;


    /* the RPC call */
    res = nfsproc3_readdirplus_3(&args, client);

    if (res) {
        /* loop through results, might take multiple calls for the whole directory */
        while (res) {
            if (res->status == NFS3_OK) {
                /* check to see if the cookieverf has changed
                 * this could mean the directory has been modified underneath us
                 * some servers (Linux) always send an empty one
                 */
                if (memcmp(args.cookieverf, emptyverf, NFS3_COOKIEVERFSIZE == 0)) {
                    /* our cookieverf is empty, this happens on the first request */
                    /* or with servers that never return a cookieverf like Linux */
                    /* check if the server has sent us a different cookieverf */
                    /* TODO do we need to track if this is the first request? what if it's been zero for a few requests then we get a cookieverf? */
                    if (memcmp(res->READDIRPLUS3res_u.resok.cookieverf, emptyverf, NFS3_COOKIEVERFSIZE) != 0) {
                        /* copy the server's cookieverf to use in subsequent requests */
                        memcpy(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE);
                    }
                } else {
                    if (memcmp(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE) != 0) {
                        /* TODO also print directory mtime */
                        /* TODO print cookieverf before and after, need a function like nfs_fh3_to_string() to print hex bytes */
                        fprintf(stderr, "%s: %s cookieverf changed!\n", host, fh->path);
                        /* now what, restart reading the directory or just keep charging ahead? */
                    }
                }

                res_entry = res->READDIRPLUS3res_u.resok.reply.entries;

                /* loop through the directory entries in the RPC result */
                while (res_entry) {
                    /* first check for hidden files */
                    if (cfg.listdot == 0 && res_entry->name[0] == '.') {
                        /* skip adding it to the list */
                        res_entry = res_entry->nextentry;
                        continue;
                    }

                    /* allocate a new entrypluslink and copy the data from the entryplus result */
                    current->next = calloc(1, sizeof(entrypluslink3));
                    current = current->next;
                    current->next = NULL;

                    /* copy the entry into the output list */
                    XDR_COPY(entryplus3, current, res_entry);

                    /* if it's a directory print a trailing slash (like ls -F) */
                    /* TODO this seems to be 0 sometimes */
                    if (current->name_attributes.post_op_attr_u.attributes.type == NF3DIR) {
                        /* make space for the filename plus / plus NULL */
                        current->name = calloc(strlen(res_entry->name) + 2, sizeof(char));
                        strncpy(current->name, res_entry->name, strlen(res_entry->name));
                        /* add a trailing slash */
                        current->name[strlen(res_entry->name)] = '/';
                    /* check for symlinks and do a READLINK */
                    } else if (current->name_attributes.post_op_attr_u.attributes.type == NF3LNK) {
                        /* use the filehandle from the current result entry for the readlink */
                        current->symlink = do_readlink(client, host, fh->path, current->name_handle.post_op_fh3_u.handle);
                        current->name = strdup(res_entry->name);
                    /* not a directory or link, just use the received filename */
                    } else {
                        current->name = strdup(res_entry->name);
                    }

                    /* update the directory cookie in case we have to make another call for more entries */
                    /* our position in the directory listing should always increase */
                    if (args.cookie < res_entry->cookie) {
                        args.cookie = res_entry->cookie;
                    } else {
                        /* copy the warning message from the Linux kernel */
                        fprintf(stderr, "directory %s:%s contains a readdirplus loop. Offending cookie: %llu\n", host, fh->path, (long long unsigned)res_entry->cookie);

                        /* make sure we don't try and do another readdirplus */
                        res->READDIRPLUS3res_u.resok.reply.eof = 1;

                        /* stop processing more entries from this directory */
                        break;
                    }

                    /* terminate the list from the server, we use the "next" member in entrypluslist3 for our own iteration */
                    current->nextentry = NULL;

                    /* go to the next directory entry from the server */
                    res_entry = res_entry->nextentry;
                }

                /* check for the end of directory */
                if (res->READDIRPLUS3res_u.resok.reply.eof == 0) {
                    /* do another RPC call for more entries */
                    /* TODO does this need a copy? */
                    memcpy(args.cookieverf, res->READDIRPLUS3res_u.resok.cookieverf, NFS3_COOKIEVERFSIZE);
                    /* free the previous result */
                    xdr_free((xdrproc_t)xdr_READDIRPLUS3res, (char *)res);
                    /* new RPC call */
                    res = nfsproc3_readdirplus_3(&args, client);

                    if (res == NULL) {
                        clnt_perror(client, proc);
                    }

                    continue;
                }
            /* !NFS3_OK */
            } else {
                /* TODO check for NFS3ERR_BAD_COOKIE which means the directory changed underneath us */
                /* it's a file, do a getattr instead */
                if (res->status == NFS3ERR_NOTDIR) {
                    /* do_getattr() can call do_readdirplus() but only if it finds a directory, so this shouldn't loop */
                    current->next = do_getattr(client, host, fh);
                } else {
                    fprintf(stderr, "%s:%s: ", host, fh->path);
                    clnt_geterr(client, &clnt_err);
                    clnt_err.re_status ? clnt_perror(client, proc) : nfs_perror(res->status, proc);
                }
            }

            /* free the result */
            xdr_free((xdrproc_t)xdr_READDIRPLUS3res, (char *)res);
            res = NULL;
        } /* while (res) */

    } else { /* if (res) */
        clnt_perror(client, proc);
    }  

    return dummy.next;
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
            /* Unknown type - this shows up for /proc, /dev, /sys etc */
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
    entrypluslink3 *current;
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
    /* pointer to which hostname string to use */
    char *host_p;
    /* pointer to which filename string to use */
    char *name_p;
    char *symlink = NULL;
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
        /* which name to use, IP address or hostname */
        host_p = cfg.display_ips ? target->ip_address : target->name;

        /* find the longest hostname */
        maxhost = strlen(host_p) > maxhost ? strlen(host_p) : maxhost;

        fh = target->filehandles;

        while (fh) {
            current = fh->entries;

            while (current) {
                count++;

                /* shortcut */
                attributes = current->name_attributes.post_op_attr_u.attributes;

                maxlinks = attributes.nlink > maxlinks ? attributes.nlink : maxlinks;
                maxsize = attributes.size > maxsize ? attributes.size : maxsize;

                /* TODO cache this ! */
                passwd = getpwuid(attributes.uid);
                maxuser = strlen(passwd->pw_name) > maxuser ? strlen(passwd->pw_name) : maxuser;

                /* TODO cache this ! */
                group = getgrgid(attributes.gid);
                maxgroup = strlen(group->gr_name) > maxgroup ? strlen(group->gr_name) : maxgroup;

                current = current->next;
            } /* while (current) */

            fh = fh->next;
        } /* while (fh) */

        target = target->next;
    } /* while (target) */

    /* calculate the maximum string widths */
    maxlinks = floor(log10(abs(maxlinks))) + 1;
    maxsize  = floor(log10(abs(maxsize))) + 1;

    /* reset to start of target list */
    target = targets;

    /* now loop through and print each entry */
    while(target) {
        fh = target->filehandles;

        /* which name to use, IP address or hostname */
        host_p = cfg.display_ips ? target->ip_address : target->name;

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

                if (attributes.type == NF3LNK) {
                    /* TODO just allocate to name_p? */
                    asprintf(&symlink, "%s -> %s", current->name, current->symlink);
                    name_p = symlink;
                } else {
                    name_p = current->name;
                }

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
                    (int)maxhost, host_p,
                    /* filename */
                    name_p);

                /* TODO */
                //free(symlink);

                current = current->next;
            }

            fh = fh->next;
        }

        target = target->next;
    }

    return count;
}


/* print an NFS filehandle as a JSON object */
/* maybe make a generic struct like sockaddr? */
/* TODO take a target_t instead of individual strings */
void print_nfs_fh3(char *host, char *ip_address, char *path, char *file_name, nfs_fh3 file_handle, const unsigned long usec) {
    /* filehandle string */
    char *fh;
    /* output string */
    char *my_json_string;
    /* path + filename */
    char *mypath;
    /* new object for output */
    JSON_Value  *json_root = json_value_init_object();
    JSON_Object *json_obj  = json_value_get_object(json_root);

    json_object_set_string(json_obj, "host", host);
    json_object_set_string(json_obj, "ip", ip_address);

    /* check if the path needs a separator */
    /* TODO use a static string */
    if (path[strlen(path) - 1] != '/') {
        asprintf(&mypath, "%s/%s", path, file_name);
    } else {
        asprintf(&mypath, "%s%s", path, file_name);
    }
    json_object_set_string(json_obj, "path", mypath);
    free(mypath);

    json_object_set_number(json_obj, "usec", usec);

    fh = nfs_fh3_to_string(file_handle);
    json_object_set_string(json_obj, "filehandle", fh);
    free(fh);

    my_json_string = json_serialize_to_string(json_root);
    printf("%s\n", my_json_string);
    json_free_serialized_string(my_json_string);

}


/* loop through a list of directory entries printing a JSON filehandle for each */
int print_filehandles(targets_t *target, struct nfs_fh_list *fh, const unsigned long usec) {
    entrypluslink3 *current = fh->entries;
    int count = 0;

    while (current) {
        count++;

        /* if there is no filehandle (/dev, /proc, etc) don't print */
        /* none of the other utilities can do anything without a filehandle */
        if (current->name_handle.post_op_fh3_u.handle.data.data_len) {
            print_nfs_fh3(target->name, target->ip_address, fh->path, current->name, current->name_handle.post_op_fh3_u.handle, usec);
        }

        current = current->next;
    }

    return count;
}


int main(int argc, char **argv) {
    int ch; /* getopt */
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
    struct timespec call_start, call_end, call_elapsed;
    struct timespec loop_start, loop_end, loop_elapsed, sleepy;
    /* default to 1Hz */
    struct timespec sleep_time = {
        .tv_sec = 1
    };
    unsigned long hertz = NFS_HERTZ;
    unsigned long usec = 0;
    /* count of requests sent */
    unsigned long ls_sent = 0;
    /* count of successful requests */
    unsigned long ls_ok   = 0;

    cfg = CONFIG_DEFAULT;

    while ((ch = getopt(argc, argv, "aAc:dhH:lLS:Tv")) != -1) {
        switch(ch) {
            /* list hidden files */
            case 'a':
                cfg.listdot = 1;
                break;
            /* display IPs instead of hostnames */
            case 'A':
                cfg.display_ips = 1;
                break;
            case 'c':
                if (cfg.loop) {
                    fatal("Can't specify both -l and -c!\n");
                }

                cfg.count = strtoul(optarg, NULL, 10);

                if (cfg.count == 0 || cfg.count == ULONG_MAX) {
                   fatal("Zero count, nothing to do!\n");
                }
                break;
            /* display directories not contents */
            case 'd':
                cfg.listdir = 1;
                break;
            /* polling frequency */
            case 'H':
                /* TODO check for reasonable values */
                hertz = strtoul(optarg, NULL, 10);
                break;
            /* long listing */
            case 'l':
                cfg.format = ls_longform;
                break;
            /* loop */
            case 'L':
                cfg.loop = 1;
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

    /* set default to JSON if nothing else was specified */
    if (cfg.format == ls_unset) {
        cfg.format = ls_json;
    }

    /* calculate the sleep_time based on the frequency */
    /* this doesn't support frequencies lower than 1Hz */
    if (hertz > 1) {
        sleep_time.tv_sec = 0;
        /* nanoseconds */
        sleep_time.tv_nsec = 1000000000 / hertz;
    }

    /* no arguments, use stdin */
    while (getline(&input_fh, &input_len, stdin) != -1) {
        current = parse_fh(targets, input_fh, 0, 0, ping);
    }

    /* skip the dummy entry */
    targets = targets->next;

    /* set timezone for date output */
    /* TODO only with long_listing set? */
    tzset();

    /* listen for ctrl-c */
    signal(SIGINT, sigint_handler);

    /* main loop */
    while (1) {
        current = targets;

#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &loop_start);
#else  
        clock_gettime(CLOCK_MONOTONIC, &loop_start);
#endif 

        /* send RPCs to each filehandle in each target */
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
    #ifdef CLOCK_MONOTONIC_RAW
                    clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
    #else
                    clock_gettime(CLOCK_MONOTONIC, &call_start);
    #endif

                    /* if we're listing directories, do a getattr no matter what */
                    /* check for a trailing slash to see if we need to do readdirplus or getattr */
                    if (cfg.listdir || filehandle->path[strlen(filehandle->path) - 1] != '/') {
                        filehandle->entries = do_getattr(current->client, current->name, filehandle);
                    } else {
                        /* store the directory entries in the filehandle list */
                        filehandle->entries = do_readdirplus(current->client, current->name, filehandle);
                    }

    #ifdef CLOCK_MONOTONIC_RAW
                    clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
    #else
                    clock_gettime(CLOCK_MONOTONIC, &call_end);
    #endif

                    ls_sent++;
                    filehandle->sent++;

                    /* check if we got a result */
                    if (filehandle->entries) {
                        ls_ok++;
                    }

                    /* calculate elapsed microseconds */
                    timespecsub(&call_end, &call_start, &call_elapsed);
                    usec = ts2us(call_elapsed);

                    /*
                    TODO make an outputs enum with json/longform/ping/fping/graphite/statsd
                    print_output function to switch
                    iterate through entries and call print_nfs_fh3 if json
                    some option to print raw request results in JSON including cookie (-d?)
                    */

                    if (cfg.format == ls_json) {
                        print_filehandles(current, filehandle, usec);
                    }

                    filehandle = filehandle->next;
                } /* while (filehandle) */
            }

            current = current->next;
        } /* while (current) */

        /* measure how long the current round took, and subtract that from the sleep time */
        /* this keeps us on the polling frequency */
#ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &loop_end);
#else  
        clock_gettime(CLOCK_MONOTONIC, &loop_end);
#endif 
        timespecsub(&loop_end, &loop_start, &loop_elapsed);
        debug("Polling took %lld.%.9lds\n", (long long)loop_elapsed.tv_sec, loop_elapsed.tv_nsec);

        /* pass the whole list for printing long listing */
        if (cfg.format == ls_longform) {
            print_long_listing(targets);
        }

        if (quitting) {
            break;
        }

        /* only sleep if looping or counting */
        /* check the count against the first filehandle in the first target */
        if (cfg.loop || (cfg.count && targets->filehandles->sent < cfg.count)) {
            /* don't sleep if we went over the sleep_time */
            if (timespeccmp(&loop_elapsed, &sleep_time, >)) {
                debug("Slow poll, not sleeping\n");
            /* sleep between rounds */
            } else {
                timespecsub(&sleep_time, &loop_elapsed, &sleepy);
                debug("Sleeping for %lld.%.9lds\n", (long long)sleepy.tv_sec, sleepy.tv_nsec);
                nanosleep(&sleepy, NULL);
            }
        } else {
            break;
        }
    } /* while (1) */

    /* return success if all requests came back ok */
    if (ls_sent && ls_sent == ls_ok) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
