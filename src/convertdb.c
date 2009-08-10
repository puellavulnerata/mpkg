#include <stdlib.h>
#include <string.h>

#include <pkg.h>

#define CONVERTDB_SUCCESS (0)
#define CONVERTDB_ERROR (-1)

static void convertdb( dbfmt_t );
static int copy_db_entries( pkg_db *, pkg_db * );

void convertdb_help( void ) {
  printf( "Convert the package database to a different format.  Usage:\n\n" );
  printf( "mpkg [global options] convertdb <format>\n\n" );
  printf( "<format> is one of:\n" );
#ifdef DB_BDB
  printf( "  bdb\n" );
#endif /* DB_BDB */
  printf( "  text\n" );
}

void convertdb_main( int argc, char **argv ) {
  int status;
  dbfmt_t dst_fmt;

  dst_fmt = DBFMT_UNKNOWN;
  status = CONVERTDB_SUCCESS;
  if ( argc == 1 ) {
    if ( argv && argv[0] ) {
      if ( strcmp( argv[0], "text" ) == 0 ) {
	dst_fmt = DBFMT_TEXT;
      }
#ifdef DB_BDB
      else if ( strcmp( argv[0], "bdb" ) == 0 ) {
	dst_fmt = DBFMT_BDB;
      }
#endif /* DB_BDB */
      else {
	fprintf( stderr, "Unknown destination DB format \'%s\'\n", argv[0] );
	status = CONVERTDB_ERROR;
      }
    }
    else {
      fprintf( stderr, "Internal error\n" );
      status = CONVERTDB_ERROR;
    }
  }
  else {
    fprintf( stderr, "Wrong number of arguments (%d)\n", argc );
    status = CONVERTDB_ERROR;
  }

  if ( status == CONVERTDB_SUCCESS ) {
    convertdb( dst_fmt );
  }
  /* else error parsing args, so nothing to do */
}

