#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <pkg.h>

static int emit_tar_header( write_stream *, tar_file_info *,
			    unsigned long long );

static int is_all_zero( void * );
static int is_file_header( void * );

/*
 * This one parses the file header, and fills out the fields in
 * u.in_file
 */

static void prepare_for_file_read( tar_reader *, void * );

/*
 * Returns TAR_NO_MORE_FILES to indicate EOF, or TAR_IO_ERROR for
 * other error.
 */

static int read_tar_block( tar_reader *, void * );

static void tar_close_read_stream( void * );
static void tar_close_write_stream( void * );
static long tar_read_from_stream( void *, void *, long );
static long tar_write_to_stream( void *, void *, long );

void close_tar_reader( tar_reader *tr ) {
  if ( tr ) {
    if ( tr->state == TAR_IN_FILE ) {
      if ( tr->u.in_file.f ) {
	free( tr->u.in_file.f );
	tr->u.in_file.f = NULL;
      }
    }
    free( tr );
  }
}

void close_tar_writer( tar_writer *tw ) {
  unsigned char buf[TAR_BLOCK_SIZE];

  if ( tw ) {
    if ( tw->state == TAR_READY ) {
      memset( buf, 0, TAR_BLOCK_SIZE );
      /* Terminate with two all-zero blocks */
      write_to_stream( tw->ws, buf, TAR_BLOCK_SIZE );
      write_to_stream( tw->ws, buf, TAR_BLOCK_SIZE );
    }
    else if ( tw->state == TAR_IN_FILE ) {
      /*
       * Don't do this, it will produce bad files, but let's at least
       * avoid leaking.
       */

      if ( tw->u.in_file.tmp >= 0 ) {
	close( tw->u.in_file.tmp );
	if ( tw->u.in_file.tmp_name ) unlink( tw->u.in_file.tmp_name );
	tw->u.in_file.tmp = -1;
      }
      if ( tw->u.in_file.tmp_name ) {
	free( tw->u.in_file.tmp_name );
	tw->u.in_file.tmp_name = NULL;
      }
    }
    free( tw );
  }
}

