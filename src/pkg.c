#include <stdlib.h>
#include <stdio.h>

#include <pkg.h>

int main( int, char **, char ** );

int main( int argc, char **argv, char **envp ) {
  pkg_db *indb, *outdb;
  char *path, *pkg;
  void *n;
  int result;

  if ( argc == 3 ) {
    indb = open_pkg_db_text_file( argv[1] );
    if ( indb ) {
      printf( "Opened indb, got %lu entries.\n",
	      get_entry_count_for_pkg_db( indb ) );
      outdb = create_pkg_db_text_file( argv[2] );
      if ( outdb ) {
	n = NULL;
	do {
	  result = enumerate_pkg_db( indb, n, &path, &pkg, &n );
	  if ( n ) {
	    insert_into_pkg_db( outdb, path, pkg );
	    free( path );
	    free( pkg );
	  }
	} while ( n );
	close_pkg_db( outdb );
      }
      else {
	fprintf( stderr, "Couldn't create out db %s.\n", argv[2] );
      }
      close_pkg_db( indb );
    }
    else {
      fprintf( stderr, "Couldn't open in db %s.\n", argv[1] );
    }
  }
  else {
    fprintf( stderr, "Wrong number of args %d.\n", argc );
  }
  return 0;
}
