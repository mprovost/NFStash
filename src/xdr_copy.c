/* from http://stackoverflow.com/questions/27000237/does-this-rpc-xdr-copy-make-sense */

#include <string.h>

#include "xdr_copy.h"

#define XDR_BUFFER_SIZE   ( 100 * 1024 )
#define XDR_BUFFER_DELTA  ( 10 * 1024 )

static char*    xdr_buffer = NULL ;
static unsigned xdr_buffer_size = 0 ;

static char* xdr_buffer_realloc( const unsigned delta )
{
   char* rv = realloc( xdr_buffer, xdr_buffer_size + delta ) ;

   if ( rv )
   {
      xdr_buffer_size += delta ;
      xdr_buffer = rv ;
   }

   return rv ;
}

static char* get_xdr_buffer()
{
   if ( !xdr_buffer )
      xdr_buffer = xdr_buffer_realloc( XDR_BUFFER_SIZE ) ;

  return xdr_buffer ;
}

bool_t xdr_copy( xdrproc_t proc, char* d, char* s )
{
   XDR   x ;
   char* buffer = get_xdr_buffer() ;

   while ( buffer )
   {
      xdrmem_create( &x, buffer, xdr_buffer_size, XDR_ENCODE ) ;
      if (( *proc )( &x, ( void* )s ))
      {
         xdr_destroy( &x ) ;
         xdrmem_create( &x, buffer, xdr_buffer_size, XDR_DECODE ) ;
         ( *proc )( &x, ( void* )d ) ;
         break ;
      }
      else
      {
         buffer = xdr_buffer_realloc( XDR_BUFFER_DELTA ) ;
         xdr_destroy( &x ) ;
      }
   }

   if ( buffer )
   {
      xdr_destroy( &x ) ;
      return 1 ;
   }
   else
      return 0 ;
}

bool_t xdr_copy_( xdrproc_t proc, char* d, char* s, const unsigned size )
{
   memset( d, 0, size ) ;
   return xdr_copy( proc, d, s ) ;
}
