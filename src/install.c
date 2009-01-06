#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <pkg.h>

#define INSTALL_SUCCESS 0
#define INSTALL_ERROR -1
#define INSTALL_OUT_OF_DISK -2

typedef struct {
  uid_t owner;
  gid_t group;
  mode_t mode;
  /* Whether to claim this dir for the package */
  char claim;
  /* Whether to delete on unroll */
  char unroll;
} dir_descr;

typedef struct {
  /* Temporary name for old package-description, created in pass one */
  char *old_descr;
  /*
   * Directories created in pass two; keys are char * (canonicalized
   * pathnames) and values are dir_descr *
   */
  rbtree *pass_two_dirs;
} install_state;

static install_state * alloc_install_state( void );
static void * copy_dir_descr( void * );
static int do_install_descr( pkg_handle *, install_state * );
static int do_preinst_dirs( pkg_handle *, install_state * );
static int do_preinst_one_dir( install_state *, pkg_descr_entry * );
static void free_dir_descr( void * );
static void free_install_state( install_state * );
static int install_pkg( pkg_db *, pkg_handle * );
static int rollback_install_descr( pkg_handle *, install_state * );
static int rollback_preinst_dirs( pkg_handle *, install_state * );

static install_state * alloc_install_state( void ) {
  install_state *is;

  is = malloc( sizeof( *is ) );
  if ( is ) {
    is->old_descr = NULL;
    is->pass_two_dirs = NULL;
  }

  return is;
}

static void * copy_dir_descr( void *dv ) {
  dir_descr *d, *dcpy;

  dcpy = NULL;
  d = (dir_descr *)dv;
  if ( d ) {
    dcpy = malloc( sizeof( *dcpy ) );
    if ( dcpy ) {
      dcpy->owner = d->owner;
      dcpy->group = d->group;
      dcpy->mode = d->mode;
      dcpy->claim = d->claim;
      dcpy->unroll = d->unroll;
    }
  }

  return dcpy;
}

