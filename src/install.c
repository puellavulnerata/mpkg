#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <utime.h>

#include <pkg.h>

#define INSTALL_SUCCESS 0
#define INSTALL_ERROR -1
#define INSTALL_OUT_OF_DISK -2

typedef struct {
  uid_t owner;
  gid_t group;
  mode_t mode;
  time_t mtime;
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
  /*
   * Full canonical path (but not including instroot) to temp name
   * where the placeholder symlink was created
   */
  char *temp_symlink;
  /* Desired owner/group/mtime of installed symlink */
  uid_t owner;
  gid_t group;
  time_t mtime;
} symlink_descr;

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
  /*
   * Directories created in pass four: keys are char * (canonicalized
   * pathnames not including instroot) and values are dir_descr *;
   * claim is always set to zero.
   */
  rbtree *pass_four_dirs;
  /*
   * Temporary symlinks created in pass four: keys are char *
   * (canonicalized pathnames of install names not including instroot)
   * and values are symlink_descr *.
   */
  rbtree *pass_four_symlinks;
  /*
   * Canonicalized pathnames (not including instroot) claimed by
   * passes five through seven.  This is used by pass eight to remove
   * existing files.  The keys are char * and the values are always
   * NULL.  This is only created if pass one flags an existing
   * package-description for this package name.
   */
  rbtree *pass_eight_names_installed;
  /*
   * Directories in need of having their mtimes adjusted after every
   * other pass has finished adding/removing things.  This is created
   * in pass five and used in pass nine.  The keys are char *
   * (canonicalized pathnames to directories not including instroot)
   * and the values are dir_descr *.
   */
  rbtree *pass_nine_dirs_to_process;
} install_state;

static int adjust_dir_mtimes( pkg_db *, pkg_handle *, install_state * );
static install_state * alloc_install_state( void );
static void * copy_dir_descr( void * );
static void * copy_file_descr( void * );
static void * copy_symlink_descr( void * );
static int create_dirs_as_needed( pkg_handle *, const char *, rbtree ** );
static int do_install_descr( pkg_handle *, install_state * );
static int do_install_dirs( pkg_db *, pkg_handle *, install_state * );
static int do_install_files( pkg_db *, pkg_handle *, install_state * );
static int do_install_one_dir( pkg_db *, pkg_handle *, install_state *, 
			       char *, dir_descr * );
static int do_install_one_file( pkg_db *, pkg_handle *, install_state *, 
				char *, file_descr * );
static int do_install_one_symlink( pkg_db *, pkg_handle *, install_state *, 
				   char *, symlink_descr * );
static int do_install_symlinks( pkg_db *, pkg_handle *, install_state * );
static int do_preinst_dirs( pkg_handle *, install_state * );
static int do_preinst_files( pkg_handle *, install_state * );
static int do_preinst_one_dir( install_state *, pkg_handle *,
			       pkg_descr_entry * );
static int do_preinst_one_file( install_state *, pkg_handle *,
				pkg_descr_entry * );
static int do_preinst_one_symlink( install_state *, pkg_handle *,
				   pkg_descr_entry * );
static int do_preinst_symlinks( pkg_handle *, install_state * );
static void free_dir_descr( void * );
static void free_file_descr( void * );
static void free_install_state( install_state * );
static void free_symlink_descr( void * );
static int handle_dir_replace( pkg_db *, pkg_descr_entry * );
static int handle_file_replace( pkg_db *, pkg_descr *, pkg_descr_entry * );
static int handle_replace( pkg_db *, pkg_handle *, install_state * );
static int handle_symlink_replace( pkg_db *, pkg_descr_entry * );
static int install_pkg( pkg_db *, pkg_handle * );
static int rollback_dir_set( rbtree ** );
static int rollback_file_set( rbtree ** );
static int rollback_install_descr( pkg_handle *, install_state * );
static int rollback_preinst_dirs( pkg_handle *, install_state * );
static int rollback_preinst_files( pkg_handle *, install_state * );
static int rollback_preinst_symlinks( pkg_handle *, install_state * );
static int rollback_symlink_set( rbtree ** );

static int adjust_dir_mtimes( pkg_db *db, pkg_handle *p, install_state *is ) {
  int status, result;
  rbtree_node *n;
  char *path, *full_path;
  dir_descr *descr;
  void *descr_v;
  struct utimbuf tb;

  status = INSTALL_SUCCESS;
  if ( db && p && is ) {
    if ( is->pass_nine_dirs_to_process ) {
      n = NULL;
      do {
	descr = NULL;
	path = rbtree_enum( is->pass_nine_dirs_to_process, n, &descr_v, &n );
	if ( path ) {
	  if ( descr_v ) {
	    descr = (dir_descr *)descr_v;
	    full_path = concatenate_paths( get_root(), path );
	    if ( full_path ) {
	      tb.actime = descr->mtime;
	      tb.modtime = descr->mtime;
	      result = utime( full_path, &tb );
	      if ( result != 0 ) {
		fprintf( stderr, "Warning: couldn't utime %s: %s\n",
			 full_path, strerror( errno ) );
	      }
	      free( full_path );
	    }
	    else {
	      fprintf( stderr,
		       "Warning: adjust_dir_mtimes() couldn't alloc for %s\n",
		       path );
	    }
	  }
	  else {
	    fprintf( stderr,
		     "adjust_dir_mtimes() saw path %s but NULL descr\n",
		     path );
	  }
	}
	/* else nothing else to process */
      } while ( n );

      /* Free the rbtree */
      rbtree_free( is->pass_nine_dirs_to_process );
      is->pass_nine_dirs_to_process = NULL;
    }
    /* else nothing to do */
  }
  else status = INSTALL_ERROR;

  return status;
}

