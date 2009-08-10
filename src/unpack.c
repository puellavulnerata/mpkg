#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include <pkg.h>

typedef struct {
  pkg_handle *p;
  rbtree *cksums;
} pkg_handle_builder;

static pkg_handle_builder * alloc_pkg_handle_builder( void );
static int check_cksums( pkg_handle_builder * );
static void * cksum_copier( void * );
static void cksum_free( void * );
static void cleanup_pkg_handle_builder( pkg_handle_builder * );
static int handle_descr( pkg_handle_builder *, read_stream * );
static int handle_file( pkg_handle_builder *, tar_file_info *,
			read_stream * );
static int setup_dirs_for_unpack( char *, char * );

#ifdef PKGFMT_V1
static pkg_handle * open_pkg_file_v1( const char * );
# ifdef COMPRESSION_BZIP2
static pkg_handle * open_pkg_file_v1_bzip2( const char * );
# endif
# ifdef COMPRESSION_GZIP
static pkg_handle * open_pkg_file_v1_gzip( const char * );
# endif
static pkg_handle * open_pkg_file_v1_none( const char * );
static pkg_handle * open_pkg_file_v1_stream( read_stream *, pkg_compression_t );
#endif

#ifdef PKGFMT_V2
static int handle_content_v2( pkg_handle_builder *, read_stream * );
static pkg_handle * open_pkg_file_v2( const char * );
static pkg_handle * open_pkg_file_v2_stream( read_stream * );
#endif

static pkg_handle_builder * alloc_pkg_handle_builder( void ) {
  pkg_handle_builder *b;
  int status;
  char *tmp;

  b = malloc( sizeof( *b ) );
  if ( b ) {
    b->p = NULL;
    if ( get_check_md5() ) {
      b->cksums = rbtree_alloc( rbtree_string_comparator,
				rbtree_string_copier,
				rbtree_string_free,
				cksum_copier,
				cksum_free );
    }
    else b->cksums = NULL;
    if ( !get_check_md5() || b->cksums ) {
      b->p = malloc( sizeof( *(b->p) ) );
      if ( b->p ) {
	b->p->compression = DEFAULT_COMPRESSION;
	b->p->version = DEFAULT_VERSION;
	b->p->descr = NULL;
	b->p->descr_file = NULL;
	b->p->unpacked_dir = get_temp_dir();
	if ( b->p->unpacked_dir ) {
	  tmp = concatenate_paths( b->p->unpacked_dir, "package-content" );
	  if ( tmp ) {
	    status = mkdir( tmp, 0700 );
	    free( tmp );
	    if ( status != 0 ) goto error;
	  }
	  else goto error;	}
	else goto error;
      }
      else goto error;
    }
    else goto error;
  }

  return b;

error:
  if ( b ) {
    if ( b->p ) {
      if ( b->p->unpacked_dir ) {
	recrm( b->p->unpacked_dir );
	free( b->p->unpacked_dir );
      }
      if ( b->cksums ) rbtree_free( b->cksums );
      free( b->p );
    }
    free( b );
  }

  return NULL;
}

static int check_cksums( pkg_handle_builder *b ) {
  int result, i, status;
  uint8_t *descr_cksum, *actual_cksum;

  result = 0;
  if ( b ) {
    if ( b->p && b->cksums ) {
      if ( b->p->descr ) {
	for ( i = 0; i < b->p->descr->num_entries; ++i ) {
	  if ( b->p->descr->entries[i].type == ENTRY_FILE ) {
	    descr_cksum = b->p->descr->entries[i].u.f.hash;
	    status = rbtree_query( b->cksums,
				   b->p->descr->entries[i].filename,
				   (void **)(&actual_cksum) );
	    if ( status == RBTREE_SUCCESS ) {
	      if ( memcmp( descr_cksum, actual_cksum, MD5_RESULT_LEN ) != 0 ) {
		fprintf( stderr, "Checksums do not match for %s\n",
			 b->p->descr->entries[i].filename );
		result = -4;
	      }
	      rbtree_delete( b->cksums,
			     b->p->descr->entries[i].filename,
			     NULL );
	    }
	    else if ( status == RBTREE_NOT_FOUND ) {
	      fprintf( stderr, "File %s from package-description not found\n",
		       b->p->descr->entries[i].filename );
	      result = -4;
	    }
	    else {
	      fprintf( stderr, "Red/black tree error during checksum for %s\n",
		       b->p->descr->entries[i].filename );
	      result = -4;
	    }
	  }
	  else if ( b->p->descr->entries[i].type == ENTRY_LAST ) break;
	}
      }
      else result = -3;
    }
    else result = -2;
  }
  else result = -1;

  return result;
}


