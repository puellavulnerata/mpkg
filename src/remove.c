#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include <pkg.h>

#define REMOVE_SUCCESS 0
#define REMOVE_ERROR -1

static int remove_directory( pkg_db *, pkg_descr *, pkg_descr_entry * );
static int remove_file( pkg_db *, pkg_descr *, pkg_descr_entry * );
static int remove_pkg_by_descr( pkg_db *, pkg_descr * );
static int remove_pkg( pkg_db *, const char * );
static int remove_symlink( pkg_db *, pkg_descr *, pkg_descr_entry * );

static int remove_directory( pkg_db *db, pkg_descr *descr,
			     pkg_descr_entry *e ) {
  int status, result;
  char *full_path, *owner, *canonical_path;
  struct stat buf;

  status = REMOVE_SUCCESS;
  if ( db && descr && e && e->type == ENTRY_DIRECTORY ) {
    canonical_path = canonicalize_and_copy( e->filename );
    if ( canonical_path ) {
      owner = query_pkg_db( db, canonical_path );
      if ( owner ) {
	if ( strcmp( owner, descr->hdr.pkg_name ) == 0 ) {
	  full_path = concatenate_paths( get_root(), e->filename );
	  if ( full_path ) {
	    result = lstat( full_path, &buf );
	    if ( result == 0 ) {
	      if ( S_ISDIR( buf.st_mode ) ) {
		/* Try to rmdir() it, and check for ENOTEMPTY */
		result = rmdir( full_path );
		if ( result == 0 ) {
		  /* It's gone */
		  printf( "RD %s\n", full_path );
		}
		else {
		  /* rmdir failed(), check why */
		  /* POSIX allows ENOTEMPTY or EEXIST */
		  if ( errno != ENOTEMPTY && errno != EEXIST ) {
		    fprintf( stderr,
			     "Warning: error trying to remove directory %s for %s: %s\n",
			     full_path, descr->hdr.pkg_name,
			     strerror( errno ) );
		    status = REMOVE_ERROR;
		  }
		  /*
		   * else it wasn't empty, so we didn't want to remove
		   * it anyway
		   */
		}
	      }
	      /* else it wasn't a directory, so nothing to do */
	    }
	    else {
	      /* lstat() failed */
	      if ( errno != ENOENT ) {
		fprintf( stderr,
			 "Warning: lstat() failed trying to remove directory %s for %s: %s\n",
			 e->filename, descr->hdr.pkg_name, strerror( errno ) );
		status = REMOVE_ERROR;
	      }
	      /*
	       * If it's ENOENT, the directory was removed, so no error; just
	       * remove its pkgdb entry
	       */
	    }
	    free( full_path );
	  }
	  else {
	    fprintf( stderr,
		     "Warning: out of memory trying to remove directory %s for %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }

	  /* Get it out of the pkg db regardless */
	  result = delete_from_pkg_db( db, canonical_path );
	  if ( result != 0 ) {
	    fprintf( stderr,
		     "Warning: failed to remove pkgdb entry for directory %s in %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }
	}
	/* else something else claims it now, so skip it */

	free( owner );
      }
      /* else it isn't claimed any more, so skip it */

      free( canonical_path );
    }
    else {
      fprintf( stderr,
	       "Warning: out of memory trying to remove directory %s for %s\n",
	       e->filename, descr->hdr.pkg_name );
      status = REMOVE_ERROR;
    }
  }
  else status = REMOVE_ERROR;

  return status;
}

static int remove_file( pkg_db *db, pkg_descr *descr, pkg_descr_entry *e ) {
  int status, result;
  char *full_path, *owner, *canonical_path;
  struct stat buf;

  status = REMOVE_SUCCESS;
  if ( db && descr && e && e->type == ENTRY_FILE ) {
    canonical_path = canonicalize_and_copy( e->filename );
    if ( canonical_path ) {
      owner = query_pkg_db( db, canonical_path );
      if ( owner ) {
	if ( strcmp( owner, descr->hdr.pkg_name ) == 0 ) {
	  full_path = concatenate_paths( get_root(), e->filename );
	  if ( full_path ) {
	    result = lstat( full_path, &buf );
	    if ( result == 0 ) {
	      if ( S_ISREG( buf.st_mode ) ) {
		if ( buf.st_mtime == descr->hdr.pkg_time ) {
		  if ( get_check_md5() ) {
		    result = file_hash_matches( full_path, e->u.f.hash );
		    if ( result == 1 ) {
		      /* Hashes match, remove it */
		      printf( "RF %s\n", full_path );
		      unlink( full_path );
		    }
		    else if ( result != 0 ) {
		      /* Error checking hash */
		      fprintf( stderr,
			       "Warning: couldn't check MD5 of file %s for %s\n",
			       full_path, descr->hdr.pkg_name );
		      status = REMOVE_ERROR;
		    }
		  }
		  else {
		    /* No MD5 check, remove it */
		    printf( "RF %s\n", full_path );
		    unlink( full_path );
		  }
		}
		/* else mtimes don't match, so nothing to do */
	      }
	      /* else it wasn't a file, so nothing to do */
	    }
	    else {
	      /* lstat() failed */
	      if ( errno != ENOENT ) {
		fprintf( stderr,
			 "Warning: lstat() failed trying to remove file %s for %s: %s\n",
			 e->filename, descr->hdr.pkg_name, strerror( errno ) );
		status = REMOVE_ERROR;
	      }
	      /*
	       * If it's ENOENT, the file was removed, so no error; just
	       * remove its pkgdb entry
	       */
	    }
	    free( full_path );
	  }
	  else {
	    fprintf( stderr,
		     "Warning: out of memory trying to remove file %s for %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }
	  
	  /* Get it out of the pkg db regardless */
	  result = delete_from_pkg_db( db, canonical_path );
	  if ( result != 0 ) {
	    fprintf( stderr,
		     "Warning: failed to remove pkgdb entry for file %s in %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }
	}
	/* else something else claims it now, so skip it */

	free( owner );
      }
      /* else it isn't claimed any more, so skip it */

      free( canonical_path );
    }
    else {
      fprintf( stderr,
	       "Warning: out of memory trying to remove file %s for %s\n",
	       e->filename, descr->hdr.pkg_name );
      status = REMOVE_ERROR;
    }
  }
  else status = REMOVE_ERROR;

  return status;
}

void remove_help( void ) {
  printf( "Remove packages.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] remove <package 1> <package 2> ...\n" );
  printf( "\n" );
  printf( "<package 1>, etc., are packages names of packages to remove.\n" );
}

void remove_main( int argc, char **argv ) {
  int i, status;
  pkg_db *db;

  if ( argc > 0 ) {
    status = sanity_check_globals();
    if ( status == 0 ) {
      db = open_pkg_db();
      if ( db ) {
	for ( i = 0; i < argc; ++i ) {
	  status = remove_pkg( db, argv[i] );
	  if ( status != REMOVE_SUCCESS ) {
	    fprintf( stderr, "Failed to remove %s\n", argv[i] );
	    break;
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
	     "At least one package must be specified to remove\n" );
  }
}

static int remove_pkg_by_descr( pkg_db *db, pkg_descr *descr ) {
  int status, result, i;
  pkg_descr_entry *e;
  rbtree *dir_queue;
  rbtree_node *n;
  char *path;
  void *e_v;

  status = REMOVE_SUCCESS;
  if ( db && descr ) {
    /* Allocate an rbtree to queue directories in */
    dir_queue = rbtree_alloc( post_path_comparator,
			      NULL, NULL, NULL, NULL );
    if ( dir_queue ) {
      /*
       * Scan over the description; process files and symlinks as we see
       * them, and queue directories up for later.
       */
      for ( i = 0; i < descr->num_entries; ++i ) {
	e = &(descr->entries[i]);
	switch ( e->type ) {
	case ENTRY_DIRECTORY:
	  result = rbtree_insert( dir_queue, e->filename, e );
	  if ( result != RBTREE_SUCCESS ) {
	    fprintf( stderr,
		     "Warning: unable to queue directory %s while removing %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }
	  break;
	case ENTRY_FILE:
	  result = remove_file( db, descr, e );
	  if ( result != REMOVE_SUCCESS ) {
	    fprintf( stderr, "Warning: unable to remove file %s from %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = result;
	  }
	  break;
	case ENTRY_SYMLINK:
	  result = remove_symlink( db, descr, e );
	  if ( result != REMOVE_SUCCESS ) {
	    fprintf( stderr, "Warning: unable to remove symlink %s from %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = result;
	  }
	  break;
	case ENTRY_LAST:
	  /* Skip it */
	  break;
	default:
	  fprintf( stderr,
		   "Warning: saw unknown entry type for %s while removing %s\n",
		   e->filename, descr->hdr.pkg_name );
	  /* Skip it */
	}
      }

      /* Now we enumerate the directory queue and remove the directories */
      n = NULL;
      do {
	e = NULL;
	path = rbtree_enum( dir_queue, n, &e_v, &n );
	if ( path ) {
	  if ( e_v ) {
	    e = (pkg_descr_entry *)e_v;
	    result = remove_directory( db, descr, e );
	    if ( result != REMOVE_SUCCESS ) {
	      fprintf( stderr,
		       "Warning: unable to remove directory %s from %s\n",
		       e->filename, descr->hdr.pkg_name );
	      status = result;
	    }
	  }
	  else {
	    fprintf( stderr, 
		     "Warning: NULL entry at filename %s while enumerating directories to remove (in %s)\n",
		     path, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }
	}
	/* else we're done */
      } while ( n );

      rbtree_free( dir_queue );
    }
    else {
      fprintf( stderr, "Unable to allocate memory to remove %s\n",
	       descr->hdr.pkg_name );
      status = REMOVE_ERROR;
    }
  }
  else status = REMOVE_ERROR;

  return status;
}

static int remove_pkg( pkg_db *db, const char *pkg ) {
  int status, result;
  char *descr_path;
  struct stat buf;
  pkg_descr *descr;

  status = REMOVE_SUCCESS;
  if ( db && pkg ) {
    descr_path = concatenate_paths( get_pkg(), pkg );
    if ( descr_path ) {
      result = stat( descr_path, &buf );
      if ( result == 0 ) {
	descr = read_pkg_descr_from_file( descr_path );
	if ( descr ) {
	  result = remove_pkg_by_descr( db, descr );
	  free_pkg_descr( descr );
	  if ( result == REMOVE_SUCCESS ) {
	    /*
	     * We succeeded, so no pkgdb entries remain referring to
	     * this package.  Now we can remove the description.
	     */
	    unlink( descr_path );
	  }
	  else status = result;
	}
	else {
	  fprintf( stderr,
		   "Unable to read package description from %s\n",
		   descr_path );
	  status = REMOVE_ERROR;
	}
      }
      else {
	fprintf( stderr,
		 "Couldn't stat() package description %s: %s\n",
		 descr_path, strerror( errno ) );
	status = REMOVE_ERROR;
      }
      free( descr_path );
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory trying to remove %s\n",
	       pkg );
      status = REMOVE_ERROR;
    }
  }
  else status = REMOVE_ERROR;
  
  return status;
}

static int remove_symlink( pkg_db *db, pkg_descr *descr, pkg_descr_entry *e ) {
  int status, result;
  char *full_path, *owner, *target, *canonical_path;
  struct stat buf;

  status = REMOVE_SUCCESS;
  if ( db && descr && e && e->type == ENTRY_SYMLINK ) {
    canonical_path = canonicalize_and_copy( e->filename );
    if ( canonical_path ) {
      owner = query_pkg_db( db, canonical_path );
      if ( owner ) {
	if ( strcmp( owner, descr->hdr.pkg_name ) == 0 ) {
	  full_path = concatenate_paths( get_root(), e->filename );
	  if ( full_path ) {
	    result = lstat( full_path, &buf );
	    if ( result == 0 ) {
	      if ( S_ISLNK( buf.st_mode ) ) {
		target = NULL;
		result = read_symlink_target( full_path, &target );
		if ( result == READ_SYMLINK_SUCCESS ) {
		  if ( strcmp( target, e->u.s.target ) == 0 ) {
		    /* They match, so delete the symlink */
		    printf( "RS %s\n", full_path );
		    unlink( full_path );
		  }
		  /* else nothing to do */
		  free( target );
		}
		else {
		  fprintf( stderr,
			   "Warning: unable to read target of symlink %s for %s\n",
			   full_path, descr->hdr.pkg_name );
		  status = REMOVE_ERROR;
		}
	      }
	      /* else it wasn't a symlink, so nothing to do */
	    }
	    else {
	      /* lstat() failed */
	      if ( errno != ENOENT ) {
		fprintf( stderr,
			 "Warning: lstat() failed trying to remove symlink %s for %s: %s\n",
			 e->filename, descr->hdr.pkg_name, strerror( errno ) );
		status = REMOVE_ERROR;
	      }
	      /*
	       * If it's ENOENT, the symlink was removed, so no error; just
	       * remove its pkgdb entry
	       */
	    }
	    free( full_path );
	  }
	  else {
	    fprintf( stderr,
		     "Warning: out of memory trying to remove symlink %s for %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }

	  /* Get it out of the pkg db regardless */
	  result = delete_from_pkg_db( db, e->filename );
	  if ( result != 0 ) {
	    fprintf( stderr,
		     "Warning: failed to remove pkgdb entry for symlink %s in %s\n",
		     e->filename, descr->hdr.pkg_name );
	    status = REMOVE_ERROR;
	  }
	}
	/* else something else claims it now, so skip it */

	free( owner );
      }
      /* else it isn't claimed any more, so skip it */

      free( canonical_path );
    }
    else {
      fprintf( stderr,
	       "Warning: out of memory trying to remove symlink %s for %s\n",
	       e->filename, descr->hdr.pkg_name );
      status = REMOVE_ERROR;
    }
  }
  else status = REMOVE_ERROR;

  return status;
}
