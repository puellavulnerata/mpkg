#ifdef COMPRESSION_GZIP

#include <pkg.h>

#include <stdlib.h>

#include <zlib.h>

#define CHUNK_SIZE 16384

static void close_gzip_read( void * );
static void close_gzip_write( void * );
static long read_gzip( void *, void *, long );
static long write_gzip( void *, void *, long );

typedef struct {
  union {
    FILE *fp;
    union {
      read_stream *rs;
      write_stream *ws;
    } streams;
  } u;
  z_stream strm;
  void *buf;
  int error;
  int use_stream;
} gzip_private;

static void close_gzip_read( void *vp ) {
  gzip_private *p;

  p = (gzip_private *)vp;
  if ( p ) {
    inflateEnd( &(p->strm) );
    if ( p->buf ) free( p->buf );
    if ( !(p->use_stream) ) {
      if ( p->u.fp ) fclose( p->u.fp );
    }
    free( p );
  }
}

static void close_gzip_write( void *vp ) {
  gzip_private *p;
  size_t result;
  int def_status;

  p = (gzip_private *)vp;
  if ( p ) {
    if ( p->error == 0 && p->buf &&
	 ( ( p->use_stream && p->u.streams.ws ) ||
	   ( !(p->use_stream) && p->u.fp ) ) ) {
      p->strm.next_in = Z_NULL;
      p->strm.avail_in = 0;
      do {
	if ( p->strm.avail_out == 0 ) {
	  if ( p->use_stream )
	    result = write_to_stream( p->u.streams.ws, p->buf, CHUNK_SIZE );
	  else
	    result = fwrite( p->buf, 1, CHUNK_SIZE, p->u.fp );
	  if ( result == CHUNK_SIZE ) {
	    p->strm.next_out = p->buf;
	    p->strm.avail_out = CHUNK_SIZE;
	  }
	  else {
	    /* We couldn't write all the output we had */
	    p->error = 1;
	    break;
	  }
	}
	def_status = deflate( &(p->strm), Z_FINISH );
	if ( !( def_status == Z_OK ||
		def_status == Z_STREAM_END ) ) {
	  p->error = 1;
	  break;
	}
      } while ( def_status != Z_STREAM_END );
      /* Write out the last partial block */
      if ( def_status == Z_STREAM_END ) {
	if ( p->use_stream )
	  result = write_to_stream( p->u.streams.ws, p->buf,
				    CHUNK_SIZE - p->strm.avail_out );
	else
	  result = fwrite( p->buf, 1,
			   CHUNK_SIZE - p->strm.avail_out, p->u.fp );
	if ( result != CHUNK_SIZE - p->strm.avail_out ) p->error = 1;
      }
    }
    deflateEnd( &(p->strm) );
    if ( p->buf ) free( p->buf );
    if ( !(p->use_stream) ) {
      if ( p->u.fp ) fclose( p->u.fp );
    }
    free( p );
  }
}

read_stream * open_read_stream_from_stream_gzip( read_stream *rs ) {
  read_stream *r;
  gzip_private *p;
  int status;

  if ( rs ) {
    r = malloc( sizeof( *r ) );
    if ( r ) {
      p = malloc( sizeof( *p ) );
      if ( p ) {
	r->private = (void *)p;
	p->error = 0;
	p->buf = malloc( CHUNK_SIZE );
	if ( p->buf ) {
	  p->strm.zalloc = Z_NULL;
	  p->strm.zfree = Z_NULL;
	  p->strm.opaque = Z_NULL;
	  p->strm.avail_in = 0;
	  p->strm.next_in = Z_NULL;
	  /* 15 bit window with gzip format */
	  status = inflateInit2( &(p->strm), 31 );
	  if ( status == Z_OK ) {
	    p->use_stream = 1;
	    p->u.streams.rs = rs;
	    r->close = close_gzip_read;
	    r->read = read_gzip;
	  }
	  else {
	    free( p->buf );
	    free( p );
	    free( r );
	    r = NULL;
	  }
	}
	else {
	  free( p );
	  free( r );
	  r = NULL;
	}
      }
      else {
	free( r );
	r = NULL;
      }
    }
  }
  else r = NULL;
  return r;
}