static void * cksum_copier( void *v ) {
  uint8_t *cksum, *copy;

  copy = NULL;
  if ( v ) {
    cksum = (uint8_t *)v;
    copy = malloc( sizeof( *copy ) * MD5_RESULT_LEN );
    if ( copy ) memcpy( copy, cksum, sizeof( *cksum ) * MD5_RESULT_LEN );
  }

  return copy;
}

static void cksum_free( void *v ) {
  if ( v ) free( v );
}

static void cleanup_pkg_handle_builder( pkg_handle_builder *b ) {
  if ( b ) {
    /* Leave the pkg_handle itself so we can return it */
    if ( b->cksums ) rbtree_free( b->cksums );
    free( b );    
  }
}

void close_pkg( pkg_handle *h ) {
  if ( h ) {
    if ( h->descr ) {
      free_pkg_descr( h->descr );
      h->descr = NULL;
    }
    if ( h->descr_file ) {
      unlink( h->descr_file );
      free( h->descr_file );
      h->descr_file = NULL;
    }
    if ( h->unpacked_dir ) {
      recrm( h->unpacked_dir );
      free( h->unpacked_dir );
      h->unpacked_dir = NULL;
    }
    free( h );
  }
}

#ifdef PKGFMT_V2

static int handle_content_v2( pkg_handle_builder *b, read_stream *rs ) {
  int error, status, result;
  tar_reader *tr;
  tar_file_info *tinf;
  read_stream *trs;

  status = 0;
  if ( b && rs ) {
    tr = start_tar_reader( rs );
    if ( tr ) {
      error = 0;
      while ( ( result = get_next_file( tr ) ) == TAR_SUCCESS ) {
	tinf = get_file_info( tr );
	if ( tinf->type == TAR_FILE ) {
	  trs = get_reader_for_file( tr );
	  if ( trs ) {
	    status = handle_file( b, tinf, trs );
	    if ( status != 0 ) {
	      error = 1;
	      break;
	    }
	    close_read_stream( trs );
	  }
	  else {
	    error = 1;
	    break;
	  }	
	}
	/* Else skip non-files */
      }

      if ( error == 0 && result != TAR_NO_MORE_FILES ) error = 1;

      if ( error != 0 ) status = -3;
      else status = 0;

      close_tar_reader( tr );
    }
    else status = -2;
  }
  else status = -1;

  return status;
}

#endif

#define UNPACK_BUF_LEN 1024

static int handle_descr( pkg_handle_builder *b, read_stream *rs ) {
  int result, error;
  unsigned char buf[UNPACK_BUF_LEN];
  char *dst;
  write_stream *ws;
  long len, wlen;
  pkg_descr *descr;

  result = 0;
  if ( b && rs ) {
    dst = concatenate_paths( b->p->unpacked_dir, "package-description" );
    if ( dst ) {
      ws = open_write_stream_none( dst );
      if ( ws ) {
	error = 0;
	while ( ( len = read_from_stream( rs, buf, UNPACK_BUF_LEN ) ) > 0 ) {
	  wlen = write_to_stream( ws, buf, len );
	  if ( wlen != len ) {
	    error = 1;
	    break;
	  }
	}
	if ( len < 0 || error ) {
	  /* Error reading or writing it */
	  result = -4;
	}

	close_write_stream( ws );
      }
      else result = -3;

      if ( result == 0 ) {
	descr = read_pkg_descr_from_file( dst );
	if ( !descr ) result == -4;
      }

      if ( result == 0 ) {
	b->p->descr_file = dst;
	b->p->descr = descr;
      }
      else free( dst );
    }
    else result = -2;
  }
  else result = -1;

  return result;
}

