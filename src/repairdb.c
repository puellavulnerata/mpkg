#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pkg.h>

static int make_repairdb_backup( pkg_db * );
static int perform_repair( pkg_db *, char );
static void repairdb( char );

static int make_repairdb_backup( pkg_db *db ) {
  int status, result;
  const char *backup_suffix = ".orig";
  char *backup_filename;
  int backup_filename_len;

  status = REPAIRDB_SUCCESS;
  if ( db && db->filename ) {
    backup_filename_len =
      strlen( db->filename ) + strlen( backup_suffix ) + 1;

    backup_filename =
      malloc( sizeof( *backup_filename ) * backup_filename_len );

    if ( backup_filename ) {
      snprintf( backup_filename, backup_filename_len, "%s%s",
		db->filename, backup_suffix );

      result = copy_file( backup_filename, db->filename );
      if ( result != LINK_OR_COPY_SUCCESS ) {
	fprintf( stderr,
		 "Unable to copy %s to %s in make_repairdb_backup()\n",
		 db->filename, backup_filename );
	status = REPAIRDB_ERROR;
      }

      free( backup_filename );
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory in make_repairdb_backup()\n" );
      status = REPAIRDB_ERROR;
    }
  }
  else status = REPAIRDB_ERROR;

  return status;
}

static int perform_repair( pkg_db *db, char content_checking ) {
  int status, result;
  claims_list_map_t *m;
  rbtree *t;

  status = REPAIRDB_SUCCESS;
  if ( db ) {
    /*
     * Theory of the repairdb command:
     *
     * The repairdb command is a consistency checker between the database and
     * the package-description files.  It scans over all the existing
     * package-descriptions, and constructs a list of all claims for each
     * filesystem location.  Then it resolves those claims, and produces
     * a list of filesystem locations, each with a single package that should
     * own it.  It compares that last against the actual contents of the
     * package database, and modifies the database accordingly.
     *
     * In more detail, there are three passes:
     *
     * Pass one: identify claims
     *
     * Enumerate the contents of the pkgdir, and identify the
     * installed package-description files.  For each one, load it and
     * iterate over its contents.  For each entry, create a claim
     * record identifying the claim type (directory, file or symlink),
     * name of claiming package, mtime of claiming package, and other
     * identifying information (MD5 for file claims, target for
     * symlink claims).  The output of pass one is an rbtree (wrapped
     * in a claims_list_map_t), where the keys are filesystem
     * locations (char *, relative to rootdir), and the values are
     * claims_list_t *, containing lists of all claims pertaining to
     * that location.  Pass one is independent of the actual database
     * contents.
     *
     * Pass two: resolve claims
     *
     * There are two versions of pass two, specified on the repairdb
     * command line.  In either case, pass two iterates over the
     * claims_list_map_t produced in pass one, and for each set of
     * claims, selects exactly one or rejects all of them, and it
     * returns a new rbtree which has locations as keys and package
     * names selected as values.  The difference between the two modes
     * is whether to check the filesystem contents.  Without content
     * checking, claims are resolved based on the mtimes of the
     * package-description files, in favor of the most recently
     * installed package, without reference to the filesystem.  With
     * content checking, claims are compared against either mtimes of
     * installed files, or, if --enable-md5 is also used (or MD5
     * checking is enabled as a compiled-in default), the MD5s.  The
     * latter case in particular is potentially quite expensive, but
     * is the most accurate method.
     *
     * Pass three: reconstruct database
     *
     * Modify the database in accordance with the claims awarded in
     * pass two.  We enumerate the database and, for each record,
     * check it against the list of claims awarded.  If it does not
     * appear, add it to a list of records to be deleted.  If it does
     * but is for a different package, add it to a list of records to
     * be modified and remove it from the list of claims awarded.  At
     * the end, the remaining records in the list of claims awarded
     * are the ones to be added to the database.  Perform these
     * modifications and we're finished.
     */

    /* Perform pass one, and get a claims_list_map_t out */
    m = repairdb_pass_one();

    if ( m ) {
      printf( "Pass one complete;" );
      printf( " discovered %d claims to %d locations by %d packages\n",
	      m->num_claims, m->num_locations, m->num_packages );

      t = repairdb_pass_two( m, content_checking );

      free_claims_list_map( m );

      if ( t ) {
	printf( "Pass two complete; upheld claims on %lu locations\n",
		t->count );

	result = repairdb_pass_three( db, t );

	if ( result == REPAIRDB_SUCCESS ) {
	  printf( "Pass three complete\n" );
	}
	else {
	  status = result;
	  fprintf( stderr, "Unable to complete pass three of repairdb\n" );
	}

	rbtree_free( t );
      }
      else {
	fprintf( stderr, "Unable to complete pass two of repairdb\n" );
      }
    }
    else {
      fprintf( stderr, "Unable to complete pass one of repairdb\n" );
      status = REPAIRDB_ERROR;
    }
  }
  else status = REPAIRDB_ERROR;

  return status;
}

