#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <pkg.h>

typedef struct {
  char *filename;
  rbtree *data;
  int dirty, created;
} text_file_data;

static int close_text_file( void * );
static int delete_from_text_file( void *, char * );
static unsigned long entry_count_text_file( void * );
static int enumerate_text_file( void *, void *, char **, char **, void ** );
static int insert_into_text_file( void *, char *, char * );
static int make_backup( text_file_data * );
static int parse_line( char *, rbtree *, int );
static int parse_text_file( FILE *, rbtree * );
static char * query_text_file( void *, char * );
static text_file_data * read_text_file( char * );

static int close_text_file( void *tfd_v ) {
  text_file_data *tfd;
  int status, result, lnum;
  FILE *fp;
  rbtree_node *n;
  void *key_v, *val_v;
  char *key, *val;

  status = 0;
  if ( tfd_v ) {
    tfd = (text_file_data *)tfd_v;
    if ( tfd->dirty ) {
      if ( !(tfd->created) ) result = make_backup( tfd );
      else result = 0;

      if ( result == 0 ) {
	fp = fopen( tfd->filename, "w" );
	if ( fp ) {
	  n = NULL;
	  lnum = 0;
	  while ( key_v = rbtree_enum( tfd->data, n, &val_v, &n ) ) {
	    ++lnum;
	    if ( key_v && val_v ) {
	      key = (char *)key_v;
	      val = (char *)val_v;
	      if ( strlen( key ) > 0 && strlen( val ) > 0 ) {
		result = fprintf( fp, "%s %s\n", key, val );
		if ( result < 0 ) {
		  fprintf( stderr, "pkgdb_text_file line %d: ", lnum );
		  fprintf( stderr, "error %d while writing\n", result );
		  status = -1;
		}
	      }
	      else {
		fprintf( stderr, "pkgdb_text_file line %d: ", lnum );
		fprintf( stderr,
			 "empty line from the rbtree while writing.\n" );
		status = -1;
	      }
	    }
	    else {
	      fprintf( stderr, "pkgdb_text_file line %d: ", lnum );
	      fprintf( stderr,
		       "got a NULL out of the rbtree while writing.\n" );
	      status = -1;
	    }
	  }
	  fclose( fp );
	}
	else {
	  fprintf( stderr, "pkgdb_text_file: " );
	  fprintf( stderr, "couldn't open output file %s to flush.\n",
		   tfd->filename );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "pkgdb_text_file: " );
	fprintf( stderr, "couldn't make backup of file %s.\n",
		 tfd->filename );
	status = -1;
      }
    }
    free( tfd->filename );
    rbtree_free( tfd->data );
    free( tfd );
  }
  else status = -1;
  return status;
}

pkg_db * create_pkg_db_text_file( char *filename ) {
  pkg_db *temp;
  text_file_data *tfd;
  FILE *fp;

  if ( filename ) {
    temp = malloc( sizeof( *temp ) );
    if ( temp ) {
      temp->query = query_text_file;
      temp->insert = insert_into_text_file;
      temp->delete = delete_from_text_file;
      temp->close = close_text_file;
      temp->entry_count = entry_count_text_file;
      temp->enumerate = enumerate_text_file;
      temp->format = DBFMT_TEXT;
      temp->filename = copy_string( filename );

      if ( temp->filename ) {
	tfd = malloc( sizeof( *tfd ) );
	if ( tfd ) {
	  temp->private = (void *)tfd;
	  tfd->filename = copy_string( filename );
	  tfd->data = rbtree_alloc( rbtree_string_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    rbtree_string_copier,
				    rbtree_string_free );
	  tfd->dirty = 0;
	  tfd->created = 1;
	  if ( tfd->filename && tfd->data ) {
	    fp = fopen( tfd->filename, "w" );
	    if ( fp ) {
	      fclose( fp );
	      return temp;
	    }
	    else {
	      free( tfd->filename );
	      rbtree_free( tfd->data );
	      free( tfd );
	      free( temp->filename );
	      free( temp );
	      return NULL;
	    }
	  }
	  else {
	    if ( tfd->filename ) free( tfd->filename );
	    if ( tfd->data ) rbtree_free( tfd->data );
	    free( tfd );
	    free( temp->filename );
	    free( temp );
	    return NULL;
	  }
	}
	else {
	  free( temp->filename );
	  free( temp );
	  return NULL;
	}
      }
      else {
	free( temp );
	return NULL;
      }
    }
    else return NULL;
  }
  else return NULL;
}