static int handle_file( pkg_handle_builder *b, tar_file_info *tinf,
			read_stream *rs ) {
  int result, error, status;
  char *dst, *tmp;
  write_stream *ws, *md5_ws;
  md5_state *md5;
  unsigned char buf[UNPACK_BUF_LEN];
  long len, wlen;
  uint8_t cksum[MD5_RESULT_LEN];

  result = 0;
  if ( b && tinf && rs ) {
    if ( tinf->type == TAR_FILE ) {
      if ( get_check_md5() ) {
	md5 = start_new_md5();
	if ( md5 ) md5_ws = get_md5_ws( md5 );
	else md5_ws = NULL;
      }
      else {
	md5 = NULL;
	md5_ws = NULL;
      }

      if ( !get_check_md5() || md5 ) {
	tmp = concatenate_paths( b->p->unpacked_dir, "package-content" );
	if ( tmp ) {
	  dst = concatenate_paths( tmp, tinf->filename );
	  free( tmp );
	  if ( dst ) {
	    status = setup_dirs_for_unpack( b->p->unpacked_dir, dst );
	    if ( status == 0 ) {
	      ws = open_write_stream_none( dst );
	      free( dst );
	      if ( ws ) {
		error = 0;
		while ( ( len =
			  read_from_stream( rs, buf, UNPACK_BUF_LEN ) ) > 0 ) {
		  if ( md5_ws ) {
		    wlen = write_to_stream( md5_ws, buf, len );
		    if ( wlen != len ) {
		      error = 1;
		      break;
		    }
		  }
		  wlen = write_to_stream( ws, buf, len );
		  if ( wlen != len ) {
		    error = 1;
		    break;
		  }
		}
		if ( len == 0 && !error ) {
		  close_write_stream( ws );
		  if ( md5_ws ) close_write_stream( md5_ws );
		  if ( md5 ) {
		    status = get_md5_result( md5, cksum );
		    if ( status == 0 ) {
		      tmp = concatenate_paths( "/", tinf->filename );
		      if ( tmp ) {
			status = rbtree_insert_no_overwrite( b->cksums,
							     tmp, cksum );
			free( tmp );
			if ( status != RBTREE_SUCCESS ) result = -11;
		      }
		      else result = -10;
		    }
		    else result = -9;
		  }
		}
		else {
		  /* Error reading or writing */

		  close_write_stream( ws );
		  if ( md5_ws ) close_write_stream( md5_ws );
		  result = -8;
		}
	      }
	      else result = -7;
	    }
	    else result = -6;
	  }
	  else result = -5;
	}
	else result = -4;

	if ( md5 ) close_md5( md5 );
      }
      else result = -3;
    }
    else result = -2;
  }
  else result = -1;

  return result;
}