read_stream * open_read_stream_gzip( const char *filename ) {
  read_stream *r;
  gzip_private *p;
  int status;

  if ( filename ) {
    r = malloc( sizeof( *r ) );
    if ( r ) {
      p = malloc( sizeof( *p ) );
      if ( p ) {
	r->private = (void *)p;
	p->error = 0;
	p->buf = malloc( CHUNK_SIZE );
	if ( p->buf ) {
	  p->strm.zalloc = Z_NULL;
	  p->strm.zfree = Z_NULL;
	  p->strm.opaque = Z_NULL;
	  p->strm.avail_in = 0;
	  p->strm.next_in = Z_NULL;
	  /* 15 bit window with gzip format */
	  status = inflateInit2( &(p->strm), 31 );
	  if ( status == Z_OK ) {
	    p->use_stream = 0;
	    p->u.fp = fopen( filename, "r" );
	    if ( p->u.fp ) {
	      r->close = close_gzip_read;
	      r->read = read_gzip;
	    }
	    else {
	      inflateEnd( &(p->strm) );
	      free( p->buf );
	      free( p );
	      free( r );
	      r = NULL;
	    }
	  }
	  else {
	    free( p->buf );
	    free( p );
	    free( r );
	    r = NULL;
	  }
	}
	else {
	  free( p );
	  free( r );
	  r = NULL;
	}
      }
      else {
	free( r );
	r = NULL;
      }
    }
  }
  else r = NULL;
  return r;
}

write_stream * open_write_stream_from_stream_gzip( write_stream *ws ) {
  write_stream *w;
  gzip_private *p;
  int status;

  if ( ws ) {
    w = malloc( sizeof( *w ) );
    if ( w ) {
      p = malloc( sizeof( *p ) );
      if ( p ) {
	w->private = (void *)p;
	p->error = 0;
	p->buf = malloc( CHUNK_SIZE );
	if ( p->buf ) {
	  p->strm.zalloc = Z_NULL;
	  p->strm.zfree = Z_NULL;
	  p->strm.opaque = Z_NULL;
	  /* 15 bit window with gzip format */
	  status = deflateInit2( &(p->strm),
				 9,
				 Z_DEFLATED,
				 31,
				 9,
				 Z_DEFAULT_STRATEGY );
	  if ( status == Z_OK ) {
	    p->strm.next_out = p->buf;
	    p->strm.avail_out = CHUNK_SIZE;
	    p->use_stream = 1;
	    p->u.streams.ws = ws;
	    w->close = close_gzip_write;
	    w->write = write_gzip;
	  }
	  else {
	    free( p->buf );
	    free( p );
	    free( w );
	    w = NULL;
	  }
	}
	else {
	  free( p );
	  free( w );
	  w = NULL;
	}
      }
      else {
	free( w );
	w = NULL;
      }
    }
  }
  else w = NULL;
  return w;
}

write_stream * open_write_stream_gzip( const char *filename ) {
  write_stream *w;
  gzip_private *p;
  int status;

  if ( filename ) {
    w = malloc( sizeof( *w ) );
    if ( w ) {
      p = malloc( sizeof( *p ) );
      if ( p ) {
	w->private = (void *)p;
	p->error = 0;
	p->buf = malloc( CHUNK_SIZE );
	if ( p->buf ) {
	  p->strm.zalloc = Z_NULL;
	  p->strm.zfree = Z_NULL;
	  p->strm.opaque = Z_NULL;
	  /* 15 bit window with gzip format */
	  status = deflateInit2( &(p->strm),
				 9,
				 Z_DEFLATED,
				 31,
				 9,
				 Z_DEFAULT_STRATEGY );
	  if ( status == Z_OK ) {
	    p->strm.next_out = p->buf;
	    p->strm.avail_out = CHUNK_SIZE;
	    p->use_stream = 0;
	    p->u.fp = fopen( filename, "w" );
	    if ( p->u.fp ) {
	      w->close = close_gzip_write;
	      w->write = write_gzip;
	    }
	    else {
	      deflateEnd( &(p->strm) );
	      free( p->buf );
	      free( p );
	      free( w );
	      w = NULL;
	    }
	  }
	  else {
	    free( p->buf );
	    free( p );
	    free( w );
	    w = NULL;
	  }
	}
	else {
	  free( p );
	  free( w );
	  w = NULL;
	}
      }
      else {
	free( w );
	w = NULL;
      }
    }
  }
  else w = NULL;
  return w;
}

