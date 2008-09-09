#ifndef __STREAMS_H__
#define __STREAMS_H__

#define STREAMS_EOF 0
#define STREAMS_BAD_STREAM -1
#define STREAMS_BAD_ARGS -2
#define STREAMS_INTERNAL_ERROR -3

typedef struct {
  void *private;

  void (*close)( void * );
  long (*read)( void *, void *, long );
} read_stream;

typedef struct {
  void *private;

  void (*close)( void * );
  long (*write)( void *, void *, long );
} write_stream;

void close_read_stream( read_stream * );
void close_write_stream( write_stream * );
long read_from_stream( read_stream *, void *, long );
long write_to_stream( write_stream *, void *, long );

read_stream * open_read_stream_none( const char * );
write_stream * open_write_stream_none( const char * );

#ifdef COMPRESSION_GZIP
read_stream * open_read_stream_gzip( const char * );
write_stream * open_write_stream_gzip( const char * );
read_stream * open_read_stream_from_stream_gzip( read_stream * );
write_stream * open_write_stream_from_stream_gzip( write_stream * );
#endif /* COMPRESSION_GZIP */

#ifdef COMPRESSION_BZIP2
read_stream * open_read_stream_bzip2( const char * );
write_stream * open_write_stream_bzip2( const char * );
read_stream * open_read_stream_from_stream_bzip2( read_stream * );
write_stream * open_write_stream_from_stream_bzip2( write_stream * );
#endif /* COMPRESSION_BZIP2 */

#endif
