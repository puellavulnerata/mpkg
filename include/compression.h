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

#endif
