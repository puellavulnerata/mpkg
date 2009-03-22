#include <stdint.h>

#include <pkg.h>

#define INIT_H0 0x67452301
#define INIT_H1 0xEFCDAB89
#define INIT_H2 0x98BADCFE
#define INIT_H3 0x10325476

#define MD5_F( x, y, z ) ( ( (x) & (y) ) | ( ( ( ~(x) ) & (z) ) ) )
#define MD5_G( x, y, z ) ( ( (x) & (z) ) | ( (y) & ( ~(z) ) ) )
#define MD5_H( x, y, z ) ( (x) ^ (y) ^ (z) )
#define MD5_I( x, y, z ) ( (y) ^ ( (x) | ( ~(z) ) ) )

#define MD5_ROTLEFT( w, r ) ( ( (w) << (r) ) | ( (w) >> ( 32 - (r) ) ) )

#define MD5_STEP( f, a, b, c, d, x, s, t ) { \
  (a) += f( (b), (c), (d) ) + (x) + (uint32_t)(t); \
  (a) = MD5_ROTLEFT( (a), (s) ); \
  (a) += (b); \
  }

static const uint8_t s[64] = {
  7, 12, 17, 22, 7, 12, 17, 22,
  7, 12, 17, 22, 7, 12, 17, 22,
  5,  9, 14, 20, 5,  9, 14, 20,
  5,  9, 14, 20, 5,  9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23,
  4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21,
  6, 10, 15, 21, 6, 10, 15, 21
};

/*
 * These integer constants are generated by floor(abs(sin(i+1))*2^32)
 * as specified by RFC 1321
 */