static install_state * alloc_install_state( void ) {
  install_state *is;

  is = malloc( sizeof( *is ) );
  if ( is ) {
    is->old_descr = NULL;
    is->pass_two_dirs = NULL;
    is->pass_three_dirs = NULL;
    is->pass_three_files = NULL;
    is->pass_four_dirs = NULL;
    is->pass_four_symlinks = NULL;
    is->pass_eight_names_installed = NULL;
    is->pass_nine_dirs_to_process = NULL;
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
      dcpy->mtime = d->mtime;
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

static void * copy_symlink_descr( void *sv ) {
  symlink_descr *s, *scpy;

  scpy = NULL;
  s = (symlink_descr *)sv;
  if ( s ) {
    scpy = malloc( sizeof( *scpy ) );
    if ( scpy ) {
      scpy->owner = s->owner;
      scpy->group = s->group;
      scpy->mtime = s->mtime;
      if ( s->temp_symlink ) {
	scpy->temp_symlink = copy_string( s->temp_symlink );
	if ( !(scpy->temp_symlink) ) {
	  /* Alloc error */
	  free( scpy );
	  scpy = NULL;
	}
      }
      else scpy->temp_symlink = NULL;
    }
  }

  return scpy;
}

/*
 * int create_dirs_as_needed( pkg_handle *pkg, const char *path,
 *                            rbtree **dirs );
 *
 * This function creates directories as needed to contain path, but
 * not path itself, under instroot, and records entries for them in
 * the rbtree of dir_descr records dirs.
 */

static int create_dirs_as_needed( pkg_handle *pkg, const char *path,
				  rbtree **dirs ) {
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
		    
		    /* Use the pkg mtime */

		    dd.mtime = pkg->descr->hdr.pkg_time;

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
	  if ( temp_name ) {
	    is->old_descr = concatenate_paths( get_pkg(), temp_name );
	    if ( is->old_descr ) {
	      free( temp_name );
	      temp_name = NULL;
	    }
	    else {
	      unlink( temp_name );
	      free( temp_name );
	      temp_name = NULL;

	      fprintf( stderr,
		       "Couldn't move an existing file %s/%s\n",
		       get_pkg(), pkg_name );
	      status = INSTALL_ERROR;
	    }
	  }
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

static int do_install_dirs( pkg_db *db, pkg_handle *p, install_state *is ) {
  int status, result;
  rbtree_node *n;
  char *path;
  dir_descr *descr;
  void *descr_v;

  status = INSTALL_SUCCESS;
  if ( db && p && is ) {
    if ( is->pass_two_dirs ) {
      n = NULL;
      do {
	descr = NULL;
	path = rbtree_enum( is->pass_two_dirs, n, &descr_v, &n );
	if ( path ) {
	  if ( descr_v ) {
	    descr = (dir_descr *)descr_v;
	    result = do_install_one_dir( db, p, is, path, descr );
	    if ( result != INSTALL_SUCCESS ) status = result;
	  }
	  else {
	    fprintf( stderr,
		     "do_install_dirs() saw path %s but NULL descr\n",
		     path );
	    status = INSTALL_ERROR;
	  }
	}
	/* else no nodes left */
      } while ( n );

      rbtree_free( is->pass_two_dirs );
      is->pass_two_dirs = NULL;
    }
    /* else nothing to do */
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_install_files( pkg_db *db, pkg_handle *p, install_state *is ) {
  int status, result;
  rbtree_node *n;
  char *path;
  file_descr *descr;
  void *descr_v;

  status = INSTALL_SUCCESS;
  if ( db && p && is ) {
    if ( is->pass_three_files ) {
      n = NULL;
      do {
	descr = NULL;
	path = rbtree_enum( is->pass_three_files, n, &descr_v, &n );
	if ( path ) {
	  if ( descr_v ) {
	    descr = (file_descr *)descr_v;
	    result = do_install_one_file( db, p, is, path, descr );
	    if ( result != INSTALL_SUCCESS ) status = result;
	  }
	  else {
	    fprintf( stderr,
		     "do_install_files() saw path %s but NULL descr\n",
		     path );
	    status = INSTALL_ERROR;
	  }
	}
	/* else no nodes left */
      } while ( n );

      rbtree_free( is->pass_three_files );
      is->pass_three_files = NULL;
    }
    /* else nothing to do */

    /*
     * We can also get rid of pass_three_dirs, since rollback is no
     * longer possible anyway at this point.
     */
    if ( is->pass_three_dirs ) {
      rbtree_free( is->pass_three_dirs );
      is->pass_three_dirs = NULL;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_install_one_dir( pkg_db *db, pkg_handle *p, install_state *is,
			       char *path, dir_descr *descr ) {
  int status, result;
  char *full_path;

  status = INSTALL_SUCCESS;
  if ( db && p && is && path && descr ) {
    full_path = concatenate_paths( get_root(), path );
    if ( full_path ) {
      /* First, we set the owner/group/mode */

      result = chown( full_path, descr->owner, descr->group );
      if ( result != 0 ) {
	fprintf( stderr, "Warning: couldn't chown directory %s: %s\n",
		 full_path, strerror( errno ) );
      }

      result = chmod( full_path, descr->mode );
      if ( result != 0 ) {
	fprintf( stderr, "Warning: couldn't chmod directory %s: %s\n",
		 full_path, strerror( errno ) );
      }

      /* Next, we queue it up to have its mtime adjusted in pass nine */

      if ( !(is->pass_nine_dirs_to_process) ) {
	is->pass_nine_dirs_to_process =
	  rbtree_alloc( rbtree_string_comparator,
			rbtree_string_copier,
			rbtree_string_free,
			copy_dir_descr,
			free_dir_descr );
      }
		
      if ( is->pass_nine_dirs_to_process ) {
	result = rbtree_insert( is->pass_nine_dirs_to_process,
				path, descr );
	if ( result != RBTREE_SUCCESS ) {
	  fprintf( stderr,
		   "Warning: couldn't queue %s for mtime adjustment.\n",
		   full_path );
	}
      }
      else {
	fprintf( stderr,
		 "Warning: couldn't allocate rbtree to queue %s for mtime adjustment\n",
		 full_path );
      }

      /* Check if we need to claim it for this package */
      if ( descr->claim ) {
	/*
	 * Check if we had an old install under this name discovered
	 * in pass one, and so need to record for finalization.
	 */

	if ( is->old_descr ) {
	  /* If we're claiming, we need to record this for pass eight */

	  if ( !(is->pass_eight_names_installed) ) {
	    is->pass_eight_names_installed =
	      rbtree_alloc( rbtree_string_comparator,
			    rbtree_string_copier,
			    rbtree_string_free,
			    NULL, NULL );
	  }
	  
	  if ( is->pass_eight_names_installed ) {
	    result = rbtree_insert( is->pass_eight_names_installed,
				    path, NULL );
	    if ( result != RBTREE_SUCCESS ) {
	      fprintf( stderr,
		       "Warning: couldn't record %s as installed for the finalization pass!\n",
		       full_path );
	    }
	  }
	  else {
	    fprintf( stderr,
		     "Warning: couldn't allocate rbtree to record %s as installed for the finalization pass!\n",
		     full_path );
	  }
	}
	/* else no need to record it */

	/* Claim it */
	
	result = insert_into_pkg_db( db, path, p->descr->hdr.pkg_name );
	if ( result == 0 ) {
	  printf( "ID %s\n", full_path );
	}
	else {
	  fprintf( stderr, "Warning: couldn't claim %s for %s\n",
		   path, p->descr->hdr.pkg_name );
	}
      }

      free( full_path );
    }
    else {
      fprintf( stderr, "Error installing directory %s: %s\n",
	       path, "failed to allocate memory" );
      status = INSTALL_ERROR;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_install_one_file( pkg_db *db, pkg_handle *p, install_state *is,
				char *path, file_descr *descr ) {
  int status, result, should_clear;
  char *full_path, *temp_path;
  struct stat st;
  struct utimbuf tb;

  status = INSTALL_SUCCESS;
  if ( db && p && is && path && descr ) {
    full_path = concatenate_paths( get_root(), path );
    if ( full_path ) {
      should_clear = 0;
      /* First, check if the path already exists */
      result = lstat( full_path, &st );
      if ( result == 0 ) {
	/* lstat() succeeded, check what's there */
	if ( S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode ) ) {
	  /* There is an existing regular file or symlink */

	  /*
	   * Try to remove it; we don't need to remove its pkgdb
	   * entry, because we will overwrite with an entry for this
	   * file if we succeed
	   */

	  result = unlink( full_path );
	  if ( result != 0 ) {
	    fprintf( stderr, "Couldn't remove existing %s at %s: %s\n",
		     ( S_ISREG( st.st_mode ) ) ? "file" : "symlink",
		     full_path, strerror( errno ) );
	    status = INSTALL_ERROR;
	  }
	  /*
	   * We removed something, so if we fail later we should clear
	   * its pkgdb entry.
	   */
	  else should_clear = 1;
	}
	else if ( S_ISDIR( st.st_mode ) ) {
	  /* There is an existing directory */

	  /* For now, this is an error */

	  fprintf( stderr,
		   "There was a directory present at %s while trying to install\n",
		   full_path );
	  status = INSTALL_ERROR;
	}
	else {
	  /* Something else */

	  /* This is an error */

	  fprintf( stderr,
		   "Some other filesystem object (st_mode = %o) was present at %s\n",
		   st.st_mode, full_path );
	  status = INSTALL_ERROR;
	}
      }
      else {
	/*
	 * lstat() failed, did it fail because the path did not exist,
	 * or some other problematic reason?
	 */

	if ( errno != ENOENT ) {
	  /* Something else, this is an error */

	  fprintf( stderr, "Couldn't lstat() %s: %s\n",
		   full_path, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
      }

      if ( status == INSTALL_SUCCESS ) {
	/* We're okay so far, and we know the target path is clear */

	temp_path = concatenate_paths( get_root(), descr->temp_file );
	if ( temp_path ) {
	  result = link_or_copy( full_path, temp_path );
	  if ( result == LINK_OR_COPY_SUCCESS ) {
	    /* Okay, we've got it in place */

	    /*
	     * If we had an old package install under this name, we
	     * need to record this for pass eight.
	     */

	    if ( is->old_descr ) {
	      if ( !(is->pass_eight_names_installed) ) {
		is->pass_eight_names_installed =
		  rbtree_alloc( rbtree_string_comparator,
				rbtree_string_copier,
				rbtree_string_free,
				NULL, NULL );
	      }
	  
	      if ( is->pass_eight_names_installed ) {
		result = rbtree_insert( is->pass_eight_names_installed,
					path, NULL );
		if ( result != RBTREE_SUCCESS ) {
		  fprintf( stderr,
			   "Warning: couldn't record %s as installed for the finalization pass!\n",
			   full_path );
		}
	      }
	      else {
		fprintf( stderr,
			 "Warning: couldn't allocate rbtree to record %s as installed for the finalization pass!\n",
			 full_path );
	      }
	    }

	    /* Adjust owner/group/mode/mtime */
	    result = chown( full_path, descr->owner, descr->group );
	    if ( result != 0 ) {
	      fprintf( stderr, "Warning: couldn't chown %s: %s\n",
		       full_path, strerror( errno ) );
	    }

	    result = chmod( full_path, descr->mode );
	    if ( result != 0 ) {
	      fprintf( stderr, "Warning: couldn't chmod %s: %s\n",
		       full_path, strerror( errno ) );
	    }

	    tb.actime = descr->mtime;
	    tb.modtime = descr->mtime;
	    result = utime( full_path, &tb );
	    if ( result != 0 ) {
	      fprintf( stderr, "Warning: couldn't utime %s: %s\n",
		       full_path, strerror( errno ) );
	    }

	    /* Claim it */
	    result = insert_into_pkg_db( db, path, p->descr->hdr.pkg_name );
	    if ( result == 0 ) {
	      printf( "IF %s\n", full_path );
	    }
	    else {
	      fprintf( stderr, "Warning: couldn't claim %s for %s\n",
		       path, p->descr->hdr.pkg_name );
	    }
	  }
	  else if ( result == LINK_OR_COPY_OUT_OF_DISK ) {
	    fprintf( stderr,
		     "Out of disk space while installing %s\n",
		     full_path );
	    status = INSTALL_OUT_OF_DISK;
	    /* Unlink any partial copy */
	    unlink( full_path );
	    should_clear = 1;
	  }
	  else {
	    fprintf( stderr,
		     "Error while installing %s\n" );
	    status = INSTALL_ERROR;
	  }

	  /* Get rid of the temp */
	  unlink( temp_path );

	  free( temp_path );
	}
	else {
	  fprintf( stderr, "Error installing file %s: %s\n",
		   path, "failed to allocate memory" );
	  status = INSTALL_ERROR;
	}
      }

      if ( status != INSTALL_SUCCESS && should_clear ) {
	/* Clear out the existing pkgdb entry */

	delete_from_pkg_db( db, path );
      }

      free( full_path );
    }
    else {
      fprintf( stderr, "Error installing file %s: %s\n",
	       path, "failed to allocate memory" );
      status = INSTALL_ERROR;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_install_one_symlink( pkg_db *db, pkg_handle *p,
				   install_state *is,
				   char *path, symlink_descr *descr ) {
  int status, result, should_clear;
  char *full_path, *temp_path, *target;
  struct stat st;
  struct utimbuf tb;

  status = INSTALL_SUCCESS;
  if ( db && p && is && path && descr ) {
    full_path = concatenate_paths( get_root(), path );
    if ( full_path ) {
      should_clear = 0;
      /* First, check if the path already exists */
      result = lstat( full_path, &st );
      if ( result == 0 ) {
	/* lstat() succeeded, check what's there */
	if ( S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode ) ) {
	  /* There is an existing regular file or symlink */
	  
	  /*
	   * Try to remove it; we don't need to remove its pkgdb
	   * entry, because we will overwrite with an entry for this
	   * file if we succeed
	   */

	  result = unlink( full_path );
	  if ( result != 0 ) {
	    fprintf( stderr, "Couldn't remove existing %s at %s: %s\n",
		     ( S_ISREG( st.st_mode ) ) ? "file" : "symlink",
		     full_path, strerror( errno ) );
	    status = INSTALL_ERROR;
	  }
	  /*
	   * We removed something, so if we fail later we should clear
	   * its pkgdb entry.
	   */
	  else should_clear = 1;
	}
	else if ( S_ISDIR( st.st_mode ) ) {
	  /* There is an existing directory */

	  /* For now, this is an error */

	  fprintf( stderr,
		   "There was a directory present at %s while trying to install\n",
		   full_path );
	  status = INSTALL_ERROR;
	}
	else {
	  /* Something else */

	  /* This is an error */

	  fprintf( stderr,
		   "Some other filesystem object (st_mode = %o) was present at %s\n",
		   st.st_mode, full_path );
	  status = INSTALL_ERROR;
	}
      }
      else {
	/*
	 * lstat() failed, did it fail because the path did not exist,
	 * or some other problematic reason?
	 */

	if ( errno != ENOENT ) {
	  /* Something else, this is an error */

	  fprintf( stderr, "Couldn't lstat() %s: %s\n",
		   full_path, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
      }

      if ( status == INSTALL_SUCCESS ) {
	/* We're okay so far, and we know the target path is clear */

	temp_path = concatenate_paths( get_root(), descr->temp_symlink );
	if ( temp_path ) {
	  /*
	   * We need to read the content of the symlink at temp_path,
	   * unlink it, and create a new symlink at full_path, and
	   * adjust its owner/group/mtime
	   */

	  target = NULL;
	  result = read_symlink_target( temp_path, &target );
	  if ( result == READ_SYMLINK_SUCCESS ) {
	    unlink( temp_path );

	    result = symlink( target, full_path );
	    if ( result == 0 ) {
	      /*
	       * Okay, the link is created.  Now we just need to
	       * record for pass eight, adjust its owner/group/mtime
	       * and claim it,
	       */
	      
	      /*
	       * If we had an old package install under this name, we
	       * need to record this for pass eight.
	       */

	      if ( is->old_descr ) {
		if ( !(is->pass_eight_names_installed) ) {
		  is->pass_eight_names_installed =
		    rbtree_alloc( rbtree_string_comparator,
				  rbtree_string_copier,
				  rbtree_string_free,
				  NULL, NULL );
		}
	  
		if ( is->pass_eight_names_installed ) {
		  result = rbtree_insert( is->pass_eight_names_installed,
					  path, NULL );
		  if ( result != RBTREE_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: couldn't record %s as installed for the finalization pass!\n",
			     full_path );
		  }
		}
		else {
		  fprintf( stderr,
			   "Warning: couldn't allocate rbtree to record %s as installed for the finalization pass!\n",
			   full_path );
		}
	      }

	      /* Adjust owner/group/mtime */
	      result = chown( full_path, descr->owner, descr->group );
	      if ( result != 0 ) {
		fprintf( stderr, "Warning: couldn't chown %s: %s\n",
			 full_path, strerror( errno ) );
	      }

	      tb.actime = descr->mtime;
	      tb.modtime = descr->mtime;
	      result = utime( full_path, &tb );
	      if ( result != 0 ) {
		fprintf( stderr, "Warning: couldn't utime %s: %s\n",
			 full_path, strerror( errno ) );
	      }

	      /* Claim it */
	      result = insert_into_pkg_db( db, path, p->descr->hdr.pkg_name );
	      if ( result == 0 ) {
		printf( "IS %s\n", full_path );
	      }
	      else {
		fprintf( stderr, "Warning: couldn't claim %s for %s\n",
			 path, p->descr->hdr.pkg_name );
	      }
	    }
	    else {
	      fprintf( stderr,
		       "Failed to symlink() while installing symlink %s: %s\n",
		       full_path, strerror( errno ) );
	      status = INSTALL_ERROR;
	    }

	    free( target );
	  }
	  else if ( result == READ_SYMLINK_LSTAT_ERROR ) {
	    fprintf( stderr,
		     "Failed to lstat() while installing symlink %s: %s\n",
		     full_path, strerror( errno ) );
	    status = INSTALL_ERROR;
	  }
	  else if ( result == READ_SYMLINK_READLINK_ERROR ) {
	    fprintf( stderr,
		     "Failed to readlink() while installing symlink %s: %s\n",
		     full_path, strerror( errno ) );
	    status = INSTALL_ERROR;
	  }
	  else if ( result == READ_SYMLINK_MALLOC_ERROR ) {
	    fprintf( stderr, "Error installing symlink %s: %s\n",
		     full_path, "failed to allocate memory" );
	    status = INSTALL_ERROR;
	  }
	  else {
	    fprintf( stderr,
		     "Error in read_symlink_target() while installing symlink %s\n" );
	    status = INSTALL_ERROR;
	  }

	  /* Otherwise, it was unlinked above */
	  if ( status != INSTALL_SUCCESS ) unlink( temp_path );

	  free( temp_path );
	}
	else {
	  fprintf( stderr, "Error installing symlink %s: %s\n",
		   path, "failed to allocate memory" );
	  status = INSTALL_ERROR;
	}
      }

      if ( status != INSTALL_SUCCESS && should_clear ) {
	/* Clear out the existing pkgdb entry */

	delete_from_pkg_db( db, path );
      }

      free( full_path );
    }
    else {
      fprintf( stderr, "Error installing symlink %s: %s\n",
	       path, "failed to allocate memory" );
      status = INSTALL_ERROR;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_install_symlinks( pkg_db *db, pkg_handle *p,
				install_state *is ) {
  int status, result;
  rbtree_node *n;
  char *path;
  symlink_descr *descr;
  void *descr_v;

  status = INSTALL_SUCCESS;
  if ( db && p && is ) {
    if ( is->pass_four_symlinks ) {
      n = NULL;
      do {
	descr = NULL;
	path = rbtree_enum( is->pass_four_symlinks, n, &descr_v, &n );
	if ( path ) {
	  if ( descr_v ) {
	    descr = (symlink_descr *)descr_v;
	    result = do_install_one_symlink( db, p, is, path, descr );
	    if ( result != INSTALL_SUCCESS ) status = result;
	  }
	  else {
	    fprintf( stderr,
		     "do_install_symlinks() saw path %s but NULL descr\n",
		     path );
	    status = INSTALL_ERROR;
	  }
	}
	/* else no nodes left */
      } while ( n );

      rbtree_free( is->pass_four_symlinks );
      is->pass_four_symlinks = NULL;
    }
    /* else nothing to do */

    /*
     * We can also get rid of pass_four_dirs, since rollback is no
     * longer possible anyway at this point.
     */
    if ( is->pass_four_dirs ) {
      rbtree_free( is->pass_four_dirs );
      is->pass_four_dirs = NULL;
    }
  }
  else status = INSTALL_ERROR;

  return status;
}

static int do_preinst_dirs( pkg_handle *p, install_state *is ) {
  int status, result, i;
  pkg_descr *desc;
  pkg_descr_entry *e;
  char *temp;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    desc = p->descr;
    for ( i = 0; i < desc->num_entries; ++i ) {
      e = desc->entries + i;
      if ( e->type == ENTRY_DIRECTORY ) {
	result = do_preinst_one_dir( is, p, e );
	if ( result != INSTALL_SUCCESS ) {
	  temp = concatenate_paths( get_root(), e->filename );
	  if ( temp ) {
	    fprintf( stderr, "Couldn't preinstall directory %s\n", temp );
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
	    fprintf( stderr, "Couldn't preinstall file %s\n", temp );
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
			       pkg_handle *pkg,
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
      result = create_dirs_as_needed( pkg, e->filename, &(is->pass_two_dirs) );

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

	    /* If p isn't '/' */
	    if ( strlen( lastcomp ) > 0 ) {
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
		  dd.mtime = pkg->descr->hdr.pkg_time;
		  dd.unroll = 0;
		  dd.claim = 1;
		}
		else {
		  /* It's not a directory.  This is an error. */
		  
		  fprintf( stderr,
			   "%s%s: already exists but not a directory\n",
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
	    }
	    else {
	      /*
	       * p was /, we record and claim it, but we don't create
	       * or unroll it.
	       */

	      record_dir = 1;
	      dd.owner = owner;
	      dd.group = group;
	      dd.mode = e->u.d.mode;
	      dd.unroll = 0;
	      dd.claim = 1;
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
	result = create_dirs_as_needed( pkg, e->filename,
					&(is->pass_three_dirs) );

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
	      status = INSTALL_ERROR;
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

static int do_preinst_one_symlink( install_state *is,
				   pkg_handle *pkg,
				   pkg_descr_entry *e ) {
  int status, result;
  uid_t owner;
  gid_t group;
  struct passwd *pwd;
  struct group *grp;
  char *p, *base, *lastcomp, *format;
  int format_len, tmpfd;
  symlink_descr sd;

  status = INSTALL_SUCCESS;
  if ( is && pkg && e ) {
    if ( e->type == ENTRY_SYMLINK ) {
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
	 * instroot.  It will be the key for the is->pass_four_symlinks
	 * entry.
	 */

	/*
	 * Create all needed dirs enclosing this path and record for
	 * possible later unroll
	 */
	result = create_dirs_as_needed( pkg, e->filename,
					&(is->pass_four_dirs) );
	if ( result == INSTALL_SUCCESS ) {
	  /*
	   * We should be chdir()ed to the directory enclosing this
	   * path, so we just need to create a temporary and record.
	   */

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
		 * format.  Clear out the temp, and try to create out
		 * placeholder symlink.
		 */
		  close( tmpfd );
		  unlink( format );

		  result = symlink( e->u.s.target, format );
		  if ( result == 0 ) {
		    sd.owner = owner;
		    sd.group = group;
		    sd.mtime = pkg->descr->hdr.pkg_time;
		    sd.temp_symlink = concatenate_paths( base, format );
		    if ( sd.temp_symlink ) {
		      if ( !(is->pass_four_symlinks) ) {
			is->pass_four_symlinks =
			  rbtree_alloc( rbtree_string_comparator,
					rbtree_string_copier,
					rbtree_string_free,
					copy_symlink_descr,
					free_symlink_descr );
		      }
		
		      if ( is->pass_four_symlinks ) {
			result =
			  rbtree_insert( is->pass_four_symlinks, p, &sd );
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

		      free( sd.temp_symlink );
		    }
		    else status = INSTALL_ERROR;

		    /*
		     * If we failed somewhere, be sure to delete the
		     * placeholder symlink.
		     */

		    if ( status != INSTALL_SUCCESS ) unlink( format );
		  }
		  else {
		    if ( errno == ENOSPC )
		      status = INSTALL_OUT_OF_DISK;
		    else status = INSTALL_ERROR;
		  }
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
	    status = INSTALL_ERROR;
	  }
	}
	else {
	  fprintf( stderr,
		   "do_preinst_one_symlink(): couldn't create enclosing directories for install target %s\n",
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


static int do_preinst_symlinks( pkg_handle *p, install_state *is ) {
  int status, result, i;
  pkg_descr *desc;
  pkg_descr_entry *e;
  char *temp;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    desc = p->descr;
    for ( i = 0; i < desc->num_entries; ++i ) {
      e = desc->entries + i;
      if ( e->type == ENTRY_SYMLINK ) {
	result = do_preinst_one_symlink( is, p, e );
	if ( result != INSTALL_SUCCESS ) {
	  temp = concatenate_paths( get_root(), e->filename );
	  if ( temp ) {
	    fprintf( stderr, "Couldn't preinstall symlink %s\n", temp );
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
  char *temp;
  if ( is ) {
    if ( is->old_descr ) {
      /* unlink it if it still exists */
      unlink( is->old_descr );
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
    if ( is->pass_four_dirs ) {
      rbtree_free( is->pass_four_dirs );
      is->pass_four_dirs = NULL;
    }
    if ( is->pass_four_symlinks ) {
      rbtree_free( is->pass_four_symlinks );
      is->pass_four_symlinks = NULL;
    }
    if ( is->pass_eight_names_installed ) {
      rbtree_free( is->pass_eight_names_installed );
      is->pass_eight_names_installed;
    }
    if ( is->pass_nine_dirs_to_process ) {
      rbtree_free( is->pass_nine_dirs_to_process );
      is->pass_nine_dirs_to_process;
    }
    free( is );
  }
}

static void free_symlink_descr( void *sv ) {
  symlink_descr *s;

  if ( sv ) {
    s = (symlink_descr *)sv;
    if ( s->temp_symlink ) free( s->temp_symlink );
    free( s );
  }
}

static int handle_dir_replace( pkg_db *db, pkg_descr_entry *e ) {
  char *full_path, *canonical_path;
  int status, result;
  struct stat buf;

  status = INSTALL_SUCCESS;
  if ( db && e && e->type == ENTRY_DIRECTORY ) {
    full_path = concatenate_paths( get_root(), e->filename );
    if ( full_path ) {
      result = lstat( full_path, &buf );
      if ( result == 0 ) {
	/* Successful lstat(), check type */
	if ( S_ISDIR( buf.st_mode ) ) {
	  /* try to rmdir() it, and check for ENOTEMPTY */
	  result = rmdir( full_path );
	  if ( result == 0 ) {
	    /* We got it */
	    printf( "RD %s\n", full_path );
	  }
	  else {
	    /* rmdir() failed */
	    /* POSIX allows ENOTEMPTY or EEXIST */
	    if ( errno != ENOTEMPTY && errno != EEXIST ) {
	      fprintf( stderr,
		       "Warning: couldn't rmdir() %s: %s\n",
		       full_path, strerror( errno ) );
	      status = INSTALL_ERROR;
	    }
	    /*
	     * else it wasn't empty, and we didn't really want to
	     * remove it anyway
	     */
	  }
	}
	/* else the thing at that path is not a directory, so nothing to do */
      }
      else {
	/* Failed lstat().  Was is because it was absent? */
	if ( errno != ENOENT ) {
	  /* Failed lstat(), throw a warning */
	  fprintf( stderr,
		   "Warning: couldn't lstat() old directory %s: %s\n",
		   e->filename, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
	/* else nothing to do; it didn't exist */
      }
      free( full_path );
    }
    else {
      fprintf( stderr,
	       "Warning: out of memory testing old directory %s for removal.\n",
	       e->filename );
      status = INSTALL_ERROR;
    }

    /* In any case, we can remove the pkgdb entry */
    canonical_path = canonicalize_and_copy( e->filename );
    if ( canonical_path ) {
      result = delete_from_pkg_db( db, canonical_path );
      if ( result != 0 ) {
	fprintf( stderr,
		 "Warning: failed to remove pkgdb entry for old directory %s\n",
		 canonical_path );
	status = INSTALL_ERROR;
      }
      
      free( canonical_path );
    }
    else {
      fprintf( stderr,
	       "Warning: error allocating memory while trying to remove pkgdb entry for old directory %s\n",
	       e->filename );
    }
  }
  else {
    fprintf( stderr,
	     "Warning: internal error in handle_dir_replace()\n" );
    status = INSTALL_ERROR;
  }

  return status;
}

static int handle_file_replace( pkg_db *db, pkg_descr *old_p,
				pkg_descr_entry *e ) {
  char *full_path, *canonical_path;
  int status, result;
  struct stat buf;

  status = INSTALL_SUCCESS;
  if ( db && old_p && e && e->type == ENTRY_FILE ) {
    full_path = concatenate_paths( get_root(), e->filename );
    if ( full_path ) {
      result = lstat( full_path, &buf );
      if ( result == 0 ) {
	/* Successful lstat(), check type */
	if ( S_ISREG( buf.st_mode ) ) {
	  /* Check if the file has been modified */
	  if ( buf.st_mtime == old_p->hdr.pkg_time ) {
	    if ( get_check_md5() ) {
	      result = file_hash_matches( full_path, e->u.f.hash );
	      if ( result == 1 ) {
		/* Hash match; remove it */
		printf( "RF %s\n", full_path );
		unlink( full_path );
	      }
	      /* if result == 0, no match, so leave it */
	      else if ( result != 0 ) {
		/* Error checking */
		fprintf( stderr,
			 "Warning: couldn't check MD5 of old file %s\n",
			 full_path );
	      }
	    }
	    else {
	      /* No MD5 check; go ahead and unlink it */
	      printf( "RF %s\n", full_path );
	      unlink( full_path );
	    }
	  }
	  /* else mtimes don't match; leave it */
	}
	/* else the thing at that path was not a file, so nothing to do */
      }
      else {
	/* Failed lstat().  Was is because it was absent? */
	if ( errno != ENOENT ) {
	  /* Failed lstat(), throw a warning */
	  fprintf( stderr,
		   "Warning: couldn't lstat() old file %s: %s\n",
		   e->filename, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
	/* else nothing to do; it didn't exist */
      }
      free( full_path );
    }
    else {
      fprintf( stderr,
	       "Warning: out of memory testing old file %s for removal.\n",
	       e->filename );
      status = INSTALL_ERROR;
    }

    /* In any case, we can remove the pkgdb entry */
    canonical_path = canonicalize_and_copy( e->filename );
    if ( canonical_path ) {
      result = delete_from_pkg_db( db, canonical_path );
      if ( result != 0 ) {
	fprintf( stderr,
		 "Warning: failed to remove pkgdb entry for old file %s\n",
		 canonical_path );
	status = INSTALL_ERROR;
      }

      free( canonical_path );
    }
    else {
      fprintf( stderr,
	       "Warning: error allocating memory while trying to remove pkgdb entry for old file %s\n",
	       e->filename );
    }
  }
  else {
    fprintf( stderr,
	     "Warning: internal error in handle_file_replace()\n" );
    status = INSTALL_ERROR;
  }

  return status;
}

static int handle_replace( pkg_db *db, pkg_handle *p, install_state *is ) {
  int status, result, i, need_to_remove, has_pkg_db;
  pkg_descr *old;
  pkg_descr_entry *e;
  rbtree *dirs_to_handle, *others_to_handle;
  rbtree_node *n;
  char *temp, *path, *canonical_path;
  void *e_v;

  status = INSTALL_SUCCESS;
  if ( db && p && is ) {
    /* Check if we have an old install under the same name to handle */
    if ( is->old_descr ) {
      /* Load it */
      old = read_pkg_descr_from_file( is->old_descr );
      if ( old ) {
	others_to_handle = rbtree_alloc( rbtree_string_comparator,
					 NULL, NULL, NULL, NULL );
	/*
	 * Use post_path_comparator for directories, so subdirectories
	 * will be processed ahead of their parents
	 */
	dirs_to_handle = rbtree_alloc( post_path_comparator,
				       NULL, NULL, NULL, NULL );

	if ( dirs_to_handle && others_to_handle ) {
	  for ( i = 0; i < old->num_entries; ++i ) {
	    e = &(old->entries[i]);
	    canonical_path = canonicalize_and_copy( e->filename );
	    if ( canonical_path ) {
	      /*
	       * Check if it's in the list of things we installed
	       * earlier, and if it still has a pkgdb entry owned by the
	       * old install.
	       */
	      temp = query_pkg_db( db, canonical_path );
	      if ( temp ) {
		if ( strcmp( temp, old->hdr.pkg_name ) == 0 ) has_pkg_db = 1;
		else has_pkg_db = 0;
		free( temp );
	      }
	      else has_pkg_db = 0;

	      if ( has_pkg_db ) {
		if ( is->pass_eight_names_installed ) {
		  result = rbtree_query( is->pass_eight_names_installed,
					 canonical_path, NULL );
		  /* We remove it if it was not installed in the new package */
		  if ( result == RBTREE_NOT_FOUND ) need_to_remove = 1;
		  else need_to_remove = 0;
		}
		/*
		 * else we installed an empty package, so everything is
		 * need-to-remove.
		 */
		else need_to_remove = 1;
	      }
	      /* else the old package no longer claimed it */
	      else need_to_remove = 0;

	      if ( need_to_remove ) {
		/* Queue for removal in the appropriate rbtree */
		switch ( e->type ) {
		case ENTRY_DIRECTORY:
		  result = rbtree_insert( dirs_to_handle, e->filename, e );
		  if ( result != RBTREE_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: error while trying to queue old directory %s from %s.\n",
			     e->filename, old->hdr.pkg_name );
		  }
		  break;
		case ENTRY_FILE:
		  result = rbtree_insert( others_to_handle, e->filename, e );
		  if ( result != RBTREE_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: error while trying to queue old file %s from %s.\n",
			     e->filename, old->hdr.pkg_name );
		  }
		  break;
		case ENTRY_SYMLINK:
		  result = rbtree_insert( others_to_handle, e->filename, e );
		  if ( result != RBTREE_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: error while trying to queue old symlink %s from %s.\n",
			     e->filename, old->hdr.pkg_name );
		  }
		  break;
		case ENTRY_LAST:
		  /* Ignore */
		  break;
		default:
		  /* Warn and ignore */
		  fprintf( stderr,
			   "Warning: unknown entry type %d for %s in old package description for %s.\n",
			   e->type, e->filename, p->descr->hdr.pkg_name );
		}
	      }

	      free( canonical_path );
	    }
	    else {
	      fprintf( stderr,
		       "Warning: error allocating memory for %s in old package description for %s\n",
		       e->filename, old->hdr.pkg_name );
	    }
	  }

	  /*
	   * Now handle the removals by enumerating the trees; first
	   * files and symlinks, then directories.
	   */
	  n = NULL;
	  do {
	    e = NULL;
	    path = rbtree_enum( others_to_handle, n, &e_v, &n );
	    if ( path ) {
	      if ( e_v ) {
		e = (pkg_descr_entry *)e_v;
		if ( e->type == ENTRY_FILE ) {
		  result = handle_file_replace( db, old, e );
		  if ( result != INSTALL_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: error removing file %s for %s\n",
			     path, p->descr->hdr.pkg_name );
		  }
		}
		else if ( e->type == ENTRY_SYMLINK ) {
		  result = handle_symlink_replace( db, e );
		  if ( result != INSTALL_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: error removing symlink %s for %s\n",
			     path, p->descr->hdr.pkg_name );
		  }
		}
		else {
		  fprintf( stderr,
			   "Warning: while removing files and symlinks, saw other entry type for %s (in %s)\n",
			   path, p->descr->hdr.pkg_name );
		  /* Skip it */
		}
	      }
	      else {
		fprintf( stderr,
			 "Warning: NULL entry at filename %s while enumerating files and symlinks to remove (in %s)\n",
			 path, p->descr->hdr.pkg_name );
		/* Skip it */
	      }
	    }
	    /* else we're done */
	  } while ( n != NULL );

	  /* Now the directories */
	  n = NULL;
	  do {
	    e = NULL;
	    path = rbtree_enum( dirs_to_handle, n, &e_v, &n );
	    if ( path ) {
	      if ( e_v ) {
		e = (pkg_descr_entry *)e_v;
		if ( e->type == ENTRY_DIRECTORY ) {
		  result = handle_dir_replace( db, e );
		  if ( result != INSTALL_SUCCESS ) {
		    fprintf( stderr,
			     "Warning: error removing directory %s for %s\n",
			     path, p->descr->hdr.pkg_name );
		  }
		}
		else {
		  fprintf( stderr,
			   "Warning: while removing directories, saw non-directory entry type for %s (in %s)\n",
			   path, p->descr->hdr.pkg_name );
		  /* Skip it */
		}
	      }
	      else {
		fprintf( stderr,
			 "Warning: NULL entry at filename %s while enumerating directories to remove (in %s)\n",
			 path, p->descr->hdr.pkg_name );
		/* Skip it */
	      }
	    }
	    /* else we're done */
	  } while ( n != NULL );

	  /* Free the rbtrees */
	  rbtree_free( dirs_to_handle );
	  rbtree_free( others_to_handle );
	}
	else {
	  fprintf( stderr,
		   "Warning: couldn't allocate rbtrees trying to remove old package %s\n",
		   p->descr->hdr.pkg_name );
	  if ( dirs_to_handle ) rbtree_free( dirs_to_handle );
	  if ( others_to_handle ) rbtree_free( others_to_handle );
	  /* Skip it */
	}

	/* Free the in-memory package description data */
	free_pkg_descr( old );
      }
      else {
	fprintf( stderr,
		 "Warning: couldn't read old package description for %s from %s\n",
		 p->descr->hdr.pkg_name, is->old_descr );
	/* Skip it, leaving old files still installed, if this happens */
      }

      /* Delete & free the old description */
      unlink( is->old_descr );
      free( is->old_descr );
      is->old_descr = NULL;

      /* Free the rbtree */
      if ( is->pass_eight_names_installed ) {
	rbtree_free( is->pass_eight_names_installed );
	is->pass_eight_names_installed = NULL;
      }
    }
    /* else nothing to do */    
  }
  else status = INSTALL_ERROR;

  return status;
}

static int handle_symlink_replace( pkg_db *db, pkg_descr_entry *e ) {
  char *full_path, *target, *canonical_path;
  int status, result;
  struct stat buf;

  status = INSTALL_SUCCESS;
  if ( db && e && e->type == ENTRY_SYMLINK ) {
    full_path = concatenate_paths( get_root(), e->filename );
    if ( full_path ) {
      result = lstat( full_path, &buf );
      if ( result == 0 ) {
	/* Successful lstat(), check type and link target */
	if ( S_ISLNK( buf.st_mode ) ) {
	  /*
	   * If it is a symlink, we delete it if it is unmodified.  We
	   * need to compare the link targets.
	   */
	  target = NULL;
	  result = read_symlink_target( full_path, &target );
	  if ( result == READ_SYMLINK_SUCCESS ) {
	    if ( strcmp( target, e->u.s.target ) == 0 ) {
	      /* They match, delete the existing symlink */
	      printf( "RS %s\n", full_path );
	      unlink( full_path );
	    }
	    free( target );
	  }
	  else {
	    fprintf( stderr,
		     "Warning: unable to read target of existing symlink %s\n",
		     full_path );
	    status = INSTALL_ERROR;
	  }
	}
	/* else the thing at that path was not a symlink, so nothing to do */
      }
      else {
	/* Failed lstat().  Was is because it was absent? */
	if ( errno != ENOENT ) {
	  /* Failed lstat(), throw a warning */
	  fprintf( stderr,
		   "Warning: couldn't lstat() old symlink %s: %s\n",
		   e->filename, strerror( errno ) );
	  status = INSTALL_ERROR;
	}
	/* else nothing to do; it didn't exist */
      }
      free( full_path );
    }
    else {
      fprintf( stderr,
	       "Warning: out of memory testing old symlink %s for removal.\n",
	       e->filename );
      status = INSTALL_ERROR;
    }

    /* In any case, we can remove the pkgdb entry */
    canonical_path = canonicalize_and_copy( e->filename );
    if ( canonical_path ) {
      result = delete_from_pkg_db( db, canonical_path );
      if ( result != 0 ) {
	fprintf( stderr,
		 "Warning: failed to remove pkgdb entry for old symlink %s\n",
		 canonical_path );
	status = INSTALL_ERROR;
      }

      free( canonical_path );
    }
    else {
      fprintf( stderr,
	       "Warning: error allocating memory while trying to remove pkgdb entry for old symlink %s\n",
	       e->filename );
    }
  }
  else {
    fprintf( stderr,
	     "Warning: internal error in handle_symlink_replace()\n" );
    status = INSTALL_ERROR;
  }

  return status;
}

void install_help( void ) {
  printf( "Install packages.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] install <package 1> <package 2> ...\n" );
  printf( "\n" );
  printf( "<package 1>, etc., are filenames of packages to install.\n" );
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
   * something exists, create the same symlink under a temporary name
   * so we take up the disk space during pre-inst.  Record the list of
   * symlinks created and temporary names used so they can be adjusted
   * in pass seven.
   *
   * At this point, we have created everything we need to in the
   * location it needs to be installed in, so we are at a maximum of
   * disk space consumption.  If we get here, we can now begin making
   * the installation permanent in the next phases.
   *
   * 5.) Iterate over the directory list in the package installer
   * again; every directory in it already exists due to pass two.
   * Adjust the mode, owner and group of these directories to match,
   * and assert ownership of them in the package db.  Create a list of
   * directories claimed for use in pass nine.  Create and/or update a
   * list of pathnames installed for use in pass eight.
   *
   * 6.) Iterate through the list of temporaries we create in pass
   * three.  For each one, test if something already exists in with
   * the name we are installing to.  If something does exist, check if
   * it is a directory.  If not, unlink it.  If so, declare an
   * error. Now that the installation path is clear, link the
   * temporary to the new name, and then unlink the temporary.  Assert
   * ownership of this path in the package db.  Create and/or update a
   * list of pathnames installed for use in pass eight.
   *
   * 7.) Iterate through the list of renames from pass 4, removing
   * them and their package db entries as needed.  Iterate through the
   * list of links created, creating package db entries. Create and/or
   * update a list of pathnames installed for use in pass eight.
   *
   * 8.) If we renamed an existing package-description for this
   * package in phase one, load it, and iterate over its contents.
   * For each item, check if ownership was asserted in the package db,
   * and if we installed at some point.  If it was owned and we did
   * not install it, remove it.  Delete the old package-description.
   *
   * 9.) Process the list of directories from pass five and adjust
   * the mtimes.
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

      /* Pass four */
      status = do_preinst_symlinks( p, is );
      if ( status != INSTALL_SUCCESS ) goto err_preinst_symlinks;
      
      /* Pass five */
      status = do_install_dirs( db, p, is );
      if ( status != INSTALL_SUCCESS ) goto install_done;

      /* Pass six */
      status = do_install_files( db, p, is );
      if ( status != INSTALL_SUCCESS ) goto install_done;

      /* Pass seven */
      status = do_install_symlinks( db, p, is );
      if ( status != INSTALL_SUCCESS ) goto install_done;

      /* Pass eight */
      status = handle_replace( db, p, is );
      if ( status != INSTALL_SUCCESS ) goto install_done;

      /* Pass nine */
      status = adjust_dir_mtimes( db, p, is );

      goto install_done;

    err_preinst_symlinks:
      rollback_preinst_symlinks( p, is );

    err_preinst_files:
      rollback_preinst_files( p, is );

    err_preinst_dirs:
      rollback_preinst_dirs( p, is );

    err_install_descr:
      rollback_install_descr( p, is );

    install_done:
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
	/* else no nodes left */
      } while ( n );

      rbtree_free( *files );
      *files = NULL;
    }
    /* else nothing to do */
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

  return status;
}

static int rollback_preinst_symlinks( pkg_handle *p, install_state *is ) {
  int status, result;

  status = INSTALL_SUCCESS;
  if ( p && is ) {
    result = rollback_symlink_set( &(is->pass_four_symlinks) );
    if ( status == INSTALL_SUCCESS && result != INSTALL_SUCCESS )
      status = result;

    result = rollback_dir_set( &(is->pass_four_dirs) );
    if ( status == INSTALL_SUCCESS && result != INSTALL_SUCCESS )
      status = result;
  }

  return status;
}

static int rollback_symlink_set( rbtree **symlinks ) {
  int status;
  rbtree_node *n;
  symlink_descr *descr;
  void *descr_v;
  char *install_target, *full_path;

  status = INSTALL_SUCCESS;
  if ( symlinks ) {
    if ( *symlinks ) {
      n = NULL;
      do {
	descr = NULL;
	install_target = rbtree_enum( *symlinks, n, &descr_v, &n );
	if ( install_target ) {
	  if ( descr_v ) {
	    descr = (symlink_descr *)descr_v;
	    if ( descr->temp_symlink ) {
	      full_path = concatenate_paths( get_root(), descr->temp_symlink );
	      if ( full_path ) {
		unlink( full_path );
		free( full_path );
	      }
	      else {
		fprintf( stderr,
			 "rollback_symlink_set() couldn't construct full path for %s\n",
			 descr->temp_symlink );
		status = INSTALL_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr,
		       "rollback_symlink_set() saw target %s with NULL temp_symlink\n",
		       install_target );
	      status = INSTALL_ERROR;
	    }
	  }
     	  else {
	    fprintf( stderr,
		     "rollback_symlink_set() saw target %s but NULL descr\n",
		     install_target );
	    status = INSTALL_ERROR;
	  }
	}
	/* else no nodes left */
      } while ( n );

      rbtree_free( *symlinks );
      *symlinks = NULL;
    }
    /* else nothing to do */
  }  
  else status = INSTALL_ERROR;

  return status;  
}
