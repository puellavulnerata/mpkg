#include <pkg.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pkg_descr_entry * find_descr_entry( pkg_descr *, const char * );
static void show_status( const char *, struct stat *, const char *,
			 pkg_descr *, pkg_descr_entry * );
static void status_file( const char * );
static void status_pkg( const char * );

static pkg_descr_entry * find_descr_entry( pkg_descr *d, const char *p ) {
  pkg_descr_entry *e;
  char *canonical;
  int i, r;

  e = NULL;
  if ( d && p ) {
    for ( i = 0; i < d->num_entries; ++i ) {
      canonical = canonicalize_and_copy( d->entries[i].filename );
      if ( canonical ) {
	r = strcmp( canonical, p );
	free( canonical );
	if ( r == 0 ) {
	  e = &(d->entries[i]);
	  break;
	}
      }
      else {
	fprintf( stderr, "Unable to allocate memory in find_descr_entry()\n" );
      }
    }
  }

  return e;
}

static void show_status( const char *filename, struct stat *st,
			 const char *pkg, pkg_descr *descr,
			 pkg_descr_entry *entry ) {
  int result;
  char *link_target;
  uint8_t hash[HASH_LEN];

  if ( filename && pkg && descr && entry ) {
    if ( st ) {
      switch ( entry->type ) {
      case ENTRY_DIRECTORY:
	if ( S_ISDIR( st->st_mode ) ) {
	  printf( "%s is owned by %s (as a directory)\n", filename, pkg );
	}
	else {
	  printf( "%s is claimed as a directory by %s, but is not one.\n",
		  filename, pkg );
	}
	break;
      case ENTRY_FILE:
	if ( S_ISREG( st->st_mode ) ) {
	  if ( get_check_md5() ) {
	    result = get_file_hash( filename, hash );
	    if ( result == 0 ) {
	      if ( memcmp( hash, entry->u.f.hash, sizeof( hash ) ) == 0 ) {
		printf( "%s is owned by %s (as a file) (by MD5)\n",
			filename, pkg );
	      }
	      else {
		printf( "%s is claimed as a file by %s, ",
			filename, pkg );
		printf( "but it has been modified (by MD5)\n" );
	      }
	    }
	    else {
	      printf( "%s is claimed as a file by %s, ",
		      filename, pkg );
	      printf( "but its MD5 could not be read\n" );
	    }
	  }
	  else {
	    if ( st->st_mtime == descr->hdr.pkg_time ) {
	      printf( "%s is owned by %s (as a file) (by mtime)\n",
		      filename, pkg );
	    }
	    else {
	      printf( "%s is claimed as a file by %s, ",
		      filename, pkg );
	      printf( "but it has been modified (by mtime)\n" );
	    }
	  }
	}
	else {
	  printf( "%s is claimed as a file by %s, but is not one.\n",
		  filename, pkg );
	}
	break;
      case ENTRY_SYMLINK:
	if ( S_ISLNK( st->st_mode ) ) {
	  link_target = NULL;
	  result = read_symlink_target( filename, &link_target );
	  if ( result == READ_SYMLINK_SUCCESS ) {
	    if ( strcmp( link_target, entry->u.s.target ) == 0 ) {
	      printf( "%s is owned by %s (as a symlink)\n", filename, pkg );
	    }
	    else {
	      printf( "%s is claimed as a symlink by %s, ",
		      filename, pkg );
	      printf( "but its target has been modified\n" );
	    }
	  }
	  else {
	    printf( "%s is claimed as a symlink by %s, ", filename, pkg );
	    printf( "but its target could not be read.\n" );
	  }

	  if ( link_target ) free( link_target );
	}
	else {
	  printf( "%s is claimed as a symlink by %s, but is not one.\n",
		  filename, pkg );
	}
	break;
      default:
	printf( "%s exists and is claimed as an unknown type by %s\n",
		filename, pkg );
      }
    }
    else {
      printf( "%s is claimed by %s, but does not exist.\n", filename, pkg );
    }
  }
  else {
    fprintf( stderr, "Internal error in show_status()\n" );
  }
}

