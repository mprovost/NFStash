#include <nfsping.h>

size_t parse_fh(char *, fsroots_t **);
char* reverse_fqdn(char *);
unsigned long tv2us(struct timeval);
unsigned long tv2ms(struct timeval);
void ms2tv(struct timeval *, unsigned long);
void ms2ts(struct timespec *, unsigned long);
unsigned long ts2ms(struct timespec);