pkg_handle * open_pkg_file( const char *filename ) {

#if defined( PKGFMT_V1 ) && defined( PKGFMT_V2 )

  int tried_v1, tried_v2, len;
  pkg_handle *h;
  const char *suffix;

  tried_v1 = 0;
  tried_v2 = 0;
  h = NULL;

  len = strlen( filename );

  /* Try .pkg and .mpkg for v2 first */
  if ( len > 4 && tried_v2 == 0 ) {
    suffix = filename + len - 4;
    if ( strcmp( suffix, ".pkg" ) == 0 ) {
      h = open_pkg_file_v2( filename );
      if ( h ) return h;
      else tried_v2 = 1;
    }
  }
  if ( len > 5 && tried_v2 == 0 ) {
    suffix = filename + len - 5;
    if ( strcmp( suffix, ".mpkg" ) == 0 ) {
      h = open_pkg_file_v2( filename );
      if ( h ) return h;
      else tried_v2 = 1;
    }
  }

  /*
   * Now try .tar for v1, and .tar.gz and .tar.bz2 if we have those
   * compression types.
   */
  if ( len > 4 && tried_v1 == 0 ) {
    suffix = filename + len - 4;
    if ( strcmp( suffix, ".tar" ) == 0 ) {
      h = open_pkg_file_v1( filename );
      if ( h ) return h;
      else tried_v1 = 1;
    }
  }
#ifdef COMPRESSION_GZIP
  if ( len > 7 && tried_v1 == 0 ) {
    suffix = filename + len - 7;
    if ( strcmp( suffix, ".tar.gz" ) == 0 ) {
      h = open_pkg_file_v1( filename );
      if ( h ) return h;
      else tried_v1 = 1;
    }
  }
#endif
#ifdef COMPRESSION_BZIP2
  if ( len > 8 && tried_v1 == 0 ) {
    suffix = filename + len - 8;
    if ( strcmp( suffix, ".tar.bz2" ) == 0 ) {
      h = open_pkg_file_v1( filename );
      if ( h ) return h;
      else tried_v1 = 1;
    }
  }
#endif

  /* It didn't have any of the standard suffixes */

  if ( tried_v2 == 0 ) {
    h = open_pkg_file_v2( filename );
    if ( h ) return h;
    else tried_v2 = 1;
  }
  if ( tried_v1 == 0 ) {
    h = open_pkg_file_v1( filename );
    if ( h ) return h;
    else tried_v1 = 1;
  }

  /* Give up */

  return NULL;

#else

# if defined( PKGFMT_V1 ) || defined( PKGFMT_V2 )

#  ifdef PKGFMT_V1
  return open_pkg_file_v1( filename );
#  else
  return open_pkg_file_v2( filename );
#  endif

# else

#  error At least one of PKGFMT_V1 or PKGFMT_V2 must be defined

# endif

#endif  

}

#ifdef PKGFMT_V1

static pkg_handle * open_pkg_file_v1( const char *filename ) {
  int len;
  const char *suffix;
  int tried_none = 0;
# ifdef COMPRESSION_GZIP
  int tried_gzip = 0;
# endif
# ifdef COMPRESSION_BZIP2
  int tried_bzip2 = 0;
# endif
  pkg_handle *result;

  result = NULL;
  if ( filename ) {
    len = strlen( filename );
# ifdef COMPRESSION_BZIP2
    if ( !result && len >= 8 ) {
      suffix = filename + len - 8;
      if ( strcmp( suffix, ".tar.bz2" ) == 0 ) {
	result = open_pkg_file_v1_bzip2( filename );
	tried_bzip2 = 1;
      }
    }
# endif
# ifdef COMPRESSION_GZIP
    if ( !result && len >= 7 ) {
      suffix = filename + len - 7;
      if ( strcmp( suffix, ".tar.gz" ) == 0 ) {
	result = open_pkg_file_v1_gzip( filename );
	tried_gzip = 1;
      }
    }
# endif
    if ( !result && len >= 4 ) {
      suffix = filename + len - 4;
      if ( strcmp( suffix, ".tar" ) == 0 ) {
	result = open_pkg_file_v1_none( filename );
	tried_none = 1;
      }
    }

    if ( !result && !tried_none ) {
      result = open_pkg_file_v1_none( filename );
      tried_none = 1;
    }
# ifdef COMPRESSION_GZIP
    if ( !result && !tried_gzip ) {
      result = open_pkg_file_v1_gzip( filename );
      tried_gzip = 1;
    }
# endif
# ifdef COMPRESSION_BZIP2
    if ( !result && !tried_bzip2 ) {
      result = open_pkg_file_v1_bzip2( filename );
      tried_bzip2 = 1;
    }
# endif
  }

  return result;
}

#ifdef COMPRESSION_BZIP2

static pkg_handle * open_pkg_file_v1_bzip2( const char *filename ) {
  read_stream *rs;
  pkg_handle *p;

  p = NULL;
  if ( filename ) {
    rs = open_read_stream_bzip2( filename );
    if ( rs ) {
      p = open_pkg_file_v1_stream( rs, BZIP2 );
      close_read_stream( rs );
    }
  }

  return p;
}

#endif

#ifdef COMPRESSION_GZIP

static pkg_handle * open_pkg_file_v1_gzip( const char *filename ) {
  read_stream *rs;
  pkg_handle *p;

  p = NULL;
  if ( filename ) {
    rs = open_read_stream_gzip( filename );
    if ( rs ) {
      p = open_pkg_file_v1_stream( rs, GZIP );
      close_read_stream( rs );
    }
  }

  return p;
}

