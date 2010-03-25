#include <rpc/rpc.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <math.h>

#define NFS_PROGRAM 100003
#define NFS_PORT    2049
#define NFSPROC_NULL 0
