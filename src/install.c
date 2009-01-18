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
  /*
   * Full canonical path (but not including instroot) to temp name
   * where the file resides
   */
  char *temp_file;
  /* Desired owner/group/mode/mtime of installed file */
  uid_t owner;
  gid_t group;
  mode_t mode;
  time_t mtime;
} file_descr;

typedef struct {
  /* Temporary name for old package-description, created in pass one */
  char *old_descr;
  /*
   * Directories created in pass two; keys are char * (canonicalized
   * pathnames not including instroot) and values are dir_descr *
   */
  rbtree *pass_two_dirs;
  /*
   * Directories created in pass three: keys are char * (canonicalized
   * pathnames not including instroot) and values are dir_descr *;
   * claim is always set to zero.
   */
  rbtree *pass_three_dirs;
  /*
   * Temporary files created in pass three: keys are char *
   * (canonicalized pathnames of targets not including instroot) and
   * values are file_descr *.
   */
  rbtree *pass_three_files;
} install_state;

static install_state * alloc_install_state( void );
static void * copy_dir_descr( void * );
static void * copy_file_descr( void * );
static int create_dirs_as_needed( const char *, rbtree ** );
static int do_install_descr( pkg_handle *, install_state * );
static int do_preinst_dirs( pkg_handle *, install_state * );
static int do_preinst_files( pkg_handle *, install_state * );
static int do_preinst_one_dir( install_state *, pkg_descr_entry * );
static int do_preinst_one_file( install_state *, pkg_handle *,
				pkg_descr_entry * );
static void free_dir_descr( void * );
static void free_file_descr( void * );
static void free_install_state( install_state * );
static int install_pkg( pkg_db *, pkg_handle * );
static int rollback_dir_set( rbtree ** );
static int rollback_file_set( rbtree ** );
static int rollback_install_descr( pkg_handle *, install_state * );
static int rollback_preinst_dirs( pkg_handle *, install_state * );
static int rollback_preinst_files( pkg_handle *, install_state * );

