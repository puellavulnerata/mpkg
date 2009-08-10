#include <pkg.h>

#include <stdlib.h>

#define DUMPDB_SUCCESS (0)
#define DUMPDB_ERROR (-1)

static void dumpdb( void );

void dumpdb_help( void ) {
  printf( "Dump the contents of the package DB.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] dumpdb\n" );
  printf( "\n" );
  printf( "The dumpdb command emits records one to a line, in the " );
  printf( "following format:\n" );
  printf( "\n" );
  printf( "<location> <package name>\n" );
  printf( "\n" );
  printf( "Here, <location> is an absolute path, and <package name> is" );
  printf( " the name of the package which owns that path.\n" );
}

void dumpdb_main( int argc, char **argv ) {
  if ( argc == 0 ) {
    dumpdb();
  }
  else {
    fprintf( stderr, "Too many arguments for dumpdb command\n" );
  }
}

static void dumpdb( void ) {
  pkg_db *db;
  void *n, *temp;
  char *key, *value;
  int result, status;

  status = DUMPDB_SUCCESS;
  db = open_pkg_db();
  if ( db ) {
    n = NULL;
    key = value = NULL;
    do {
      /* Retrieve an entry */
      result = enumerate_pkg_db( db, n, &key, &value, &temp );
      if ( result != 0 ) {
	fprintf( stderr, "Unable to enumerate database\n" );
	status = DUMPDB_ERROR;
      }

      if ( status == DUMPDB_SUCCESS && key && value ) {
	printf( "%s %s\n", key, value );
      }

      /* Free the key and value */
      if ( key ) {
	free( key );
	key = NULL;
      }

      if ( value ) {
	free( value );
	value = NULL;
      }

      /* Prep for next entry */
      n = temp;
    } while ( n && status == DUMPDB_SUCCESS );
    close_pkg_db( db );
  }
  else {
    fprintf( stderr, "Unable to open package database\n" );
    status = DUMPDB_ERROR;
  }
}
