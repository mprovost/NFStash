#ifndef UTIL_H
#define UTIL_H

#include "nfsping.h"
#include "parson/parson.h"

void sigint_handler(int);
int nfs_perror(nfsstat3, const char *);
targets_t *parse_fh(targets_t *, char *, uint16_t, unsigned long, enum outputs);
char *nfs_fh3_to_string(nfs_fh3);
char* reverse_fqdn(char *);
targets_t *make_target(char *, const struct addrinfo *, uint16_t, int, int, unsigned long, enum outputs);
targets_t *init_target(uint16_t, unsigned long, enum outputs);
targets_t *copy_target(targets_t *, unsigned long, enum outputs);
targets_t *append_target(targets_t **, targets_t *);
nfs_fh_list *nfs_fh_list_new(targets_t *, unsigned long);
targets_t *find_target_by_ip(targets_t *, struct sockaddr_in *);
targets_t *find_or_make_target(targets_t *, struct sockaddr_in *, uint16_t, unsigned long, enum outputs);
unsigned long tv2us(struct timeval);
unsigned long tv2ms(struct timeval);
void ms2tv(struct timeval *, unsigned long);
void ms2ts(struct timespec *, unsigned long);
unsigned long ts2us(const struct timespec);
unsigned long ts2ms(struct timespec);
unsigned long long ts2ns(const struct timespec);

#endif /* UTIL_H */