#endif

static pkg_handle * open_pkg_file_v1_none( const char *filename ) {
  read_stream *rs;
  pkg_handle *p;

  p = NULL;
  if ( filename ) {
    rs = open_read_stream_none( filename );
    if ( rs ) {
      p = open_pkg_file_v1_stream( rs, NONE );
      close_read_stream( rs );
    }
  }

  return p;
}

static pkg_handle * open_pkg_file_v1_stream( read_stream *rs, pkg_compression_t comp ) {
  pkg_handle *p;
  tar_reader *tr;
  read_stream *trs;
  tar_file_info *tinf;
  int error, status;
  pkg_handle_builder *b;

  p = NULL;
  if ( rs ) {
    tr = start_tar_reader( rs );
    if ( tr ) {
      b = alloc_pkg_handle_builder();
      if ( b ) {
	b->p->compression = comp;
	b->p->version = V1;
	error = 0;
	while ( ( status = get_next_file( tr ) ) == TAR_SUCCESS ) {
	  tinf = get_file_info( tr );
	  if ( tinf->type == TAR_FILE ) {
	    trs = get_reader_for_file( tr );
	    if ( trs ) {
	      if ( strcmp( tinf->filename, "package-description" ) == 0 )
		status = handle_descr( b, trs );
	      else status = handle_file( b, tinf, trs );
	      if ( status != 0 ) {
		error = 1;
		break;
	      }
	      close_read_stream( trs );
	    }
	    else {
	      error = 1;
	      break;
	    }
	  }
	}
	if ( status != TAR_NO_MORE_FILES ) error = 1;
	else if ( get_check_md5() ) {
	  status = check_cksums( b );
	  if ( status != 0 ) error = 1;
	}
	if ( !error ) p = b->p;
	else close_pkg( b->p );
	cleanup_pkg_handle_builder( b );
      }
      close_tar_reader( tr );
    }  
  }

  return p;
}

#endif

#ifdef PKGFMT_V2

static pkg_handle * open_pkg_file_v2( const char *filename ) {
  read_stream *rs;
  pkg_handle *p;

  p = NULL;
  if ( filename ) {
    rs = open_read_stream_none( filename );
    if ( rs ) {
      p = open_pkg_file_v2_stream( rs );
      close_read_stream( rs );
    }
  }

  return p;
}