static int emit_tar_header( write_stream *ws, tar_file_info *info,
			    unsigned long long size ) {
  unsigned char buf[TAR_BLOCK_SIZE], link_ind, tmp[13];
  int filename_len, target_len, i;
  unsigned char *ustar_sig = "ustar00";
  unsigned int checksum;

  if ( ws && info ) {
    /* Clear out the buffer */
    memset( buf, 0, TAR_BLOCK_SIZE );

    /* Set the USTAR indicator */
    memcpy( buf + TAR_USTAR_SIG_OFFSET, ustar_sig, strlen( ustar_sig ) + 1 );

    /* Set the Link Indicator byte */
    switch ( info->type ) {
    case TAR_FILE:
      link_ind = 0;
      break;
    case TAR_LINK:
      link_ind = '1';
      break;
    case TAR_SYMLINK:
      link_ind = '2';
      break;
    case TAR_CDEV:
      link_ind = '3';
      break;
    case TAR_BDEV:
      link_ind = '4';
      break;
    case TAR_DIR:
      link_ind = '5';
      break;
    case TAR_FIFO:
      link_ind = '6';
      break;
    case TAR_CONTIG_FILE:
      link_ind = '7';
      break;
    default:
      /* File by default */
      link_ind = 0;
      break;
    }
    buf[TAR_LINK_IND_OFFSET] = link_ind;

    /* Emit the filename */
    filename_len = strlen( info->filename );
    if ( filename_len <= TAR_FILENAME_LEN ) {
      memcpy( buf + TAR_FILENAME_OFFSET, info->filename, filename_len );
    }
    else if ( filename_len <= TAR_FILENAME_LEN + TAR_PREFIX_LEN ) {
      memcpy( buf + TAR_PREFIX_OFFSET, info->filename,
	      filename_len - TAR_FILENAME_LEN );
      memcpy( buf + TAR_FILENAME_OFFSET,
	      info->filename + ( filename_len - TAR_FILENAME_LEN ),
	      TAR_FILENAME_LEN );
    }
    else return TAR_INTERNAL_ERROR;

    /* Emit the target if necessary */
    if ( info->type == TAR_LINK || info->type == TAR_SYMLINK ) {
      target_len = strlen( info->target );
      if ( target_len <= TAR_TARGET_LEN )
	memcpy( buf + TAR_TARGET_OFFSET, info->target, target_len );
      else return TAR_INTERNAL_ERROR;
    }

    /* Emit the owner */
    memset( tmp, 0, 8 );
    snprintf( tmp, 8, "%07o", info->owner );
    memcpy( buf + TAR_OWNER_OFFSET, tmp, 8 );

    /* Emit the group */
    memset( tmp, 0, 8 );
    snprintf( tmp, 8, "%07o", info->group );
    memcpy( buf + TAR_GROUP_OFFSET, tmp, 8 );

    /* Emit the mode */
    memset( tmp, 0, 8 );
    snprintf( tmp, 8, "%07o", info->mode );
    memcpy( buf + TAR_MODE_OFFSET, tmp, 8 );

    /* Emit the mtime */
    memset( tmp, 0, 12 );
    snprintf( tmp, 12, "%011o", info->mtime );
    memcpy( buf + TAR_MTIME_OFFSET, tmp, 12 );

    /* Emit the size - max 8G */
    if ( size < 8589934592LL ) {
      memset( tmp, 0, 12 );
      snprintf( tmp, 12, "%011Lo", size );
      memcpy( buf + TAR_SIZE_OFFSET, tmp, 12 );
    }
    else return TAR_INTERNAL_ERROR;

    /* Calculate the checksum */
    checksum = 0;
    for ( i = 0; i < TAR_CHECKSUM_OFFSET; ++i )
      checksum += buf[i];
    /*
     * The 8 bytes of checksum count as ASCII spaces (32) for toward the
     * checksum
     */
    checksum += 8 * ' ';
    for ( i = TAR_LINK_IND_OFFSET; i < TAR_BLOCK_SIZE; ++i )
      checksum += buf[i];

    /* Write the checksum to the block */
    memset( tmp, 0, 8 );
    snprintf( tmp, 8, "%07o", checksum );
    memcpy( buf + TAR_CHECKSUM_OFFSET, tmp, 8 );

    /* Write this block to the stream */
    if ( write_to_stream( ws, buf, TAR_BLOCK_SIZE ) == TAR_BLOCK_SIZE )
      return TAR_SUCCESS;
    else return TAR_IO_ERROR;
  }
  else return TAR_BAD_PARAMS;
}

tar_file_info * get_file_info( tar_reader *tr ) {
  if ( tr ) {
    if ( tr->state == TAR_IN_FILE ) return tr->u.in_file.f;
    else return NULL;
  }
  else return NULL;
}

int get_next_file( tar_reader *tr ) {
  char buf[TAR_BLOCK_SIZE];
  int status, result;
  unsigned long long blocks_total;

  if ( tr ) {
    if ( tr->state == TAR_READY || tr->state == TAR_IN_FILE ) {
      if ( tr->state == TAR_IN_FILE ) {
        status = TAR_SUCCESS;
	blocks_total = tr->u.in_file.bytes_total / TAR_BLOCK_SIZE;
	if ( tr->u.in_file.bytes_total % TAR_BLOCK_SIZE > 0 )
	  ++blocks_total;
	while ( tr->u.in_file.blocks_seen < blocks_total ) {
	  status = read_tar_block( tr, buf );
	  if ( status == TAR_SUCCESS )
	    ++(tr->u.in_file.blocks_seen);
	  else break;
	}
	if ( tr->u.in_file.f ) {
	  free( tr->u.in_file.f );
	  tr->u.in_file.f = NULL;
	}
	if ( status == TAR_SUCCESS ) {
	  tr->state = TAR_READY;
	  tr->zero_blocks_seen = 0;
	}
	else {
	  tr->state = TAR_DONE;
	  tr->zero_blocks_seen = 0;
	  if ( status == TAR_NO_MORE_FILES )
	    result = TAR_UNEXPECTED_EOF;
	  else result = status;
	}
      }
      if ( tr->state == TAR_READY ) {
	do {
	  status = read_tar_block( tr, buf );
	  if ( status == TAR_SUCCESS ) {
	    if ( is_file_header( buf ) ) {
	      tr->state = TAR_IN_FILE;
	      prepare_for_file_read( tr, buf );
	      tr->zero_blocks_seen = 0;
	      ++(tr->files_seen);
	      result = TAR_SUCCESS;
	    }
	    else if ( is_all_zero( buf ) ) {
	      ++(tr->zero_blocks_seen);
	      if ( tr->zero_blocks_seen >= 2 ) {
		tr->state = TAR_DONE;
		tr->zero_blocks_seen = 0;
		result = TAR_NO_MORE_FILES;
	      }
	    }
	  }
	  else {
	    tr->state = TAR_DONE;
	    tr->zero_blocks_seen = 0;
	    if ( status == TAR_NO_MORE_FILES )
	      result = TAR_UNEXPECTED_EOF;
	    else result = status;
	  }
	} while ( tr->state == TAR_READY );
      }
      return result;
    }
    else if ( tr->state == TAR_DONE ) return TAR_NO_MORE_FILES;
    else return TAR_INTERNAL_ERROR;
  }
  else return TAR_BAD_PARAMS;
}

