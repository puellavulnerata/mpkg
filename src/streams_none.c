#include <stdio.h>
#include <stdlib.h>

#include <pkg.h>

static void close_none( void * );
static long read_none( void *, void *, long );
static long write_none( void *, void *, long );

static void close_none( void *p ) {
  FILE *fp;

  fp = (FILE *)p;
  if ( fp ) fclose( fp );
}

read_stream * open_read_stream_none( const char *filename ) {
  read_stream *r;
  FILE *fp;

  r = malloc( sizeof( *r ) );
  if ( r ) {
    fp = fopen( filename, "r" );
    if ( fp ) {
      r->private = (void *)fp;
      r->close = close_none;
      r->read = read_none;
    }
    else {
      free( r );
      r = NULL;
    }
  }
  return r;
}

write_stream * open_write_stream_none( const char *filename ) {
  write_stream *w;
  FILE *fp;

  w = malloc( sizeof( *w ) );
  if ( w ) {
    fp = fopen( filename, "w" );
    if ( fp ) {
      w->private = (void *)fp;
      w->close = close_none;
      w->write = write_none;
    }
    else {
      free( w );
      w = NULL;
    }
  }
  return w;

}

static long read_none( void *p, void *buf, long len ) {
  FILE *fp;
  size_t result;

  fp = (FILE *)p;
  if ( fp && buf && len > 0 ) {
    result = fread( buf, 1, len, fp );
    if ( result < 0 ) return STREAMS_INTERNAL_ERROR;
    else if ( result < len ) {
      if ( result > 0 ) return result;
      else if ( feof( fp ) ) return STREAMS_EOF;
      else return STREAMS_INTERNAL_ERROR;
    }
    else return result;
  }
  else return STREAMS_BAD_ARGS;
}

static long write_none( void *p, void *buf, long len ) {
  FILE *fp;
  size_t result;

  fp = (FILE *)p;
  if ( fp && buf && len > 0 ) {
    result = fwrite( buf, 1, len, fp );
    if ( result < 0 ) return STREAMS_INTERNAL_ERROR;
    else if ( result < len ) {
      if ( result > 0 ) return result;
      else if ( feof( fp ) ) return STREAMS_EOF;
      else return STREAMS_INTERNAL_ERROR;
    }
    else return result;
  }
  else return STREAMS_BAD_ARGS;
}