static pkg_handle * open_pkg_file_v2_stream( read_stream *rs ) {
  pkg_handle *p;
  tar_reader *tr;
  read_stream *trs, *decomped_trs;
  tar_file_info *tinf;
  int error, status, got_descr, got_content;
  pkg_handle_builder *b;

  p = NULL;
  got_descr = 0;
  got_content = 0;
  decomped_trs = NULL;
  if ( rs ) {
    tr = start_tar_reader( rs );
    if ( tr ) {
      b = alloc_pkg_handle_builder();
      if ( b ) {
	b->p->version = V2;
	error = 0;
	while ( ( status = get_next_file( tr ) ) == TAR_SUCCESS ) {
	  tinf = get_file_info( tr );
	  if ( tinf->type == TAR_FILE ) {
	    trs = get_reader_for_file( tr );
	    if ( trs ) {
	      if ( strcmp( tinf->filename, "package-description" ) == 0 ) {
		if ( !got_descr ) {
		  status = handle_descr( b, trs );
		  if ( status == 0 ) got_descr = 1;
		}
		/* Duplicate package-description */
		else status = -1;
	      }
	      else if ( strcmp( tinf->filename, "package-content.tar" ) == 0 ) {
		if ( !got_content ) {
		  status = handle_content_v2( b, trs );
		  if ( status == 0 ) {
		    b->p->compression = NONE;
		    got_content = 1;
		  }
		}
		/* Duplicate package-content */
		else status = -1;
	      }
#ifdef COMPRESSION_GZIP
	      else if ( strcmp( tinf->filename, "package-content.tar.gz" ) == 0 ) {
		if ( !got_content ) {
		  decomped_trs = open_read_stream_from_stream_gzip( trs );
		  if ( decomped_trs ) {
		    status = handle_content_v2( b, decomped_trs );
		    if ( status == 0 ) {
		      got_content = 1;
		      b->p->compression = GZIP;
		    }
		    close_read_stream( decomped_trs );
		    decomped_trs = NULL;
		  }
		}
		/* Duplicate package-content */
		else status = -1;
	      }
#endif
#ifdef COMPRESSION_BZIP2
	      else if ( strcmp( tinf->filename, "package-content.tar.bz2" ) == 0 ) {
		if ( !got_content ) {
		  decomped_trs = open_read_stream_from_stream_bzip2( trs );
		  if ( decomped_trs ) {
		    status = handle_content_v2( b, decomped_trs );
		    if ( status == 0 ) {
		      got_content = 1;
		      b->p->compression = BZIP2;
		    }
		    close_read_stream( decomped_trs );
		    decomped_trs = NULL;
		  }
		}
		/* Duplicate package-content */
		else status = -1;
	      }
#endif
	      /* 
	       * else {
	       *     Skip any filenames we don't recognize so they can be
	       *     used in future versions
	       * }
	       */

	      close_read_stream( trs );

	      if ( status != 0 ) {
		error = 1;
		break;
	      }
	    }
	    else {
	      error = 1;
	      break;
	    }
	  }
	}

	if ( got_content && got_descr ) {
	  if ( status != TAR_NO_MORE_FILES ) error = 1;
	  else if ( get_check_md5() ) {
	    status = check_cksums( b );
	    if ( status != 0 ) error = 1;
	  }
	}
	else error = 1;

	if ( !error ) p = b->p;
	else close_pkg( b->p );

	cleanup_pkg_handle_builder( b );
      }
      close_tar_reader( tr );
    }  
  }

  return p;
}

#endif

static int setup_dirs_for_unpack( char *base, char *dst ) {
  int blen, dlen, result, slashes, status, i, j;
  char *temp;
  struct stat statbuf;

  result = 0;
  if ( base && dst ) {
    blen = strlen( base );
    dlen = strlen( dst );
    if ( blen > 0 && dlen > 0 && dlen > blen ) {
      temp = malloc( sizeof( *temp ) * ( dlen + 1 ) );
      if ( temp ) {
	strcpy( temp, dst );

	/*
	 * Check that base is a prefix of dst
	 */

	for ( i = 0; i < blen; ++i ) {
	  if ( temp[i] != base[i] ) {
	    result = -4;
	    break;
	  }
	}

	if ( result == 0 ) {
	  slashes = 0;
	  /*
	   * Count a terminating slash in base
	   */
	  if ( base[blen-1] == '/' ) ++slashes;
	  i = blen;
	  while ( temp[i++] == '/' ) ++slashes;
	  if ( slashes > 0 ) {
	    while ( i < dlen && result == 0 ) {
	      j = i;
	      while ( temp[j] != 0 && temp[j] != '/' ) ++j;
	      if ( j > i ) {
		if ( temp[j] == '/' ) {
		  /* We found a directory element from i to j */
		  temp[j] = 0;
		  status = lstat( temp, &statbuf );
		  if ( status == 0 ) {
		    if ( !( S_ISDIR( statbuf.st_mode ) ) ) {
		      /* Already exists and isn't a directory! */
		      result = -7;
		    }
		  }
		  else {
		    if ( errno == ENOENT ) {
		      /* It doesn't exist yet, create it */
		      status = mkdir( temp, 0700 );
		      if ( status != 0 ) result = -8;
		    }
		    else {
		      /* Other error from lstat() */
		      result = -9;
		    }
		  }
		  temp[j] = '/';
		}
		else {
		  /*
		   * We found the end of the string, the filename is
		   * from i to j
		   */

		  break;
		}
		while ( temp[j] == '/' ) ++j;
		i = j;
	      }
	      else result = -6; /* No empty elements or terminating slashes */
	    }

	    /*
	     * All directories should be created at this point
	     */
	  }
	  else result = -5; /* base must end on a directory boundary */
	}

	free( temp );
      }
      else result = -3;
    }
    else result = -2;
  }
  else result = -1;

  return result;
}