static long read_gzip( void *vp, void *buf, long len ) {
  gzip_private *p;
  size_t result;
  long status;
  int inf_status;

  p = (gzip_private *)vp;
  if ( p && buf && len > 0 ) {
    if ( p->error == 0 ) {
      status = 0;
      p->strm.next_out = buf;
      p->strm.avail_out = len;
      while ( p->strm.avail_out > 0 ) {
	if ( p->strm.avail_in == 0 ) {
	  p->strm.next_in = p->buf;
	  if ( p->use_stream ) {
	    result = read_from_stream( p->u.streams.rs, p->buf, CHUNK_SIZE );
	    if ( result > 0 ) p->strm.avail_in = result;
	    else if ( result == 0 ) break; /* EOF */
	    else {
	      status = STREAMS_INTERNAL_ERROR;
	    }
	  }
	  else {
	    result = fread( p->buf, 1, CHUNK_SIZE, p->u.fp );
	    if ( result > 0 ) p->strm.avail_in = result;
	    else {
	      if ( ferror( p->u.fp ) ) status = STREAMS_INTERNAL_ERROR;
	      break;
	    }
	  }
	}
	/* We flush if avail_in < CHUNK_SIZE, because we probably had EOF */
	inf_status = inflate( &(p->strm),
			      ( p->strm.avail_in < CHUNK_SIZE ) ?
			      Z_SYNC_FLUSH : Z_NO_FLUSH );
	if ( inf_status == Z_STREAM_END ) break;
	else if ( inf_status != Z_OK ) {
	  p->error = 1;
	  status = STREAMS_INTERNAL_ERROR;
	  break;
	}
      }
      if ( status >= 0 ) status = len - p->strm.avail_out;
      return status;
    }
    else return STREAMS_INTERNAL_ERROR;
  }
  else return STREAMS_BAD_ARGS;
}

static long write_gzip( void *vp, void *buf, long len ) {
  gzip_private *p;
  size_t result;
  long status;
  int def_status;

  p = (gzip_private *)vp;
  if ( p && buf && len > 0 ) {
    if ( p->error == 0 ) {
      status = 0;
      p->strm.next_in = buf;
      p->strm.avail_in = len;
      while ( p->strm.avail_in > 0 ) {
	if ( p->strm.avail_out == 0 ) {
	  if ( p->use_stream )
	    result = write_to_stream( p->u.streams.ws, p->buf, CHUNK_SIZE );
	  else
	    result = fwrite( p->buf, 1, CHUNK_SIZE, p->u.fp );
	  if ( result == CHUNK_SIZE ) {
	    p->strm.next_out = p->buf;
	    p->strm.avail_out = CHUNK_SIZE;
	  }
	  else {
	    /* We couldn't write all the output we had */
	    status = STREAMS_INTERNAL_ERROR;
	    p->error = 1;
	    break;
	  }
	}
	/* Always Z_NO_FLUSH here, we do Z_FINISH in close */
	def_status = deflate( &(p->strm), Z_NO_FLUSH );
	if ( def_status != Z_OK ) {
	  p->error = 1;
	  status = STREAMS_INTERNAL_ERROR;
	  break;
	}
      }
      if ( status >= 0 ) status = len - p->strm.avail_in;
      return status;
    }
    else return STREAMS_INTERNAL_ERROR;
  }
  else return STREAMS_BAD_ARGS;
}

#endif /* COMPRESS_GZIP */