read_stream * get_reader_for_file( tar_reader *tr ) {
  read_stream *rs;
  tar_read_stream *trs;

  if ( tr ) {
    if ( tr->state == TAR_IN_FILE ) {
      rs = malloc( sizeof( *rs ) );
      if ( rs ) {
	trs = malloc( sizeof( *trs ) );
	if ( trs ) {
	  trs->filenum = tr->files_seen;
	  trs->tr = tr;
	  rs->private = trs;
	  rs->read = tar_read_from_stream;
	  rs->close = tar_close_read_stream;
	}
	else {
	  free( rs );
	  rs = NULL;
	}
      }
      return rs;
    }
    else return NULL;
  }
  else return NULL;
}

static int is_all_zero( void *buf ) {
  char *bufc;
  int i;

  bufc = (char *)buf;
  for ( i = 0; i < TAR_BLOCK_SIZE; ++i )
    if ( bufc[i] != 0 ) return 0;
  return 1;
}

static int is_file_header( void *buf ) {
  int count, i;
  unsigned int checksum, calc_checksum;
  unsigned char *cbuf;
  unsigned char tmp[9];

  if ( !buf ) return 0;
  cbuf = (unsigned char *)buf;

  /*
   * The checksum field is 8 bytes long, and is a six-digital octal
   * number in ASCII with leading zeroes followed by a NUL a space
   */

  memcpy( tmp, cbuf + TAR_CHECKSUM_OFFSET, 8 );
  tmp[8] = 0;
  count = sscanf( tmp, "%o", &checksum );
  if ( count != 1 ) return 0;

  calc_checksum = 0;
  for ( i = 0; i < TAR_CHECKSUM_OFFSET; ++i )
    calc_checksum += cbuf[i];
  /*
   * The 8 bytes of checksum count as ASCII spaces (32) for toward the
   * checksum
   */
  calc_checksum += 8 * ' ';
  for ( i = TAR_LINK_IND_OFFSET; i < TAR_BLOCK_SIZE; ++i )
    calc_checksum += cbuf[i];

  if ( checksum != calc_checksum ) return 0;

  return 1;
}