static int do_install_descr( pkg_handle *p,
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
  if ( p && is ) {
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
	  /*
	   * We couldn't stat, but not because it doesn't exist.  This
	   * is bad.
	   */

	  fprintf( stderr, "Couldn't stat %s/%s: %s\n",
		   get_pkg(), pkg_name, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
      }

      if ( status == INSTALL_SUCCESS ) {
	/*
	 * We renamed the old description if necessary.  Now install
	 * the new one.
	 */

	result = write_pkg_descr_to_file( p->descr, pkg_name );
	if ( result != 0 ) {
	  fprintf( stderr, "Couldn't write package description to %s/%s\n",
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

static int do_preinst_dirs( pkg_handle *p,
			    install_state *is ) {
  int status, result, i;
  pkg_descr *desc;
  pkg_descr_entry *e;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    desc = p->descr;
    for ( i = 0; i < desc->num_entries; ++i ) {
      e = desc->entries + i;
      if ( e->type == ENTRY_DIRECTORY ) {
	result = do_preinst_one_dir( is, e );
	if ( result != INSTALL_SUCCESS ) {
	  status = result;
	  break;
	}
      }
    }
  }
  else status = INSTALL_ERROR;
  return status;
}

static int do_preinst_one_dir( install_state *is,
			       pkg_descr_entry *e ) {
  int status, result, record_dir;
  char *p, *pcomp, *next, *temp, *currpath, *currpath_end, *currcomp;
  void *stringbuf;
  struct stat st;
  dir_descr dd;
  uid_t owner;
  gid_t group;
  struct passwd *pwd;
  struct group *grp;

  status = INSTALL_SUCCESS;
  if ( is && e ) {
    if ( e->type == ENTRY_DIRECTORY ) {
      pwd = getpwnam( e->owner );
      if ( pwd ) owner = pwd->pw_uid;
      else {
	/* Error or not found, default to 0 */
	owner = 0;
      }

      grp = getgrnam( e->group );
      if ( grp ) group = grp->gr_gid;
      else {
	/* Error or not found, default to 0 */
	group = 0;
      }

      p = canonicalize_and_copy( e->filename );
      if ( p ) {
	currpath = malloc( sizeof( *currpath ) * ( strlen( p ) + 1 ) );
	if ( currpath ) {
	  result = chdir( get_root() );
	  if ( result == 0 ) {
	    pcomp = get_path_component( p, &temp );

	    currpath_end = currpath;
	    while ( pcomp && status == INSTALL_SUCCESS ) {
	      sprintf( currpath_end, "/%s", pcomp );
	      currpath_end += strlen( pcomp ) + 1;

	      currcomp = copy_string( pcomp );
	      if ( currcomp ) {
		next = get_path_component( NULL, &temp );
		record_dir = 0;
		result = lstat( currcomp, &st );
		if ( result == 0 ) {
		  if ( S_ISDIR( st.st_mode ) ) {
		    /*
		     * This directory already exists:
		     *
		     * If this is the last path component, claim it.
		     * In any case, chdir to it.
		     */

		    if ( !next ) {
		      dd.unroll = 0;
		      dd.claim = 1;
		      dd.owner = owner;
		      dd.group = group;
		      dd.mode = e->u.d.mode;
		      record_dir = 1;
		    }

		    result = chdir( currcomp );
		    if ( result != 0 ) {
		      fprintf( stderr, "Error: couldn't chdir to %s%s: %s\n",
			       get_root(), currpath, strerror( errno ) );
		      status = INSTALL_ERROR;
		    }
		  }
		  else {
		    /*
		     * Something else already exists!  This is an error.
		     */
		    fprintf( stderr,
			     "Error: %s%s exists and is not a directory.\n",
			     get_root(), currpath );
		    status = INSTALL_ERROR;
		  }
		}
		else {
		  if ( errno == ENOENT ) {
		    /*
		     * This component doesn't exist:
		     *
		     * Create a directory, and mark it for unrolling.  If
		     * this is the last component, also claim it.
		     */

		    dd.unroll = 1;
		    if ( next ) {
		      dd.claim = 0;
		      /*
		       * Create directories owned by root/root, mode 0755
		       * by default
		       */

		      dd.owner = 0;
		      dd.group = 0;
		      dd.mode = 0755;
		    }
		    else {
		      dd.claim = 1;
		      dd.owner = owner;
		      dd.group = group;
		      dd.mode = e->u.d.mode;
		    }

		    result = mkdir( currcomp, 0700 );
		    if ( result == 0 ) {
		      result = chdir( currcomp );
		      if ( result == 0 ) {
			record_dir = 1;
		      }
		      else {
			fprintf( stderr,
				 "Error: couldn't chdir to %s%s: %s\n",
				 get_root(), currpath, strerror( errno ) );
			status = INSTALL_ERROR;
		      }
		    }
		    else {
		      fprintf( stderr, "Error: couldn't mkdir %s%s: %s\n",
			       get_root(), currpath, strerror( errno ) );
		      if ( errno == ENOSPC ) status = INSTALL_OUT_OF_DISK;
		      else status = INSTALL_ERROR;
		    }
		  }
		  else {
		    /*
		     * Some other error; p is NULL-terminated after pcomp
		     * right now
		     */
		    fprintf( stderr, "Error: couldn't stat %s%s: %s\n",
			     get_root(), currpath, strerror( errno ) );
		    status = INSTALL_ERROR;
		  }
		}

		if ( record_dir ) {
		  if ( !(is->pass_two_dirs) ) {
		    is->pass_two_dirs =
		      rbtree_alloc( rbtree_string_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    copy_dir_descr,
				    free_dir_descr );
		  }
		
		  if ( is->pass_two_dirs ) {
		    result = rbtree_insert( is->pass_two_dirs, currpath,
					    &dd );
		    if ( result != RBTREE_SUCCESS ) {
		      fprintf( stderr, "Couldn't insert into rbtree.\n" );
		      status = INSTALL_ERROR;
		    }
		  }
		  else {
		    fprintf( stderr, "Couldn't allocate rbtree.\n" );
		    status = INSTALL_ERROR;
		  }
		}

		free( currcomp );
	      }
	      else status = INSTALL_ERROR;

	      pcomp = next;
	    }
	  }
	  else {
	    fprintf( stderr, "Couldn't chdir to install root %s!\n",
		     get_root() );
	    status = INSTALL_ERROR;
	  }

	  free( currpath );
	}
	else status = INSTALL_ERROR;

	free( p );
      }
      else status = INSTALL_ERROR;
    }
    else status = INSTALL_ERROR;
  }
  else status = INSTALL_ERROR;

  return status;
}

static void free_dir_descr( void *dv ) {
  if ( dv ) free( dv );
}

static void free_install_state( install_state *is ) {
  if ( is ) {
    if ( is->old_descr ) {
      free( is->old_descr );
      is->old_descr = NULL;
    }
    if ( is->pass_two_dirs ) {
      rbtree_free( is->pass_two_dirs );
      is->pass_two_dirs = NULL;
    }
    free( is );
  }
}

void install_main( int argc, char **argv ) {
  pkg_db *db;
  int i, status;
  pkg_handle *p;

  if ( argc > 0 ) {
    status = sanity_check_globals();
    if ( status == 0 ) {
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
    /* else sanity_check_globals() will emit a warning */
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
      status = do_install_descr( p, is );
      if ( status != INSTALL_SUCCESS ) goto err_install_descr;
      /* Pass two */
      status = do_preinst_dirs( p, is );
      if ( status != INSTALL_SUCCESS ) goto err_preinst_dirs;

      printf( "Finished do_preinst_dirs()\n" );

      /* TODO - Passes three through eight */
      goto success;

    err_preinst_dirs:
      rollback_preinst_dirs( p, is );

      printf( "Finished rollback_preinst_dirs()\n" );
    err_install_descr:
      rollback_install_descr( p, is );

    success:
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

static int rollback_install_descr( pkg_handle *p,
				   install_state *is ) {
  int status, result;
  char *pkg_name;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    result = chdir( get_pkg() );
    if ( result == 0 ) {
      pkg_name = p->descr->hdr.pkg_name;
      unlink( pkg_name );
      if ( is->old_descr ) {
	/*
	 * Don't bother checking for errors, since this is rollback
	 * and we can't do anything about them anyway
	 */

	link( is->old_descr, pkg_name );
	unlink( is->old_descr );
	free( is->old_descr );
	is->old_descr = NULL;
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

static int rollback_preinst_dirs( pkg_handle *p, install_state *is ) {
  rbtree_node *n;
  char *path, *full_path;
  dir_descr *descr;
  void *descr_v;
  int status;

  status = INSTALL_SUCCESS;
  if ( is->pass_two_dirs ) {
    n = NULL;
    do {
      descr = NULL;
      path = rbtree_enum( is->pass_two_dirs, n, &descr_v, &n );
      if ( path ) {
	if ( descr_v ) {
	  descr = (dir_descr *)descr_v;
	  if ( descr->unroll ) {
	    /*
	     * We need to unroll this one.  We might see it before any
	     * subdirectories that also need to be unrolled, so just
	     * recrm() it, and we might have already recrm()ed a
	     * parent, so it won't exist, so ignore any errors. This
	     * is unroll and we can't do anything about them anyway.
	     */
	    full_path = malloc( sizeof( *full_path ) *
				( strlen( get_root() ) +
				  strlen( path ) + 2 ) );
	    if ( full_path ) {
	      sprintf( full_path, "%s/%s", get_root(), path );
	      if ( canonicalize_path( full_path ) == 0 ) {
		recrm( full_path );
	      }
	      else {
		fprintf( stderr,
			 "rollback_preinst_dirs() couldn't canonicalize a path.\n" );
		status = INSTALL_ERROR;
	      }
	      free( full_path );
	    }
	    else {
	      fprintf( stderr,
		       "rollback_preinst_dirs() couldn't allocate memory\n" );
	      status = INSTALL_ERROR;
	    }
	  }
	}
	else {
	  fprintf( stderr,
		   "rollback_preinst_dirs() saw path %s but NULL descr\n",
		   path );
	  status = INSTALL_ERROR;
	}
      }
    } while ( n );

    rbtree_free( is->pass_two_dirs );
    is->pass_two_dirs = NULL;
  }

  return status;
}
