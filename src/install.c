#include <pkg.h>

#define INSTALL_SUCCESS 0
#define INSTALL_ERROR -1
#define INSTALL_OUT_OF_DISK -2

static int install_pkg( pkg_db *, pkg_handle * );

void install_main( int argc, char **argv ) {
  pkg_db *db;
  int i, status;
  pkg_handle *p;

  if ( argc > 0 ) {
    db = open_pkg_db();
    if ( db ) {
      for ( i = 0; i < argc; ++i ) {
	p = open_pkg_file( argv[i] );
	if ( p ) {
	  status = install_pkg( db, p );
	  close_pkg( p );
	  if ( status != INSTALL_SUCCESS ) {
	    fprintf( stderr, "Failed to install %s\n", argv[i] );
	    if ( status == INSTALL_OUT_OF_DISK ) {
	      fprintf( stderr,
		       "Out of disk space trying to install %s, stopping.\n",
		       argv[i] );
	      break;
	    }
	  }
	}
	else {
	  fprintf( stderr, "Warning: couldn't open %s to install\n",
		   argv[i] );
	}
      }
      close_pkg_db( db );
    }
    else {
      fprintf( stderr, "Couldn't open package database\n" );
    }
  }
  else {
    fprintf( stderr,
	     "At least one package must be specified to install\n" );
  }
}

static int install_pkg( pkg_db *db, pkg_handle *p ) {
  return INSTALL_ERROR;
}