static int delete_from_text_file( void *tfd_v, char *key ) {
  int status, result;
  text_file_data *tfd;

  status = 0;
  if ( tfd_v && key ) {
    tfd = (text_file_data *)tfd_v;
    result = rbtree_delete( tfd->data, key, NULL );
    if ( result == RBTREE_SUCCESS ) tfd->dirty = 1;
    else if ( result != RBTREE_NOT_FOUND ) status = -1;
  }
  else status = -1;
  return status;
}

static unsigned long entry_count_text_file( void *tfd_v ) {
  text_file_data *tfd;

  if ( tfd_v ) {
    tfd = (text_file_data *)tfd_v;
    return rbtree_size( tfd->data );
  }
  else return 0;
}

static int enumerate_text_file( void *tfd_v, void *n_in,
				char **k_out, char **v_out,
				void **n_out ) {
  int status;
  text_file_data *tfd;
  rbtree_node *n;
  void *ktmp_v, *vtmp_v;
  char *ktmp, *vtmp, *kcpy, *vcpy;

  status = 0;
  if ( tfd_v && k_out && v_out && n_out ) {
    tfd = (text_file_data *)tfd_v;
    n = (rbtree_node *)n_in;
    ktmp_v = rbtree_enum( tfd->data, n, &vtmp_v, &n );
    ktmp = (char *)ktmp_v;
    vtmp = (char *)vtmp_v;
    if ( ktmp ) {
      if ( vtmp ) {
	kcpy = copy_string( ktmp );
	vcpy = copy_string( vtmp );
	if ( kcpy && vcpy ) {
	  *k_out = kcpy;
	  *v_out = vcpy;
	  *n_out = (void *)n;
	}
	else {
	  fprintf( stderr, "pkgdb_text_file: " );
	  fprintf( stderr, "couldn't copy a string while enumerating.\n" );
	  if ( kcpy ) free( kcpy );
	  if ( vcpy ) free( vcpy );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "pkgdb_text_file: " );
	fprintf( stderr, "got a key %s with no value while enumerating.\n",
		 ktmp );
	status = -1;
      }
    }
    else {
      *k_out = NULL;
      *v_out = NULL;
      *n_out = NULL;
    }
  }
  else status = -1;
  return status;
}

static int insert_into_text_file( void *tfd_v, char *key, char *data ) {
  int status, result;
  text_file_data *tfd;

  status = 0;
  if ( tfd_v && key && data ) {
    tfd = (text_file_data *)tfd_v;
    result = rbtree_insert( tfd->data, key, data );
    if ( result == RBTREE_SUCCESS ) tfd->dirty = 1;
    else status = -1;
  }
  else status = -1;
  return status;
}

#define BK_BUFSIZ 1024

static int make_backup( text_file_data *tfd ) {
  int status, result, bk_file_len;
  char *bk_file;
  int srcfd, dstfd;
  struct stat st;
  char buf[BK_BUFSIZ];
  int count, written, wcount;

  status = 0;
  if ( tfd && tfd->filename ) {
    bk_file_len = strlen( tfd->filename ) + 5;
    bk_file = malloc( bk_file_len * sizeof( *bk_file ) );
    if ( bk_file ) {
      snprintf( bk_file, bk_file_len, "%s.bak", tfd->filename );

      /* Try to stat the backup file, and remove any old backup */
      result = lstat( bk_file, &st );
      if ( result == 0 ) {
	/* It exists */
	if ( S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) ) {
	  /* Try to remove it */
	  result = unlink( bk_file );
	  if ( result != 0 ) status = -1;
	}
	/* else error */
	else status = -1;
      }
      else {
	/* If we had ENOENT, we're okay, otherwise error */
	if ( errno != ENOENT ) status = -1;
      }

      if ( status == 0 ) {
	/* We've cleared the backup file */
	result = copy_file( bk_file, tfd->filename );
	if ( result != LINK_OR_COPY_SUCCESS ) {
	  unlink( bk_file );
	  status = -1;
	}
      }

      free( bk_file );
    }
    else status = -1;
  }
  else status = -1;

  return status;
}

