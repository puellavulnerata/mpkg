#ifndef __MD5_H__
#define __MD5_H__

#include <stdint.h>

#include <streams.h>

/*
 * We process 512-bit (64-byte) blocks at a time
 */

#define MD5_BLOCK_LEN 64
#define MD5_RESULT_LEN 16

typedef struct {
  enum {
    MD5_RUNNING,
    MD5_DONE
  } state;

  uint64_t byte_count;

  uint8_t curr_block;
  uint8_t block[MD5_BLOCK_LEN];

  uint32_t h[4];

  write_stream *ws;
} md5_state;

void close_md5( md5_state * );
int file_hash_matches( const char *, uint8_t * );
int get_file_hash( const char *, uint8_t * );
int get_md5_result( md5_state *, uint8_t * );
write_stream * get_md5_ws( md5_state * );
md5_state * start_new_md5( void );

#endif
