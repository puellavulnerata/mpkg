#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pkg.h>

static int make_repairdb_backup( pkg_db * );
static int perform_repair( pkg_db * );
static void repairdb( void );

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

static int perform_repair( pkg_db *db ) {
  int status;
  claims_list_map_t *m;

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
     */

    /* Perform pass one, and get a claims_list_map_t out */
    m = repairdb_pass_one();

    if ( m ) {
      printf( "Pass one complete;" );
      printf( " discovered %d claims to %d locations by %d packages\n",
	      m->num_claims, m->num_locations, m->num_packages );
      free_claims_list_map( m );
    }
    else {
      fprintf( stderr, "Unable to complete pass one of repairdb\n" );
      status = REPAIRDB_ERROR;
    }
  }
  else status = REPAIRDB_ERROR;

  return status;
}

void repairdb_main( int argc, char **argv ) {
  if ( argc == 0 ) {
    repairdb();
  }
  else {
    fprintf( stderr, "Wrong number of arguments to repairdb (%d)\n",
	     argc );
  }
}

static void repairdb( void ) {
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
      result = perform_repair( db );
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
