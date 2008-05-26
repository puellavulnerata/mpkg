#ifndef __COMPRESSION_H__
#define __COMPRESSION_H__

#define COMP_EOF 0
#define COMP_BAD_STREAM -1
#define COMP_BAD_ARGS -2
#define COMP_INTERNAL_ERROR -3

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

read_stream * open_read_stream_none( char * );
write_stream * open_write_stream_none( char * );

#ifdef COMPRESSION_GZIP
read_stream * open_read_stream_gzip( char * );
write_stream * open_write_stream_gzip( char * );
read_stream * open_read_stream_from_stream_gzip( read_stream * );
write_stream * open_write_stream_from_stream_gzip( write_stream * );
#endif /* COMPRESSION_GZIP */

#ifdef COMPRESSION_BZIP2
read_stream * open_read_stream_bzip2( char * );
write_stream * open_write_stream_bzip2( char * );
read_stream * open_read_stream_from_stream_bzip2( read_stream * );
write_stream * open_write_stream_from_stream_bzip2( write_stream * );
#endif /* COMPRESSION_BZIP2 */

#endif
