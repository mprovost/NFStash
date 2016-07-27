/* from http://stackoverflow.com/questions/27000237/does-this-rpc-xdr-copy-make-sense */

#include <rpc/rpc.h>
#include <rpc/xdr.h>

#define XDR_COPY( T, d, s  ) xdr_copy_(( xdrproc_t  )xdr_##T, ( char*  )d, ( const char*  )s, sizeof( T  ))
extern bool_t xdr_copy( xdrproc_t proc, char* d, const char* s  ) ;
extern bool_t xdr_copy_( xdrproc_t proc, char* d, const char* s, const unsigned size  ) ;