static void prepare_for_file_read( tar_reader *tr, void *buf ) {
  /*
   * is_file_header() gets called before this, so we can assume it is
   * a valid tar header here.
   */
  unsigned char *bufc;
  char tmp[13];
  int pref_chars, count;

  if ( tr && buf && tr->state == TAR_IN_FILE ) {
    bufc = (unsigned char *)buf;
    tr->u.in_file.f = malloc( sizeof( *(tr->u.in_file.f) ) );
    if ( tr->u.in_file.f ) {
      switch ( bufc[TAR_LINK_IND_OFFSET] ) {
      case 0:
      case '0':
	tr->u.in_file.f->type = TAR_FILE;
	break;
      case '1':
	tr->u.in_file.f->type = TAR_LINK;
	break;
      case '2':
	tr->u.in_file.f->type = TAR_SYMLINK;
	break;
      case '3':
	tr->u.in_file.f->type = TAR_CDEV;
	break;
      case '4':
	tr->u.in_file.f->type = TAR_BDEV;
	break;
      case '5':
	tr->u.in_file.f->type = TAR_DIR;
	break;
      case '6':
	tr->u.in_file.f->type = TAR_FIFO;
	break;
      case '7':
	tr->u.in_file.f->type = TAR_CONTIG_FILE;
	break;
      default:
	tr->u.in_file.f->type = TAR_FILE;
	break;
      }

      pref_chars = 0;
      memcpy( tmp, bufc + TAR_USTAR_SIG_OFFSET, 5 );
      tmp[5] = 0;
      if ( strcmp( tmp, "ustar" ) == 0 ) {
	/* Check for a USTAR name prefix */
	while ( pref_chars < TAR_PREFIX_LEN ) {
	  if ( bufc[TAR_PREFIX_OFFSET + pref_chars] != 0 )
	    ++pref_chars;
	  else break;
	}
	if ( pref_chars > 0 )
	  memcpy( tr->u.in_file.f->filename,
		  bufc + TAR_PREFIX_OFFSET,
		  pref_chars );
      }

      count = 0;
      while ( count < TAR_FILENAME_LEN ) {
	if ( bufc[TAR_FILENAME_OFFSET + count] != 0 ) ++count;
	else break;
      }

      if ( count > 0 )
	memcpy( tr->u.in_file.f->filename + pref_chars,
		bufc + TAR_FILENAME_OFFSET,
		count );
      tr->u.in_file.f->filename[pref_chars + count] = 0;

      /* Okay, done with the name and prefix */

      count = 0;
      while ( count < TAR_TARGET_LEN ) {
	if ( bufc[TAR_TARGET_OFFSET + count] != 0 ) ++count;
	else break;
      }
      if ( count > 0 )
	memcpy( tr->u.in_file.f->target,
		bufc + TAR_TARGET_OFFSET,
		count );
      tr->u.in_file.f->target[count] = 0;

      /* And the target now */

      count = 0;
      while ( count < TAR_OWNER_LEN) {
	if ( !( bufc[TAR_OWNER_OFFSET + count] == 0 ||
		bufc[TAR_OWNER_OFFSET + count] == ' ' ) ) {
	  tmp[count] = bufc[TAR_OWNER_OFFSET + count];
	  ++count;
	}
	else break;
      }
      tmp[count] = 0;

      count = sscanf( tmp, "%o", &(tr->u.in_file.f->owner) );
      if ( count != 1 )
	tr->u.in_file.f->owner = 0;

      /* Done with owner */

      count = 0;
      while ( count < TAR_GROUP_LEN) {
	if ( !( bufc[TAR_GROUP_OFFSET + count] == 0 ||
		bufc[TAR_GROUP_OFFSET + count] == ' ' ) ) {
	  tmp[count] = bufc[TAR_GROUP_OFFSET + count];
	  ++count;
	}
	else break;
      }
      tmp[count] = 0;

      count = sscanf( tmp, "%o", &(tr->u.in_file.f->group) );
      if ( count != 1 )
	tr->u.in_file.f->group = 0;

      /* Done with group */

      count = 0;
      while ( count < TAR_MODE_LEN) {
	if ( !( bufc[TAR_MODE_OFFSET + count] == 0 ||
		bufc[TAR_MODE_OFFSET + count] == ' ' ) ) {
	  tmp[count] = bufc[TAR_MODE_OFFSET + count];
	  ++count;
	}
	else break;
      }
      tmp[count] = 0;

      count = sscanf( tmp, "%o", &(tr->u.in_file.f->mode) );
      if ( count != 1 )
	tr->u.in_file.f->mode = 0644;

      /* Done with mode */

      count = 0;
      while ( count < TAR_MTIME_LEN) {
	if ( !( bufc[TAR_MTIME_OFFSET + count] == 0 ||
		bufc[TAR_MTIME_OFFSET + count] == ' ' ) ) {
	  tmp[count] = bufc[TAR_MTIME_OFFSET + count];
	  ++count;
	}
	else break;
      }
      tmp[count] = 0;

      count = sscanf( tmp, "%o", &(tr->u.in_file.f->mtime) );
      if ( count != 1 )
	tr->u.in_file.f->mtime = 0;

      /* Done with mtime */

      count = 0;
      while ( count < TAR_SIZE_LEN) {
	if ( !( bufc[TAR_SIZE_OFFSET + count] == 0 ||
		bufc[TAR_SIZE_OFFSET + count] == ' ' ) ) {
	  tmp[count] = bufc[TAR_SIZE_OFFSET + count];
	  ++count;
	}
	else break;
      }
      tmp[count] = 0;

      count = sscanf( tmp, "%Lo", &(tr->u.in_file.bytes_total) );
      if ( count != 1 )
	tr->u.in_file.bytes_total = 0;

      tr->u.in_file.blocks_seen = 0;
      tr->u.in_file.bytes_seen = 0;
    }
  }
}

