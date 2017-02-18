#ifndef UTIL_H
#define UTIL_H

#include "nfsping.h"
#include "parson/parson.h"

void sigint_handler(int);
int nfs_perror(nfsstat3, const char *);
targets_t *parse_fh(targets_t *, char *, uint16_t, struct timeval, unsigned long);
char *nfs_fh3_to_string(nfs_fh3);
char* reverse_fqdn(char *);
struct mount_exports *init_export(struct targets *, char *, unsigned long);
unsigned int make_target(targets_t *, char *, const struct addrinfo *, uint16_t, int, int, int, struct timeval, char *, unsigned long);
targets_t *init_target(uint16_t, struct timeval, unsigned long);
targets_t *copy_target(targets_t *, unsigned long);
targets_t *append_target(targets_t **, targets_t *);
nfs_fh_list *nfs_fh_list_new(targets_t *, unsigned long);
targets_t *find_target_by_ip(targets_t *, struct sockaddr_in *);
targets_t *find_or_make_target(targets_t *, struct sockaddr_in *, uint16_t, struct timeval, unsigned long);
unsigned long tv2us(struct timeval);
unsigned long tv2ms(struct timeval);
void ms2tv(struct timeval *, unsigned long);
void ms2ts(struct timespec *, unsigned long);
unsigned long ts2us(const struct timespec);
unsigned long ts2ms(struct timespec);
unsigned long long ts2ns(const struct timespec);

#endif /* UTIL_H */
