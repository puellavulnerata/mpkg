#include <stdlib.h>

#include <pkg.h>

typedef struct {
  char *filename;
  rbtree *data;
} text_file_data;

static int close_text_file( void * );
static int delete_from_text_file( void *, char * );
static int insert_into_text_file( void *, char *, char * );
static int parse_text_file( FILE *, rbtree * );
static char * query_text_file( void *, char * );
static text_file_data * read_text_file( char * );

static int close_text_file( void *tfd_v ) {
  return -1;
}

static int delete_from_text_file( void *tfd_v, char *key ) {
  return -1;
}

static int insert_into_text_file( void *tfd_v, char *key, char *data ) {
  return -1;
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
      temp->private = read_text_file( filename );
      if ( temp->private ) return temp;
      else {
	free( temp );
	return NULL;
      }
    }
    else return NULL;
  }
  else return NULL;
}

static int parse_text_file( FILE *fp, rbtree *t ) {
  int status;

  status = 0;
  if ( fp && t ) {
    /* parse it here */
  }
  else status = -1;
  return status;
}

static char * query_text_file( void *tfd_v, char *key ) {
  return NULL;
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