static void convertdb( dbfmt_t dst_fmt ) {
  pkg_db *src, *dst;
  int delete_src, result;
  char *src_filename, *src_backup_filename, *dst_filename;
  int status, src_backup_filename_len;

  status = CONVERTDB_SUCCESS;
  if ( dst_fmt == DBFMT_TEXT
#ifdef DB_BDB
       || dst_fmt == DBFMT_BDB
#endif /* DB_BDB */
       ) {
    src = open_pkg_db();
    if ( src ) {
      src_filename = copy_string( src->filename );
      if ( src_filename ) {
	delete_src = 0;
	if ( src->format != dst_fmt ) {
	  /* First, try to create and open the destination format */
	  
	  dst = NULL;
	  dst_filename = NULL;
	  if ( dst_fmt == DBFMT_TEXT ) {
	    dst_filename =
	      concatenate_paths( get_pkg(), PKGDB_TEXT_FILE_NAME );
	  }
#ifdef DB_BDB
	  else if ( dst_fmt = DBFMT_BDB ) {
	    dst_filename =
	      concatenate_paths( get_pkg(), PKGDB_BDB_FILE_NAME );
	  }
#endif /* DB_BDB */
	  else status = CONVERTDB_ERROR;

	  if ( dst_filename && status == CONVERTDB_SUCCESS ) {
	    /* Remove any existing file */
	    if ( unlink_if_needed( dst_filename ) ) {
	      if ( dst_fmt == DBFMT_TEXT ) {
		dst = create_pkg_db_text_file( dst_filename );
	      }
#ifdef DB_BDB
	      else if ( dst_fmt == DBFMT_BDB ) {
		dst = create_pkg_db_bdb( dst_filename );
	      }
#endif /* DB_BDB */
	      else status = CONVERTDB_ERROR;

	      if ( !dst ) {
		fprintf( stderr,
			 "Unable to create destination database at %s\n",
			 dst_filename );
		status = CONVERTDB_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr,
		       "Unable to remove existing file or directory at %s\n",
		       dst_filename );
	      status = CONVERTDB_ERROR;
	    }
	  }
	  else {
	    fprintf( stderr, "Internal error\n" );
	  }

	  if ( dst && status == CONVERTDB_SUCCESS ) {
	    /*
	     * We have source and destination databases open; next we
	     * make a backup copy of the source.
	     */
	    src_backup_filename_len = strlen( src_filename ) + 5;
	    src_backup_filename = malloc( ( src_backup_filename_len + 1 ) *
					  sizeof( *src_backup_filename ) );
	    if ( src_backup_filename ) {
	      snprintf( src_backup_filename, src_backup_filename_len,
			"%s.bak", src_filename );
	      result = copy_file( src_backup_filename, src_filename );
	      if ( result != LINK_OR_COPY_SUCCESS ) {
		fprintf( stderr, "Unable to back up source database\n" );
		status = CONVERTDB_ERROR;
	      }
	      free( src_backup_filename );
	      src_backup_filename = NULL;
	    }
	    else {
	      fprintf( stderr, "Unable to allocate memory\n" );
	      status = CONVERTDB_ERROR;
	    }

	    if ( status == CONVERTDB_SUCCESS ) {
	      /*
	       * We have source and destination databases open, and
	       * the source is backed up.  We can copy entries over
	       * now.
	       */

	      result = copy_db_entries( src, dst );
	      if ( result != CONVERTDB_SUCCESS ) status = result;
	    }

	    /* Close the destination database, and remove it if we failed */
	    result = close_pkg_db( dst );
	    /*
	     * Pay attention to result; text format can fail on out of
	     * disk at close; if close fails, do *not* delete source.
	     */
	    if ( result != 0 ) {
	      fprintf( stderr, "Failed to write destination db\n" );
	      status = CONVERTDB_ERROR;
	    }

	    /* Delete source or destination depending on status at finish */
	    if ( status == CONVERTDB_SUCCESS ) delete_src = 1;
	    else unlink( dst_filename );
	  }
	  else status = CONVERTDB_ERROR;
	  
	  if ( dst_filename ) {
	    free( dst_filename );
	    dst_filename = NULL;
	  }
	}
	else {
	  fprintf( stderr, "Database already in requested format\n" );
	}
      }
      else {
	fprintf( stderr, "Unable to allocate memory\n" );
      }

      close_pkg_db( src );

      if ( src_filename ) {
	if ( delete_src ) {
	  unlink( src_filename );
	}

	free( src_filename );
	src_filename = NULL;
      }
    }
    else {
      fprintf( stderr, "Unable to open package database\n" );
    }
  }
  else {
    fprintf( stderr, "Internal error\n" );
  }
}

static int copy_db_entries( pkg_db *src, pkg_db *dst ) {
  int status, result, count;
  void *n, *temp;
  char *key, *value;

  status = CONVERTDB_SUCCESS;
  if ( src && dst ) {
    count = 0;
    n = NULL;
    do {
      /* Grab an entry from the source */
      result = enumerate_pkg_db( src, n, &key, &value, &temp );
      if ( result != 0 ) {
	fprintf( stderr, "Error enumerating from source database\n" );
	status = CONVERTDB_ERROR;
      }

      /* Did we get one? */
      if ( status == CONVERTDB_SUCCESS && key && value ) {
	/* Insert it into the destination */
	result = insert_into_pkg_db( dst, key, value );
	if ( result == 0 ) ++count;
	else {
	  fprintf( stderr,
		   "Error inserting %s/%s into destination database\n",
		   key, value );
	  status = CONVERTDB_ERROR;
	}
      }

      /* Free the key/value */
      if ( key ) {
	free( key );
	key = NULL;
      }

      if ( value ) {
	free( value );
	value = NULL;
      }

      /* Prep for the next entry */
      n = temp;
    } while ( n && status == CONVERTDB_SUCCESS );

    if ( status == CONVERTDB_SUCCESS ) {
      printf( "Copied %d key/value pairs into destination database\n", count );
    }
  }
  else status = CONVERTDB_ERROR;

  return status;  
}