void repairdb_help( void ) {
  printf( "Repair the package database by reconstructing it from the " );
  printf( "installed package-description files.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] repairdb [(--enable|--disable)-content-" );
  printf( "checking]\n" );
  printf( "\n" );
  printf( "The --enable-content-checking option switches on content " );
  printf( "checking, causing repairdb to examine the contents of the " );
  printf( "filesystem to resolve claims by packages.  If MD5 checking " );
  printf( "(global option --enable-md5) is also specified, this can be very" );
  printf( " expensive.  Without content checking, repairdb only uses the " );
  printf( "mtimes of installed package-description files to determine what" );
  printf( " was most recently installed.\n" );
}

void repairdb_main( int argc, char **argv ) {
  char content_checking, error;

  if ( argc == 0 || argc == 1 ) {
    error = 0;

    if ( argc == 1 ) {
      if ( strcmp( argv[0], "--enable-content-checking" ) == 0 )
	content_checking = 1;
      else if ( strcmp( argv[0], "--disable-content-checking" ) == 0 )
	content_checking = 0;
      else {
	fprintf( stderr, "Unknown parameter %s passed to repairdb\n",
		 argv[0] );
	fprintf( stderr, "Valid parameters are --enable-content-checking " );
	fprintf( stderr, "and --disable-content-checking.\n" );
	error = 1;
      }
    }
    else content_checking = 0;

    if ( !error ) repairdb( content_checking );
  }
  else {
    fprintf( stderr, "Wrong number of arguments to repairdb (%d)\n",
	     argc );
  }
}

static void repairdb( char content_checking ) {
  pkg_db *db;
  int status, result;

  status = REPAIRDB_SUCCESS;

  /*
   * First, open the existing database.  The existence of a database
   * to open is a mandatory condition to repair; if it has been too
   * badly damaged, delete any DB files remaining and create a new one
   * with the createdb command, then run repairdb.
   */

  db = open_pkg_db();
  if ( db ) {
    /*
     * We've got it open; make a backup copy, in a separate location from
     * the usual backups before we do anything to it.
     */

    result = make_repairdb_backup( db );
    if ( result != REPAIRDB_SUCCESS ) {
      status = result;
      fprintf( stderr,
	       "Error: repairdb could not make a backup copy of the " );
      fprintf( stderr,
	       "existing database, and will not proceed without one.\n" );
    }

    if ( status == REPAIRDB_SUCCESS ) {
      result = perform_repair( db, content_checking );
      if ( result != REPAIRDB_SUCCESS ) {
	fprintf( stderr, "Failed to repair database\n" );
	status = result;
      }
    }

    close_pkg_db( db );
  }
  else {
    fprintf( stderr,
	     "The repairdb command was unable to open the existing package" );
    fprintf( stderr,
	     " database; try deleting it and recreating it with createdb, " );
    fprintf( stderr,
	     "then run repairdb again.\n" );
    status = REPAIRDB_ERROR;
  }
}
