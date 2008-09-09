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
static int handle_descr( pkg_handle_builder *, tar_file_info *,
			 read_stream * );
static int handle_file( pkg_handle_builder *, tar_file_info *,
			read_stream * );

#ifdef PKGFMT_V1
static pkg_handle * open_pkg_file_v1( const char * );
# ifdef COMPRESSION_BZIP2
static pkg_handle * open_pkg_file_v1_bzip2( const char * );
# endif
# ifdef COMPRESSION_GZIP
static pkg_handle * open_pkg_file_v1_gzip( const char * );
# endif
static pkg_handle * open_pkg_file_v1_none( const char * );
static pkg_handle * open_pkg_file_v1_stream( read_stream * );
#endif

#ifdef PKGFMT_V2
static pkg_handle * open_pkg_file_v2( const char * );
#endif

static pkg_handle_builder * alloc_pkg_handle_builder( void ) {
  pkg_handle_builder *b;

  b = malloc( sizeof( *b ) );
  if ( b ) {
    b->cksums = rbtree_alloc( rbtree_string_comparator,
			      rbtree_string_copier,
			      rbtree_string_free,
			      cksum_copier,
			      cksum_free );
    if ( b->cksums ) {
      b->p = malloc( sizeof( *(b->p) ) );
      if ( b->p ) {
	b->p->descr = NULL;
	b->p->descr_file = NULL;
	b->p->unpacked_dir = get_temp_dir();
	if ( !(b->p->unpacked_dir) ) {
	  rbtree_free( b->cksums );
	  free( b->p );
	  free( b );
	  b = NULL;
	}
      }
      else {
	rbtree_free( b->cksums );
	free( b );
	b = NULL;
      }
    }
    else {
      free( b );
      b = NULL;
    }
  }

  return b;
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
      p = open_pkg_file_v1_stream( rs );
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
      p = open_pkg_file_v1_stream( rs );
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
      p = open_pkg_file_v1_stream( rs );
      close_read_stream( rs );
    }
  }

  return p;
}

static pkg_handle * open_pkg_file_v1_stream( read_stream *rs ) {
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
	b->p->version = PKG_VER_1;
	error = 0;
	while ( get_next_file( tr ) == TAR_SUCCESS ) {
	  tinf = get_file_info( tr );
	  if ( tinf->type == TAR_FILE ) {
	    trs = get_reader_for_file( tr );
	    if ( trs ) {
	      if ( strcmp( tinf->filename, "package-description" ) == 0 )
		status = handle_descr( b, tinf, trs );
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
	status = check_cksums( b );
	if ( status != 0 ) error = 1;
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
  return NULL;
}

#endif

