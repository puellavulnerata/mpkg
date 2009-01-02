#include <pkg.h>

#ifdef DB_BDB
static void createdb_bdb( void );
#endif
static void createdb_text( void );

void createdb_main( int argc, char **argv ) {
  if ( sanity_check_globals() == 0 ) {
    if ( argc == 0 ) {
#ifdef DB_BDB
      /* We're compiling BDB support, so we use that by default */
      createdb_bdb();
#else
      /* No BDB support, use text */
      createdb_text();
#endif
    }
    else if ( argc == 1 ) {
      if ( strcmp( argv[0], "text" ) == 0 ) createdb_text();
#ifdef DB_BDB
      else if ( strcmp( argv[0], "bdb" ) == 0 ) createdb_bdb();
#endif
      else {
	fprintf( stderr,
		 "Unknown db type %s in mpkg createdb\n",
		 argv[0] );
      }
    }
    else {
      fprintf( stderr,
	       "Wrong number of arguments (%d) to mpkg createdb\n",
	       argc );
    }
  }
  /* else sanity_check_globals() will emit a warning */
}
