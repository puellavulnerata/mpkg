#include <pkg.h>

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
static long tar_read_from_stream( void *, void *, long );

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
	      prepare_for_file_read( tr, buf );
	      tr->zero_blocks_seen = 0;
	      ++(tr->files_seen);
	      tr->state = TAR_IN_FILE;
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

  if ( tr && buf && tr->state == TAR_READY ) {
    bufc = (unsigned char *)buf;
    if ( tr->u.in_file.f ) free( tr->u.in_file.f );
    tr->u.in_file.f = malloc( sizeof( *(tr->u.in_file.f) ) );
    if ( tr->u.in_file.f ) {
      switch ( bufc[TAR_LINK_IND_OFFSET] ) {
      case 0:
      case '0':
	tr->u.in_file.type = TAR_FILE;
	break;
      case '1':
	tr->u.in_file.type = TAR_LINK;
	break;
      case '2':
	tr->u.in_file.type = TAR_SYMLINK;
	break;
      case '3':
	tr->u.in_file.type = TAR_CDEV;
	break;
      case '4':
	tr->u.in_file.type = TAR_BDEV;
	break;
      case '5':
	tr->u.in_file.type = TAR_DIR;
	break;
      case '6':
	tr->u.in_file.type = TAR_FIFO;
	break;
      case '7':
	tr->u.in_file.type = TAR_CONTIG_FILE;
	break;
      default:
	tr->u.in_file.type = TAR_FILE;
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

tar_reader *start_tar_reader( read_stream *rs ) {
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

static void tar_close_read_stream( void *v ) {
  if ( v ) free( v );
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