static const uint32_t t[64] = {
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
  0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
  0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
  0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
  0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
  0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
  0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
  0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
  0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
  0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
  0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
  0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
  0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static void md5_close( void * );
static void md5_process_block( md5_state *, uint8_t * );
static long md5_write( void *, void *, long );

void close_md5( md5_state *md5 ) {
  if ( md5 ) {
    if ( md5->ws ) free( md5->ws );
    free( md5 );
  }
}

#define MATCH_BUF_LEN 1024

int file_hash_matches( const char *filename, uint8_t *hash ) {
  int result;
  md5_state *md5_ctxt;
  read_stream *file_rs;
  write_stream *md5_ws;
  void *buf;
  long len;
  uint8_t h[HASH_LEN];

  result = 0;
  md5_ctxt = start_new_md5();
  if ( md5_ctxt ) {
    md5_ws = get_md5_ws( md5_ctxt );
    if ( md5_ws ) {
      file_rs = open_read_stream_none( filename );
      if ( file_rs ) {
	buf = malloc( MATCH_BUF_LEN );
	if ( buf ) {
	  while ( ( len = read_from_stream( file_rs, buf,
					    MATCH_BUF_LEN ) ) > 0 ) {
	    write_to_stream( md5_ws, buf, len );
	  }
	  free( buf );
	}
	else result = -4;

	close_read_stream( file_rs );
      }
      else result = -3;

      close_write_stream( md5_ws );
      if ( result == 0 ) {
	if ( get_md5_result( md5_ctxt, h ) == 0 ) {
	  if ( memcmp( h, hash, sizeof( uint8_t ) * HASH_LEN ) == 0 )
	    result = 1;
	  else result = 0;
	}
	else result = -5;
      }
    }
    else result = -2;
    
    close_md5( md5_ctxt );
  }
  else result = -1;

  return result;
}

int get_md5_result( md5_state *md5, uint8_t *out ) {
  if ( md5 && out ) {
    if ( md5->state == MD5_DONE ) {
      out[0] = md5->h[0] & 0xff;
      out[1] = ( md5->h[0] >> 8 ) & 0xff;
      out[2] = ( md5->h[0] >> 16 ) & 0xff;
      out[3] = ( md5->h[0] >> 24 ) & 0xff;
      out[4] = md5->h[1] & 0xff;
      out[5] = ( md5->h[1] >> 8 ) & 0xff;
      out[6] = ( md5->h[1] >> 16 ) & 0xff;
      out[7] = ( md5->h[1] >> 24 ) & 0xff;
      out[8] = md5->h[2] & 0xff;
      out[9] = ( md5->h[2] >> 8 ) & 0xff;
      out[10] = ( md5->h[2] >> 16 ) & 0xff;
      out[11] = ( md5->h[2] >> 24 ) & 0xff;
      out[12] = md5->h[3] & 0xff;
      out[13] = ( md5->h[3] >> 8 ) & 0xff;
      out[14] = ( md5->h[3] >> 16 ) & 0xff;
      out[15] = ( md5->h[3] >> 24 ) & 0xff;
      return 0;
    }
    else return -1;
  }
  else return -1;
}

write_stream * get_md5_ws( md5_state *md5 ) {
  return md5->ws;
}

static void md5_close( void *v ) {
  md5_state *md5;
  uint64_t bits;

  if ( v ) {
    md5 = (md5_state *)v;

    md5->byte_count += md5->curr_block;

    /*
     * Padding:
     *
     * First, a 1 bit, which, since we only implement multiples of
     * 1 byte, is a 0x80 byte (1000000, little endian)
     */

    if ( md5->curr_block == MD5_BLOCK_LEN ) {
      md5_process_block( md5, md5->block );
      md5->curr_block = 0;      
    }
    md5->block[md5->curr_block] = 0x80;
    ++(md5->curr_block);

    /*
     * Next, pad out with zero until we are 64 bits short of 512
     * (curr_block == 56)
     */

    while ( md5->curr_block != 56 ) {
      if ( md5->curr_block == MD5_BLOCK_LEN ) {
	md5_process_block( md5, md5->block );
	md5->curr_block = 0;      
      }
      md5->block[md5->curr_block] = 0;
      ++(md5->curr_block);
    }

    /*
     * Now we fill in the last 64 bits of this block with the bit
     * count as a little-endian 64-bit integer
     */

    bits = md5->byte_count * 8;
    md5->block[56] = bits & 0xff;
    md5->block[57] = ( bits >> 8 ) & 0xff;
    md5->block[58] = ( bits >> 16 ) & 0xff;
    md5->block[59] = ( bits >> 24 ) & 0xff;
    md5->block[60] = ( bits >> 32 ) & 0xff;
    md5->block[61] = ( bits >> 40 ) & 0xff;
    md5->block[62] = ( bits >> 48 ) & 0xff;
    md5->block[63] = ( bits >> 56 ) & 0xff;

    md5_process_block( md5, md5->block );
    md5->state = MD5_DONE;
    md5->ws = NULL;
  }
}

static void md5_process_block( md5_state *md5, uint8_t *block ) {
  uint32_t a, b, c, d;
  uint8_t i;
  uint32_t le_block[16];

  for ( i = 0; i < 16; ++i ) {
    le_block[i] = (uint32_t)(block[4 * i]);
    le_block[i] |= (uint32_t)(block[4 * i + 1] << 8);
    le_block[i] |= (uint32_t)(block[4 * i + 2] << 16);
    le_block[i] |= (uint32_t)(block[4 * i + 3] << 24);
  }

  a = md5->h[0];
  b = md5->h[1];
  c = md5->h[2];
  d = md5->h[3];

  MD5_STEP( MD5_F, a, b, c, d, le_block[0],  s[0],  t[0]  );
  MD5_STEP( MD5_F, d, a, b, c, le_block[1],  s[1],  t[1]  );
  MD5_STEP( MD5_F, c, d, a, b, le_block[2],  s[2],  t[2]  );
  MD5_STEP( MD5_F, b, c, d, a, le_block[3],  s[3],  t[3]  );
  MD5_STEP( MD5_F, a, b, c, d, le_block[4],  s[4],  t[4]  );
  MD5_STEP( MD5_F, d, a, b, c, le_block[5],  s[5],  t[5]  );
  MD5_STEP( MD5_F, c, d, a, b, le_block[6],  s[6],  t[6]  );
  MD5_STEP( MD5_F, b, c, d, a, le_block[7],  s[7],  t[7]  );
  MD5_STEP( MD5_F, a, b, c, d, le_block[8],  s[8],  t[8]  );
  MD5_STEP( MD5_F, d, a, b, c, le_block[9],  s[9],  t[9]  );
  MD5_STEP( MD5_F, c, d, a, b, le_block[10], s[10], t[10] );
  MD5_STEP( MD5_F, b, c, d, a, le_block[11], s[11], t[11] );
  MD5_STEP( MD5_F, a, b, c, d, le_block[12], s[12], t[12] );
  MD5_STEP( MD5_F, d, a, b, c, le_block[13], s[13], t[13] );
  MD5_STEP( MD5_F, c, d, a, b, le_block[14], s[14], t[14] );
  MD5_STEP( MD5_F, b, c, d, a, le_block[15], s[15], t[15] );

  MD5_STEP( MD5_G, a, b, c, d, le_block[1],  s[16], t[16] );
  MD5_STEP( MD5_G, d, a, b, c, le_block[6],  s[17], t[17] );
  MD5_STEP( MD5_G, c, d, a, b, le_block[11], s[18], t[18] );
  MD5_STEP( MD5_G, b, c, d, a, le_block[0],  s[19], t[19] );
  MD5_STEP( MD5_G, a, b, c, d, le_block[5],  s[20], t[20] );
  MD5_STEP( MD5_G, d, a, b, c, le_block[10], s[21], t[21] );
  MD5_STEP( MD5_G, c, d, a, b, le_block[15], s[22], t[22] );
  MD5_STEP( MD5_G, b, c, d, a, le_block[4],  s[23], t[23] );
  MD5_STEP( MD5_G, a, b, c, d, le_block[9],  s[24], t[24] );
  MD5_STEP( MD5_G, d, a, b, c, le_block[14], s[25], t[25] );
  MD5_STEP( MD5_G, c, d, a, b, le_block[3],  s[26], t[26] );
  MD5_STEP( MD5_G, b, c, d, a, le_block[8],  s[27], t[27] );
  MD5_STEP( MD5_G, a, b, c, d, le_block[13], s[28], t[28] );
  MD5_STEP( MD5_G, d, a, b, c, le_block[2],  s[29], t[29] );
  MD5_STEP( MD5_G, c, d, a, b, le_block[7],  s[30], t[30] );
  MD5_STEP( MD5_G, b, c, d, a, le_block[12], s[31], t[31] );

  MD5_STEP( MD5_H, a, b, c, d, le_block[5],  s[32], t[32] );
  MD5_STEP( MD5_H, d, a, b, c, le_block[8],  s[33], t[33] );
  MD5_STEP( MD5_H, c, d, a, b, le_block[11], s[34], t[34] );
  MD5_STEP( MD5_H, b, c, d, a, le_block[14], s[35], t[35] );
  MD5_STEP( MD5_H, a, b, c, d, le_block[1],  s[36], t[36] );
  MD5_STEP( MD5_H, d, a, b, c, le_block[4],  s[37], t[37] );
  MD5_STEP( MD5_H, c, d, a, b, le_block[7],  s[38], t[38] );
  MD5_STEP( MD5_H, b, c, d, a, le_block[10], s[39], t[39] );
  MD5_STEP( MD5_H, a, b, c, d, le_block[13], s[40], t[40] );
  MD5_STEP( MD5_H, d, a, b, c, le_block[0],  s[41], t[41] );
  MD5_STEP( MD5_H, c, d, a, b, le_block[3],  s[42], t[42] );
  MD5_STEP( MD5_H, b, c, d, a, le_block[6],  s[43], t[43] );
  MD5_STEP( MD5_H, a, b, c, d, le_block[9],  s[44], t[44] );
  MD5_STEP( MD5_H, d, a, b, c, le_block[12], s[45], t[45] );
  MD5_STEP( MD5_H, c, d, a, b, le_block[15], s[46], t[46] );
  MD5_STEP( MD5_H, b, c, d, a, le_block[2],  s[47], t[47] );

  MD5_STEP( MD5_I, a, b, c, d, le_block[0],  s[48], t[48] );
  MD5_STEP( MD5_I, d, a, b, c, le_block[7],  s[49], t[49] );
  MD5_STEP( MD5_I, c, d, a, b, le_block[14], s[50], t[50] );
  MD5_STEP( MD5_I, b, c, d, a, le_block[5],  s[51], t[51] );
  MD5_STEP( MD5_I, a, b, c, d, le_block[12], s[52], t[52] );
  MD5_STEP( MD5_I, d, a, b, c, le_block[3],  s[53], t[53] );
  MD5_STEP( MD5_I, c, d, a, b, le_block[10], s[54], t[54] );
  MD5_STEP( MD5_I, b, c, d, a, le_block[1],  s[55], t[55] );
  MD5_STEP( MD5_I, a, b, c, d, le_block[8],  s[56], t[56] );
  MD5_STEP( MD5_I, d, a, b, c, le_block[15], s[57], t[57] );
  MD5_STEP( MD5_I, c, d, a, b, le_block[6],  s[58], t[58] );
  MD5_STEP( MD5_I, b, c, d, a, le_block[13], s[59], t[59] );
  MD5_STEP( MD5_I, a, b, c, d, le_block[4],  s[60], t[60] );
  MD5_STEP( MD5_I, d, a, b, c, le_block[11], s[61], t[61] );
  MD5_STEP( MD5_I, c, d, a, b, le_block[2],  s[62], t[62] );
  MD5_STEP( MD5_I, b, c, d, a, le_block[9],  s[63], t[63] );

  md5->h[0] += a;
  md5->h[1] += b;
  md5->h[2] += c;
  md5->h[3] += d;
}

static long md5_write( void *v, void *buf, long len ) {
  md5_state *md5;
  long count;
  uint8_t *b;

  if ( v && buf ) {
    md5 = (md5_state *)v;
    b = (uint8_t *)buf;
    count = 0;
    while ( count < len ) {
      if ( md5->curr_block == MD5_BLOCK_LEN ) {
	md5_process_block( md5, md5->block );
	md5->curr_block = 0;
	md5->byte_count += MD5_BLOCK_LEN;
      }
      md5->block[md5->curr_block] = b[count];
      ++count;
      ++(md5->curr_block);
    }
    return len;
  }
  else return STREAMS_BAD_ARGS;
}

md5_state * start_new_md5( void ) {
  md5_state *md5;
  write_stream *ws;

  md5 = malloc( sizeof( *md5 ) );
  if ( md5 ) {
    ws = malloc( sizeof( *ws ) );
    if ( ws ) {
      md5->state = MD5_RUNNING;
      md5->h[0] = INIT_H0;
      md5->h[1] = INIT_H1;
      md5->h[2] = INIT_H2;
      md5->h[3] = INIT_H3;
      md5->byte_count = 0;
      md5->curr_block = 0;
      md5->ws = ws;
      ws->private = md5;
      ws->close = md5_close;
      ws->write = md5_write;
    }
    else {
      free( md5 );
      md5 = NULL;
    }
  }

  return md5;
}
