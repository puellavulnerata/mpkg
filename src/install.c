#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include <pkg.h>

#define INSTALL_SUCCESS 0
#define INSTALL_ERROR -1
#define INSTALL_OUT_OF_DISK -2

typedef struct {
  /* Temporary name for old package-description, created in pass one */
  char *old_descr;
} install_state;

static install_state * alloc_install_state( void );
static int do_install_descr( pkg_db *, pkg_handle *, install_state * );
static void free_install_state( install_state * );
static int install_pkg( pkg_db *, pkg_handle * );

static install_state * alloc_install_state( void ) {
  install_state *is;

  is = malloc( sizeof( *is ) );
  if ( is ) {
    is->old_descr = NULL;
  }

  return is;
}

static int do_install_descr( pkg_db *db,
			     pkg_handle *p,
			     install_state *is ) {
  /*
   * Install the package-description in pkgdir.  We check if an
   * existing description file for this package name is present; if so
   * we rename it to a temporary and store that in the install_state
   * first.
   */

  int status, result;
  char *pkg_name, *temp_name;
  struct stat stat_buf;

  status = INSTALL_SUCCESS;
  if ( db && p && is ) {
    result = chdir( get_pkg() );
    if ( result == 0 ) {
      pkg_name = p->descr->hdr.pkg_name;
      result = stat( pkg_name, &stat_buf );
      if ( result == 0 ) {
	/*
	 * The stat() succeeded, so something already exists with that
	 * name.
	 */

	if ( S_ISREG( stat_buf.st_mode ) ) {
	  temp_name = rename_to_temp( pkg_name );
	  if ( temp_name ) is->old_descr = temp_name;
	  else {
	    /* Failed to rename it */
	    
	    fprintf( stderr,
		     "Couldn't move an existing file %s/%s\n",
		     get_pkg(), pkg_name );
	    status = INSTALL_ERROR;
	  }
	}
	else {
	  fprintf( stderr,
		   "Error: existing package description %s/%s isn't a regular file\n",
		   get_pkg(), pkg_name );
	  status = INSTALL_ERROR;
	}
      }
      else {
	if ( errno != ENOENT ) {
	  /* We couldn't stat, but not because it doesn't exist.  This is bad. */

	  fprintf( stderr, "Couldn't stat %s/%s: %s\n",
		   get_pkg(), pkg_name, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
      }

      if ( status == INSTALL_SUCCESS ) {
	/* We renamed the old description if necessary.  Now install the new one. */

	result = write_pkg_descr_to_file( p->descr, pkg_name );
	if ( result != 0 ) {
	  fprintf( stderr, "Couldn't write package description %to %s/s\n",
		   get_pkg(), pkg_name );
	  /*
	   * A write failure at this point means either we don't have
	   * permission to write in that directory, or we're out of
	   * disk space.
	   */

	  status = INSTALL_OUT_OF_DISK;
	}
      }
    }
    else {
      fprintf( stderr,
	       "Couldn't chdir to package directory %s!\n",
	       get_pkg() );
      status = INSTALL_ERROR;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static void free_install_state( install_state *is ) {
  if ( is ) {
    if ( is->old_descr ) {
      free( is->old_descr );
      is->old_descr = NULL;
    }
    free( is );
  }
}

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
  /*
   *
   * Theory of the package installer:
   *
   * We want to make sure we can always back out of package
   * installations if we run out of disk space.  Therefore, if we need
   * to delete any existing files, we will do so only after we can't
   * possibly fail for this reason.
   *
   * We proceed in several passes:
   *
   * 1.) Install a copy of the package-description in pkgdir; if one
   * already existed rename it to a temporary name and keep a record
   * of this.
   *
   * 2.) Iterate through the directory list in the package
   * description; if any directories it specifies do not exist, create
   * them with mode 0700, and keep a list of directories we create at
   * this step so we can roll back later.
   *
   * 3.) Iterate through the file list in the package.  For each file,
   * copy it to a temporary file in the directory it will be installed
   * in, and keep a list of temporary files and names to eventually
   * install to.
   *
   * 4.) Iterate through the list of symlinks in the package.  If
   * nothing with that name already exists, create the symlink.  If
   * something exists, rename to a temporary name.  Record the list of
   * links created and renames performed.
   *
   * At this point, we have created everything we need to in the
   * location it needs to be installed in, so we are at a maximum of
   * disk space consumption.  If we get here, we can now begin making
   * the installation permanent in the next phases.
   *
   * 5.) Iterate over the directory list in the package installer
   * again; every directory in it already exists due to pass two.
   * Adjust the mode, owner and group of these directories to match,
   * and assert ownership of them in the package db.
   *
   * 6.) Iterate through the list of temporaries we create in pass
   * three.  For each one, test if something already exists in with
   * the name we are installing to.  If something does exist, check if
   * it is a directory.  If not, unlink it.  If so, recurse down it
   * and remove its contents, making sure to also remove entries from
   * the package db.  Now that the installation path is clear, link
   * the temporary to the new name, and then unlink the temporary.
   * Assert ownership of this path in the package db.
   *
   * 7.) Iterate through the list of renames from pass 4, removing
   * them and their package db entries as needed.  Iterate through the
   * list of links created, creating package db entries.
   *
   * 8.) If we renamed an existing package-description for this
   * package in phase one, load it, and iterate over its contents.
   * For each item, check if ownership was asserted in the package db,
   * and if we installed at some point.  If it was owned and we did
   * not install it, remove it.  Delete the old package-description.
   *
   * At this point, all package files are in place and the package db
   * is updated.  We're done.
   *
   * If at some point in passes one through four we encounter an
   * error, it's possible to roll back.  All errors due to
   * insufficient disk space will occur at this point.  Roll back
   * according to the following procedures for each pass:
   *
   * 4.) Iterate through the list of links created, removing them.
   * Iterate through the list of renames performed, renaming things
   * back to their original names.
   *
   * 3.) Iterate through the list of temporary files created, deleting
   * them.
   *
   * 2.) Iterate through the list of directories created, deleting
   * them.
   *
   * 1.) Delete the package-description in pkgdir, and restore any old
   * package-description that was renamed.
   *
   */

  int status, result;
  install_state *is;

  status = INSTALL_SUCCESS;
  if ( db && p ) {
    is = alloc_install_state();
    if ( is ) {
      /* Pass one */
      status = do_install_descr( db, p, is );
      /* TODO */
      free_install_state( is );
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory installing %s\n",
	       p->descr->hdr.pkg_name );
      status = INSTALL_ERROR;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}
