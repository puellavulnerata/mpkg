#include <stdlib.h>

#include <pkg.h>

void close_read_stream( read_stream *r ) {
  if ( r ) {
    r->close( r->private );
    free( r );
  }
}

void close_write_stream( write_stream *w ) {
  if ( w ) {
    w->close( w->private );
    free( w );
  }
}

long read_from_stream( read_stream *r, void *buf, long len ) {
  if ( r )
    return r->read( r->private, buf, len );
  else return STREAMS_BAD_STREAM;
}

long write_to_stream( write_stream *w, void *buf, long len ) {
  if ( w )
    return w->write( w->private, buf, len );
  else return STREAMS_BAD_STREAM;
}