pkg_db * open_pkg_db_text_file( char *filename ) {
  pkg_db *temp;

  if ( filename ) {
    temp = malloc( sizeof( *temp ) );
    if ( temp ) {
      temp->query = query_text_file;
      temp->insert = insert_into_text_file;
      temp->delete = delete_from_text_file;
      temp->close = close_text_file;
      temp->entry_count = entry_count_text_file;
      temp->enumerate = enumerate_text_file;
      temp->format = DBFMT_TEXT;
      temp->filename = copy_string( filename );

      if ( temp->filename ) {
	temp->private = read_text_file( filename );
	if ( temp->private ) return temp;
	else {
	  free( temp->filename );
	  free( temp );
	  return NULL;
	}
      }
      else {
	free( temp );
	return NULL;
      }
    }
    else return NULL;
  }
  else return NULL;
}

static int parse_line( char *line, rbtree *t, int lnum ) {
  int status, result, n;
  char **fields;
  char *path, *pkg;

  status = 0;
  if ( line && t ) {
    result = parse_strings_from_line( line, &fields );
    if ( result == 0 ) {
      n = strlistlen( fields );
      if ( n == 2 ) {
	path = fields[0];
	pkg = fields[1];
	result = rbtree_insert_no_overwrite( t, path, pkg );
	if ( result != RBTREE_SUCCESS ) {
	  if ( result == RBTREE_NO_OVERWRITE ) {
	    fprintf( stderr, "pkgdb_text_file line %d: ", lnum );
	    fprintf( stderr, "duplicate entry for %s\n", path );
	  }
	  else {
	    fprintf( stderr, "pkgdb_text_file line %d: ", lnum );
	    fprintf( stderr, "error inserting into rbtree (%d)\n", result );
	  }
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "pkgdb_text_file line %d: ", lnum );
	fprintf( stderr, "wrong number (%d) of fields.\n", n );
	status = -1;
      }
      free( fields );
    }
    else status = result;
  }
  else status = -1;
  return status;
}

static int parse_text_file( FILE *fp, rbtree *t ) {
  int status, lnum;
  char *line;

  status = 0;
  if ( fp && t ) {
    lnum = 0;
    while ( line = read_line_from_file( fp ) ) {
      ++lnum;
      if ( !is_whitespace( line ) ) status = parse_line( line, t, lnum );
      free( line );
      if ( status != 0 ) break;
    }
  }
  else status = -1;
  return status;
}

static char * query_text_file( void *tfd_v, char *key ) {
  text_file_data *tfd;
  void *val;
  int result;

  if ( tfd_v && key ) {
    tfd = (text_file_data *)tfd_v;
    result = rbtree_query( tfd->data, key, &val );
    if ( result == RBTREE_SUCCESS ) return copy_string( (char *)val );
    else return NULL;
  }
  else return NULL;
}

static text_file_data * read_text_file( char *filename ) {
  text_file_data *tfd;
  FILE *fp;
  int result;

  if ( filename ) {
    fp = fopen( filename, "r" );
    if ( fp ) {
      tfd = malloc( sizeof( *tfd ) );
      if ( tfd ) {
	tfd->dirty = 0;
	tfd->created = 0;
	tfd->filename = copy_string( filename );
	if ( tfd->filename ) {
	  tfd->data = rbtree_alloc( rbtree_string_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    rbtree_string_copier,
				    rbtree_string_free );
	  if ( tfd->data ) {
	    result = parse_text_file( fp, tfd->data );
	    if ( result != 0 ) {
	      rbtree_free( tfd->data );
	      free( tfd->filename );
	      free( tfd );
	      tfd = NULL;
	    }
	  }
	  else {
	    free( tfd->filename );
	    free( tfd );
	    tfd = NULL;
	  }
	}
	else {
	  free( tfd );
	  tfd = NULL;
	}
      }
      fclose( fp );
      return tfd;
    }
    else return NULL;
  }
  else return NULL;
}