static void status_file( const char *filename ) {
  char *adjusted, *pkg, *descr_file;
  pkg_db *db;
  struct stat st;
  int result, have_stat, not_found;
  pkg_descr *descr;
  pkg_descr_entry *entry;
  int error;

  error = 0;
  have_stat = 0;
  result = lstat( filename, &st );
  if ( result == 0 ) {
    have_stat = 1;
    not_found = 0;
  }
  else {
    if ( errno == ENOENT ) {
      have_stat = 1;
      not_found = 1;
    }
    else {
      fprintf( stderr, "Error: couldn't lstat( \"%s\" ): %s\n",
	       filename, strerror( errno ) );
      error = 1;
    }
  }

  if ( !error && have_stat ) {
    adjusted = adjust_path_against_root( filename );
    if ( adjusted ) {
      db = open_pkg_db();
      if ( db ) {
	/* Check the package DB for this path */
	pkg = query_pkg_db( db, adjusted );
	close_pkg_db( db );

	/* Next, try to load the associated package-description and find it */
	descr = NULL;
	entry = NULL;
	if ( pkg ) {
	  descr_file = concatenate_paths( get_pkg(), pkg );
	  if ( descr_file ) {
	    descr = read_pkg_descr_from_file( descr_file );
	    if ( descr ) {
	      entry = find_descr_entry( descr, adjusted );
	    }
	    free( descr_file );
	  }
	  else {
	    fprintf( stderr, "Unable to allocate memory\n" );
	    error = 1;
	  }
	}

	if ( pkg ) {
	  if ( descr ) {
	    if ( entry ) show_status( filename, not_found ? NULL : &st,
				      pkg, descr, entry );
	    else {
	      if ( not_found ) {
		printf( "%s does not exist; it is claimed by %s, but the ",
			filename, pkg );
		printf( "package-description does not mention it.\n" );
	      }
	      else {
		printf( "%s exists and is claimed by %s, but the ",
			filename, pkg );
		printf( "package-description does not mention it.\n" );
	      }
	    }
	  }
	  else {
	    if ( not_found ) {
	      printf( "%s does not exist, but is claimed by %s, for which",
		      filename, pkg );
	      printf( " no description could be loaded.\n" );
	    }
	    else {
	      printf( "%s exists and is claimed by %s, but no description",
		      filename, pkg );
	      printf( " could be loaded\n" );
	    }
	  }
	}
	else {
	  if ( not_found ) {
	    printf( "%s does not exist\n", filename );
	  }
	  else {
	    printf( "%s is not under package management\n", filename );
	  }
	}

	if ( descr ) free_pkg_descr( descr );
	if ( pkg ) free( pkg );
      }
      else {
	fprintf( stderr, "Unable to open package db\n" );
	error = 1;
      }
      
      free( adjusted );
    }
    else {
      fprintf( stderr, "The path %s is not in the current root %s\n",
	       filename, get_root() );
      error = 1;
    }
  }
}

void status_help( void ) {
  printf( "Check the status of a file or installed package.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] status [file | pkg] <name>\n" );
  printf( "\n" );
  printf( "If 'file' or 'pkg' are not specified, the status command checks" );
  printf( " whether a file by that name exists.  If it does, it assumes " );
  printf( "<name> is a filename.  Otherwise it interprets <name> as a " );
  printf( "package name.\n" );
}