write_stream * put_next_file( tar_writer *tw, tar_file_info *info ) {
  write_stream *ws;
  int tmp;
  char tmpl[] = "/tmp/pkgtarXXXXXX";
  char *tmp_name;

  if ( tw && info ) {
    if ( tw->state == TAR_READY ) {
      tw->u.in_file.f = info;
      tw->u.in_file.bytes_seen = 0;
      ws = malloc( sizeof( *ws ) );
      if ( ws ) {
	ws->private = tw;
	ws->close = tar_close_write_stream;
	ws->write = tar_write_to_stream;
	tmp_name = malloc( sizeof( *tmp_name ) * ( strlen( tmpl ) + 1 ) );
	if ( tmp_name ) {
	  tmp = mkstemp( tmpl );
	  if ( tmp >= 0 ) {
	    strcpy( tmp_name, tmpl );
	    tw->u.in_file.tmp = tmp;
	    tw->u.in_file.tmp_name = tmp_name;
	    tw->state = TAR_IN_FILE;
	    ++(tw->files_out);
	    return ws;
	  }
	  else {
	    free( tmp_name );
	    free( ws );
	    return NULL;
	  }
	}
	else {
	  free( ws );
	  return NULL;
	}
      }
      else return NULL;
    }
    else return NULL;
  }
  else return NULL;
}

static int read_tar_block( tar_reader *tr, void *buf ) {
  int status, result;
  long len, read;

  result = TAR_SUCCESS;
  if ( tr && buf ) {
    if ( tr->rs ) {
      read = 0;
      while ( read < TAR_BLOCK_SIZE ) {
	len = read_from_stream( tr->rs, buf + read, TAR_BLOCK_SIZE - read );
	if ( len > 0 ) read += len;
	else break;
      }
      if ( read < TAR_BLOCK_SIZE ) result = TAR_NO_MORE_FILES;
    }
    else result = TAR_INTERNAL_ERROR;
  }
  else result = TAR_BAD_PARAMS;
  return result;
}

tar_reader * start_tar_reader( read_stream *rs ) {
  tar_reader *tr;

  if ( rs ) {
    tr = malloc( sizeof( *tr ) );
    if ( tr ) {
      tr->files_seen = 0;
      tr->blocks_seen = 0;
      tr->state = TAR_READY;
      tr->rs = rs;
      return tr;
    }
    else return NULL;
  }
  else return NULL;
}

tar_writer * start_tar_writer( write_stream *ws ) {
  tar_writer *tw;

  if ( ws ) {
    tw = malloc( sizeof( *tw ) );
    if ( tw ) {
      tw->files_out = 0;
      tw->blocks_out = 0;
      tw->state = TAR_READY;
      tw->u.in_file.f = NULL;
      tw->u.in_file.tmp_name = NULL;
      tw->u.in_file.tmp = -1;
      tw->u.in_file.bytes_seen = 0;
      tw->ws = ws;
      return tw;
    }
    else return NULL;
  }
  else return NULL;
}

static void tar_close_read_stream( void *v ) {
  if ( v ) free( v );
}

