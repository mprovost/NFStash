#ifndef UTIL_H
#define UTIL_H

#include "nfsping.h"
#include "parson/parson.h"

void sigint_handler(int);
int nfs_perror(nfsstat3);
nfs_fh_list *parse_fh(char *);
int nfs_fh3_to_string(char *, nfs_fh3);
int print_nfs_fh3(struct sockaddr *, char *, char *, nfs_fh3);
char* reverse_fqdn(char *);
targets_t *make_target(char *, const struct addrinfo *, uint16_t, int, int, unsigned long, enum outputs);
targets_t *init_target(uint16_t, unsigned long, enum outputs);
targets_t *copy_target(targets_t *, unsigned long, enum outputs);
targets_t *append_target(targets_t **, targets_t *);
unsigned long tv2us(struct timeval);
unsigned long tv2ms(struct timeval);
void ms2tv(struct timeval *, unsigned long);
void ms2ts(struct timespec *, unsigned long);
unsigned long ts2us(const struct timespec);
unsigned long ts2ms(struct timespec);
unsigned long long ts2ns(const struct timespec);

#endif /* UTIL_H */