void status_main( int argc, char **argv ) {
  int result, file;
  struct stat st;

  if ( argc == 1 || argc == 2 ) {
    if ( argc == 1 ) {
      /*
       * Try to stat argv[0] to see if there's a file we can use
       * there.  If not, treat it as a package.
       */
      file = 0;
      result = lstat( argv[0], &st );
      if ( result == 0 ) {
	/* lstat() succeeded; could it possibly be under management? */
	if ( S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode ) ||
	     S_ISDIR( st.st_mode ) ) {
	  /* it could, go for it */
	  file = 1;
	}
      }
      /* else couldn't stat it, so try it as a package */

      if ( file ) status_file( argv[0] );
      else status_pkg( argv[0] );
    }
    else {
      /* argc == 2, argv[0] specifies package or file */

      if ( strcmp( argv[0], "file" ) == 0 ) status_file( argv[1] );
      else if ( strcmp( argv[0], "pkg" ) == 0 ) status_pkg( argv[1] );
      else {
	fprintf( stderr,
		 "Unknown type %s for status (must be file or pkg)\n",
		 argv[0] );
      }
    }
  }
  else {
    fprintf( stderr, "Wrong number of arguments to mpkg status.\n" );
    fprintf( stderr, "Syntax is:\n\n" );
    fprintf( stderr, "mpkg status ( <package name> | <filename> )\n" );
    fprintf( stderr, "mpkg status file <filename>\n" );
    fprintf( stderr, "mpkg status pkg <package name>\n" );
  }
}

static void status_pkg( const char *pkgname ) {
  char *descr_file, *p, *full_p, *pkg_from_db;
  pkg_descr *descr;
  pkg_descr_entry *e;
  pkg_db *db;
  int error, i, result;
  struct stat st;
  int not_found, have_stat;

  /* Try to load the package-description */
  descr = NULL;
  error = 0;
  descr_file = concatenate_paths( get_pkg(), pkgname );
  if ( descr_file ) {
    descr = read_pkg_descr_from_file( descr_file );
    free( descr_file );
  }
  else {
    fprintf( stderr, "Unable to allocate memory\n" );
    error = 1;
  }

  if ( !error ) {
    if ( descr ) {
      /* We got it; now try to open the database */
      db = open_pkg_db();
      if ( db ) {
	for ( i = 0; i < descr->num_entries; ++i ) {
	  e = &(descr->entries[i]);
	  p = canonicalize_and_copy( e->filename );
	  full_p = concatenate_paths( get_root(), e->filename );

	  if ( p && full_p ) {
	    /* Check what package claims this in the database */
	    pkg_from_db = query_pkg_db( db, p );
	    if ( pkg_from_db ) {
	      /* Check if that's ours */
	      if ( strcmp( pkg_from_db, pkgname ) == 0 ) {
		/*
		 * It is; try to stat it, and we'll pass the results
		 * on to show_status() which will do the checks
		 * specific to this case.
		 */

		have_stat = 0;
		not_found = 0;
		result = lstat( full_p, &st );
		if ( result == 0 ) have_stat = 1;
		else {
		  if ( errno == ENOENT ) {
		    have_stat = 1;
		    not_found = 1;
		  }
		}

		if ( have_stat ) {
		  show_status( full_p, not_found ? NULL : &st, pkgname,
			       descr, e );
		}
		else {
		  printf( "%s is claimed by this package, ", full_p );
		  printf( "but lstat() failed\n" );
		}
	      }
	      else {
		/* It isn't */
		printf( "%s has been claimed by %s\n",
			full_p, pkg_from_db );
	      }

	      free( pkg_from_db );
	    }
	    else {
	      printf( "%s is unclaimed in the database\n", full_p );
	    }
	  }
	  else {
	    fprintf( stderr, "Unable to allocate memory checking %s\n",
		     e->filename );
	  }

	  if ( p ) free( p );
	  if ( full_p ) free( full_p );
	}

	/* Close the database */
	close_pkg_db( db );
      }
      else {
	fprintf( stderr, "Unable to open package database.\n" );
      }
    }
    else {
      /* No package-description */
      printf( "Couldn't load package-description for %s\n", pkgname );
    }
  }

  /* Free the package-description if we need to */
  if ( descr ) free_pkg_descr( descr );
}