static install_state * alloc_install_state( void ) {
  install_state *is;

  is = malloc( sizeof( *is ) );
  if ( is ) {
    is->old_descr = NULL;
    is->pass_two_dirs = NULL;
    is->pass_three_dirs = NULL;
    is->pass_three_files = NULL;
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

static void * copy_file_descr( void *fv ) {
  file_descr *f, *fcpy;

  fcpy = NULL;
  f = (file_descr *)fv;
  if ( f ) {
    fcpy = malloc( sizeof( *fcpy ) );
    if ( fcpy ) {
      fcpy->owner = f->owner;
      fcpy->group = f->group;
      fcpy->mode = f->mode;
      fcpy->mtime = f->mtime;
      if ( f->temp_file ) {
	fcpy->temp_file = copy_string( f->temp_file );
	if ( !(fcpy->temp_file) ) {
	  /* Alloc error */
	  free( fcpy );
	  fcpy = NULL;
	}
      }
      else fcpy->temp_file = NULL;
    }
  }

  return fcpy;
}

/*
 * int create_dirs_as_needed( const char *path, rbtree **dirs );
 *
 * This function creates directories as needed to contain path, but
 * not path itself, under instroot, and records entries for them in
 * the rbtree of dir_descr records dirs.
 */

static int create_dirs_as_needed( const char *path, rbtree **dirs ) {
  char *p, *currpath, *currpath_end, *currcomp, *next, *pcomp, *temp;
  dir_descr dd;
  struct stat st;
  int status, result, record_dir;

  status = INSTALL_SUCCESS;
  if ( path && dirs ) {
    p = canonicalize_and_copy( path );
    if ( p ) {
      currpath = malloc( sizeof( *currpath ) * ( strlen( p ) + 1 ) );
      if ( currpath ) {
	result = chdir( get_root() );
	if ( result == 0 ) {
	  pcomp = get_path_component( p, &temp );
	  
	  /* currpath tracks the part of the path we're up to thus far */

	  currpath_end = currpath;
	  while ( pcomp && status == INSTALL_SUCCESS ) {
	    sprintf( currpath_end, "/%s", pcomp );
	    currpath_end += strlen( pcomp ) + 1;

	    currcomp = copy_string( pcomp );
	    if ( currcomp ) {
	      next = get_path_component( NULL, &temp );
	      if ( next ) {
		/*
		 * We don't do anything if we're on the last
		 * component; that's for the caller to worry about.
		 */
		record_dir = 0;
		result = lstat( currcomp, &st );
		if ( result == 0 ) {
		  if ( S_ISDIR( st.st_mode ) ) {
		    /*
		     * This directory already exists:
		     *
		     * Go ahead and chdir to it.
		     */

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
		else { /* The stat failed.  Did it not exist? */
		  if ( errno == ENOENT ) {
		    /*
		     * This component doesn't exist:
		     *
		     * Create a directory, and mark it for unrolling.
		     */

		    dd.unroll = 1;
		    dd.claim = 0;

		    /*
		     * Create directories owned by root/root, mode 0755
		     * by default
		     */

		    dd.owner = 0;
		    dd.group = 0;
		    dd.mode = 0755;

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

		/*
		 * At this point, we've either created and chdir()ed
		 * to the needed directory, or errored.  Now we just
		 * need to record it.
		 */

		if ( record_dir ) {
		  if ( !(*dirs) ) {
		    *dirs =
		      rbtree_alloc( rbtree_string_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    copy_dir_descr,
				    free_dir_descr );
		  }
		
		  if ( *dirs ) {
		    result = rbtree_insert( *dirs, currpath,
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
	      }

	      free( currcomp );
	    }
	    else status = INSTALL_ERROR;

	    /* The while loop ends here; pcomp gets next */

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
  
  return status;
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

static int do_preinst_files( pkg_handle *p, install_state *is ) {
  int status, result, i;
  pkg_descr *desc;
  pkg_descr_entry *e;
  char *temp;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    desc = p->descr;
    for ( i = 0; i < desc->num_entries; ++i ) {
      e = desc->entries + i;
      if ( e->type == ENTRY_FILE ) {
	result = do_preinst_one_file( is, p, e );
	if ( result != INSTALL_SUCCESS ) {
	  temp = concatenate_paths( get_root(), e->filename );
	  if ( temp ) {
	    fprintf( stderr, "Couldn't preinstall %s\n", temp );
	    free( temp );
	  }
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
  char *p, *lastcomp;
  dir_descr dd;
  struct stat st;
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

      /*
       * Create all needed dirs enclosing this path and record for
       * possible later unroll
       */
      result = create_dirs_as_needed( e->filename, &(is->pass_two_dirs) );

      if ( result == INSTALL_SUCCESS ) {
	/*
	 * We should be chdir()ed to the directory enclosing this
	 * path, so we just need to get the last component and mkdir
	 * and record.
	 */

	p = canonicalize_and_copy( e->filename );
	if ( p ) {
	  lastcomp = get_last_component( p );
	  if ( lastcomp ) {
	    record_dir = 0;

	    result = lstat( lastcomp, &st );
	    if ( result == 0 ) {
	      /* It already exists */
	      if ( S_ISDIR( st.st_mode ) ) {
		/*
		 * It's a directory, so we're finished here.  Just claim
		 * it.
		 */

		record_dir = 1;
		dd.owner = owner;
		dd.group = group;
		dd.mode = e->u.d.mode;
		dd.unroll = 0;
		dd.claim = 1;
	      }
	      else {
		/* It's not a directory.  This is an error. */

		fprintf( stderr, "%s%s: already exists but not a directory\n",
			 get_root(), p );
		status = INSTALL_ERROR;
	      }
	    }
	    else {
	      if ( errno == ENOENT ) {
		/* It doesn't exist, we create it. */

		result = mkdir( lastcomp, 0700 );
		if ( result == 0 ) {
		  record_dir = 1;
		  dd.owner = owner;
		  dd.group = group;
		  dd.mode = e->u.d.mode;
		  dd.unroll = 1;
		  dd.claim = 1;
		}
		else {
		  /* Error in mkdir() */
		  fprintf( stderr,
			   "%s%s: couldn't mkdir(): %s\n",
			   get_root(), p, strerror( errno ) );

		  if ( errno == ENOSPC ) status = INSTALL_OUT_OF_DISK;
		  else status = INSTALL_ERROR;
		}
	      }
	      else {
		/* Some other error in lstat() */

		fprintf( stderr, "%s%s: couldn't lstat(): %s\n",
			 get_root(), p, strerror( errno ) );
		status = INSTALL_ERROR;
	      }
	    }

	    /* Now we just record it if necessary */

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
		result = rbtree_insert( is->pass_two_dirs, p, &dd );
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

	    free( lastcomp );
	  }
	  else status = INSTALL_ERROR;

	  free( p );
	}
	else status = INSTALL_ERROR;
      }
      else status = result;
    }
    else status = INSTALL_ERROR;
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_preinst_one_file( install_state *is,
				pkg_handle *pkg,
				pkg_descr_entry *e ) {
  int status, result, tmpfd;
  uid_t owner;
  gid_t group;
  struct passwd *pwd;
  struct group *grp;
  char *p, *format, *src, *lastcomp, *base, *temp;
  int format_len;
  file_descr fd;

  status = INSTALL_SUCCESS;
  if ( is && pkg && e ) {
    if ( e->type == ENTRY_FILE ) {
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
	/*
	 * p is the canonical pathname of the target, not including
	 * instroot.  It will be the key for the is->pass_three_files
	 * entry.
	 */

	/*
	 * Create all needed dirs enclosing this path and record for
	 * possible later unroll
	 */
	result = create_dirs_as_needed( e->filename, &(is->pass_three_dirs) );

	if ( result == INSTALL_SUCCESS ) {
	  /*
	   * We should be chdir()ed to the directory enclosing this
	   * path, so we just need to create a temporary and record.
	   */

	  src = NULL;
	  temp = concatenate_paths( pkg->unpacked_dir, "package-content" );
	  if ( temp ) {
	    src = concatenate_paths( temp, e->filename );
	    free( temp );
	  }

	  if ( src ) {
	    base = get_base_path( p );
	    lastcomp = get_last_component( p );
	    if ( base && lastcomp ) {
	      format_len = strlen( lastcomp ) + 32;
	      format = malloc( sizeof( *format ) * format_len );
	      if ( format ) {
		snprintf( format, format_len, ".%s.mpkg.%d.XXXXXX",
			  lastcomp, getpid() );

		tmpfd = mkstemp( format );

		if ( tmpfd != -1 ) {
		  /*
		   * Okay, we've got a temp, and its name is now in
		   * format.  Clear out the temp, and try to hard-link
		   * from the appropriate place.
		   */
		  close( tmpfd );
		  unlink( format );
		  /* Try to link src to format, copy if not possible. */
		  result = link_or_copy( format, src );

		  if ( result == LINK_OR_COPY_SUCCESS ) {
		    fd.owner = owner;
		    fd.group = group;
		    fd.mode = e->u.f.mode;
		    fd.mtime = pkg->descr->hdr.pkg_time;
		    fd.temp_file = concatenate_paths( base, format );
		    if ( fd.temp_file ) {
		      if ( !(is->pass_three_files) ) {
			is->pass_three_files =
			  rbtree_alloc( rbtree_string_comparator,
					rbtree_string_copier,
					rbtree_string_free,
					copy_file_descr,
					free_file_descr );
		      }
		
		      if ( is->pass_three_files ) {
			result = rbtree_insert( is->pass_three_files, p, &fd );
			if ( result != RBTREE_SUCCESS ) {
			  fprintf( stderr, "Couldn't insert into rbtree.\n" );
			  status = INSTALL_ERROR;
			}
		      }
		      else {
			fprintf( stderr, "Couldn't allocate rbtree.\n" );
			status = INSTALL_ERROR;
		      }

		      /* 
		       * If the insert went okay it copied
		       * fd.temp_file, and if it failed we don't need
		       * it any longer, so we can free it now.
		       */

		      free( fd.temp_file );
		    }
		    else status = INSTALL_ERROR;

		    /*
		     * If we failed somewhere, be sure to delete the
		     * tempfile
		     */

		    if ( status != INSTALL_SUCCESS ) unlink( format );
		  }
		  else if ( result == LINK_OR_COPY_OUT_OF_DISK )
		    status = INSTALL_OUT_OF_DISK;
		  else status = INSTALL_ERROR;
		}
		else status = INSTALL_ERROR;

		free( format );
	      }
	      else status = INSTALL_ERROR;

	      free( base );
	      free( lastcomp );
	    }
	    else {
	      if ( base ) free( base );
	      if ( lastcomp ) free( lastcomp );
	    }

	    free( src );
	  }
	  else status = INSTALL_ERROR;
	}
	else {
	  fprintf( stderr,
		   "do_preinst_one_file(): couldn't create enclosing directories for install target %s\n",
		   e->filename );
	  status = result;
	}

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

static void free_file_descr( void *fv ) {
  file_descr *f;

  if ( fv ) {
    f = (file_descr *)fv;
    if ( f->temp_file ) free( f->temp_file );
    free( f );
  }
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
    if ( is->pass_three_dirs ) {
      rbtree_free( is->pass_three_dirs );
      is->pass_three_dirs = NULL;
    }
    if ( is->pass_three_files ) {
      rbtree_free( is->pass_three_files );
      is->pass_three_files = NULL;
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
      /* Pass three */
      status = do_preinst_files( p, is );
      if ( status != INSTALL_SUCCESS ) goto err_preinst_files;

      /* TODO - Passes four through eight */

      goto success;

    err_preinst_files:
      rollback_preinst_files( p, is );

    err_preinst_dirs:
      rollback_preinst_dirs( p, is );

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

static int rollback_dir_set( rbtree **dirs ) {
  rbtree_node *n;
  char *path, *full_path;
  dir_descr *descr;
  void *descr_v;
  int status;

  status = INSTALL_SUCCESS;
  if ( dirs ) {
    if ( *dirs ) {
      n = NULL;
      do {
	descr = NULL;
	path = rbtree_enum( *dirs, n, &descr_v, &n );
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
	      full_path = concatenate_paths( get_root(), path );
	      if ( full_path ) {
		recrm( full_path );
		free( full_path );
	      }
	      else {
		fprintf( stderr,
			 "rollback_dir_set() couldn't allocate memory\n" );
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

      rbtree_free( *dirs );
      *dirs = NULL;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static int rollback_file_set( rbtree **files ) {
  int status;
  rbtree_node *n;
  file_descr *descr;
  void *descr_v;
  char *target;
  char *full_path;

  status = INSTALL_SUCCESS;
  if ( files ) {
    if ( *files ) {
      n = NULL;
      do {
	descr = NULL;
	target = rbtree_enum( *files, n, &descr_v, &n );
	if ( target ) {
	  if ( descr_v ) {
	    descr = (file_descr *)descr_v;
	    if ( descr->temp_file ) {
	      full_path = concatenate_paths( get_root(), descr->temp_file );
	      if ( full_path ) {
		unlink( full_path );
		free( full_path );
	      }
	      else {
		fprintf( stderr,
			 "rollback_file_set() couldn't construct full path for %s\n",
			 descr->temp_file );
		status = INSTALL_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr,
		       "rollback_file_set() saw target %s with NULL temp_file\n",
		       target );
	      status = INSTALL_ERROR;
	    }
	  }
	  else {
	    fprintf( stderr,
		     "rollback_file_set() saw target %s but NULL descr\n",
		     target );
	    status = INSTALL_ERROR;
	  }
	}
      } while ( n );

      rbtree_free( *files );
      *files = NULL;
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
  int status;

  if ( p && is ) status = rollback_dir_set( &(is->pass_two_dirs) );
  else status = INSTALL_ERROR;

  return status;
}

static int rollback_preinst_files( pkg_handle *p, install_state *is ) {
  int status, result;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    result = rollback_file_set( &(is->pass_three_files) );
    if ( status == INSTALL_SUCCESS && result != INSTALL_SUCCESS )
      status = result;

    result = rollback_dir_set( &(is->pass_three_dirs) );
    if ( status == INSTALL_SUCCESS && result != INSTALL_SUCCESS )
      status = result;
  }
  else status = INSTALL_ERROR;

 err_out:
  return status;
}
