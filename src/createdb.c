#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include <pkg.h>

static int check_or_create_pkg_dir( void );
#ifdef DB_BDB
static void createdb_bdb( void );
#endif
static void createdb_text( void );

static int check_or_create_pkg_dir( void ) {
  const char *pkgdir, *instroot;
  char *rest_of_path, *comp, *parse_state, *cwd;
  int status, result;
  int chdir_pkgdir_errno, chdir_instroot_errno;

  status = 0;
  pkgdir = get_pkg();
  instroot = get_root();
  if ( pkgdir && instroot ) {
    result = chdir( pkgdir );
    if ( result != 0 ) {
      chdir_pkgdir_errno = errno;
      /*
       * Couldn't chdir to pkgdir, but if instroot is a prefix we can
       * try to create it.
       */

      if ( strstr( pkgdir, instroot ) == pkgdir ) {
	/* if instroot is a prefix of pkgdir... */
	if ( chdir_pkgdir_errno == ENOENT ) {
	  result = chdir( instroot );
	  if ( result == 0 ) {
	    rest_of_path =
	      malloc( sizeof( *rest_of_path ) *
		      ( strlen( pkgdir ) - strlen( instroot ) + 1 ) );
	    if ( rest_of_path ) {
	      strcpy( rest_of_path, pkgdir + strlen( instroot ) );
	      comp = get_path_component( rest_of_path, &parse_state );
	      while ( comp ) {
		if ( strlen( comp ) != 0 ) {
		  result = chdir( comp );
		  if ( result != 0 ) {
		    if ( errno == ENOENT ) {
		      /* Try to create it */

		      result = mkdir( comp, 0755 );
		      if ( result == 0 ) {
			result = chdir( comp );
			if ( result != 0 ) status = -1;
		      }
		      else status = -1;
		    }
		    else status = -1;
		  }

		  if ( status != 0 ) {
		    cwd = get_current_dir();
		    if ( cwd ) {
		      fprintf( stderr,
			       "Couldn't chdir or mkdir %s/%s\n",
			       cwd, comp );
		      free( cwd );
		    }
		    else {
		      fprintf( stderr, "Couldn't get cwd\n" );
		    }
		    break;
		  }
		}
		comp = get_path_component( NULL, &parse_state );
	      }

	      free( rest_of_path );
	    }
	    else {
	      fprintf( stderr,
		       "Failed to allocate memory\n" );
	      status = -1;
	    }
	  }
	  else {
	    fprintf( stderr,
		     "Failed to chdir to instroot (%s): %s\n",
		     instroot, strerror( chdir_pkgdir_errno ) );
	    status = -1;
	  }
	}
	else {
	  fprintf( stderr,
		   "Failed to chdir to pkgdir (%s): %s\n",
		   pkgdir, strerror( chdir_pkgdir_errno ) );
	  status = -1;
	}
      }
      else {
	fprintf( stderr,
		 "The pkgdir (%s) doesn't exist and I can't create it\n",
		 pkgdir );
	status = -1;
      }
    }
  }
  else {
    fprintf( stderr, "Failed to get pkgdir and instroot\n" );
    status = -1;
  }

  return status;
}


#ifdef DB_BDB

static void createdb_bdb( void ) {
  int status;
  pkg_db *db;
  char *filename;

  status = check_or_create_pkg_dir();
  if ( status == 0 ) {
    filename = malloc( sizeof( *filename ) *
		       ( strlen( get_pkg() ) +
			 strlen( PKGDB_BDB_FILE_NAME ) + 2 ) );
    if ( filename ) {
      sprintf( filename, "%s/%s", get_pkg(), PKGDB_BDB_FILE_NAME );
      db = create_pkg_db_bdb( filename );
      if ( db ) {
	close_pkg_db( db );
      }
      else {
	fprintf( stderr,
		 "Couldn't create bdb db\n" );
      }
      free( filename );
    }
    else {
      fprintf( stderr,
	       "Couldn't allocate memory to create bdb db\n" );
    }
  }
}

#endif /* DB_BDB */

void createdb_help( void ) {
  printf( "Create a new package database.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] createdb <format>\n" );
  printf( "\n" );
  printf( "<format> is one of:\n" );
#ifdef DB_BDB
  printf( "  bdb\n" );
#endif /* DB_BDB */
  printf( "  text\n" );
}

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

static void createdb_text( void ) {
  int status;
  pkg_db *db;
  char *filename;

  status = check_or_create_pkg_dir();
  if ( status == 0 ) {
    filename = malloc( sizeof( *filename ) *
		       ( strlen( get_pkg() ) +
			 strlen( PKGDB_TEXT_FILE_NAME ) + 2 ) );
    if ( filename ) {
      sprintf( filename, "%s/%s", get_pkg(), PKGDB_TEXT_FILE_NAME );
      db = create_pkg_db_text_file( filename );
      if ( db ) {
	close_pkg_db( db );
      }
      else {
	fprintf( stderr,
		 "Couldn't create text db\n" );
      }
      free( filename );
    }
    else {
      fprintf( stderr,
	       "Couldn't allocate memory to create text db\n" );
    }
  }
}