static void tar_close_write_stream( void *v ) {
  tar_writer *tw;
  int status;
  off_t o;
  unsigned char buf[TAR_BLOCK_SIZE];
  unsigned long long so_far, this_time;
  unsigned int block_so_far;
  size_t r;

  tw = (tar_writer *)v;
  if ( tw ) {
    if ( tw->state == TAR_IN_FILE && tw->u.in_file.tmp >= 0 ) {
      status = emit_tar_header( tw->ws, tw->u.in_file.f,
				tw->u.in_file.bytes_seen );
      if ( status == TAR_SUCCESS ) {
	++(tw->blocks_out);
	o = lseek( tw->u.in_file.tmp, 0, SEEK_SET );
	if ( o == 0 ) {
	  so_far = 0;
	  while ( so_far < tw->u.in_file.bytes_seen ) {
	    this_time = tw->u.in_file.bytes_seen - so_far;
	    if ( this_time > TAR_BLOCK_SIZE ) this_time = TAR_BLOCK_SIZE;

	    block_so_far = 0;
	    while ( block_so_far < this_time ) {
	      r = read( tw->u.in_file.tmp, buf + block_so_far,
			(size_t)( this_time - block_so_far ) );
	      if ( r > 0 ) block_so_far += r;
	      else break;
	    }

	    if ( block_so_far == this_time ) {
	      if ( this_time < TAR_BLOCK_SIZE )
		memset( buf + this_time, 0, TAR_BLOCK_SIZE - this_time );

	      r = write_to_stream( tw->ws, buf, TAR_BLOCK_SIZE );
	      if ( r != TAR_BLOCK_SIZE ) break;

	      ++(tw->blocks_out);
	      so_far += this_time;
	    }
	    else break;
	  }
	}
	close( tw->u.in_file.tmp );
	tw->u.in_file.tmp = -1;
	if ( tw->u.in_file.tmp_name ) {
	  unlink( tw->u.in_file.tmp_name );
	  free( tw->u.in_file.tmp_name );
	  tw->u.in_file.tmp_name = NULL;
	}
	tw->state = TAR_READY;
      }
    }
  }
}

static long tar_read_from_stream( void *v, void *buf, long size ) {
  tar_read_stream *trs;
  long max_read, read, this_read;
  unsigned long long left_in_block, ofs;
  int status;

  if ( v && buf && size > 0 ) {
    trs = (tar_read_stream *)v;
    if ( trs->tr ) {
      if ( trs->tr->files_seen == trs->filenum ) {
	if ( trs->tr->state == TAR_IN_FILE ) {
	  max_read = trs->tr->u.in_file.bytes_total -
	    trs->tr->u.in_file.bytes_seen;
	  if ( max_read > size ) max_read = size;
	  if ( max_read > 0 ) {
	    read = 0;
	    while ( read < max_read ) {
	      left_in_block = trs->tr->u.in_file.blocks_seen * TAR_BLOCK_SIZE -
		trs->tr->u.in_file.bytes_seen;
	      if ( left_in_block <= TAR_BLOCK_SIZE )
		this_read = (long)left_in_block;
	      else return read;

	      if ( this_read == 0 ) {
		status =
		  read_tar_block( trs->tr, trs->tr->u.in_file.curr_block );
		if ( status == TAR_SUCCESS ) {
		  ++(trs->tr->blocks_seen);
		  ++(trs->tr->u.in_file.blocks_seen);
		  this_read = TAR_BLOCK_SIZE;
		  left_in_block = TAR_BLOCK_SIZE;
		}
		else return read;
	      }

	      if ( this_read > max_read - read ) this_read = max_read - read;
	      ofs = TAR_BLOCK_SIZE - left_in_block;
	      memcpy( buf + read, trs->tr->u.in_file.curr_block + ofs,
		      this_read );
	      read += this_read;
	      trs->tr->u.in_file.bytes_seen += this_read;
	    }
	    return read;
	  }
	  else return STREAMS_EOF;
	}
	else return STREAMS_EOF;
      }
      else return STREAMS_BAD_STREAM;
    }
    else return STREAMS_BAD_STREAM;
  }
  else return STREAMS_BAD_ARGS;
}

static long tar_write_to_stream( void *v, void *buf, long size ) {
  tar_writer *tw;
  ssize_t written, total;

  if ( v && buf && size > 0 ) {
    tw = (tar_writer *)v;
    if ( tw->state == TAR_IN_FILE ) {
      if ( tw->u.in_file.tmp >= 0 ) {
	total = 0;
	while ( total < size ) {
	  written = write( tw->u.in_file.tmp, buf + total, size - total );
	  if ( written >= 0 ) {
	    total += written;
	    tw->u.in_file.bytes_seen += written;
	  }
	  else break;
	}
	if ( total > 0 ) return total;
	else return STREAMS_INTERNAL_ERROR;
      }
      else return STREAMS_BAD_STREAM;
    }
    else return STREAMS_BAD_STREAM;
  }
  else return STREAMS_BAD_ARGS;
}
