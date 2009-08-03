#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>

#include <pkg.h>

#define CREATE_SUCCESS 0
#define CREATE_ERROR -1

typedef enum {
  ENABLED,
  DISABLED,
  DEFAULT
} create_boolean_opt;

typedef struct {
  char *input_directory, *pkg_name;
  emit_opts *emit;
  create_boolean_opt dirs, files, symlinks;
} create_opts;

typedef struct {
  char *owner, *group;
  mode_t mode;
} create_dir_info;

typedef struct {
  char *src_path;
  char *owner, *group;
  mode_t mode;
  uint8_t hash[HASH_LEN];
} create_file_info;

typedef struct {
  char *owner, *group;
  char *target;
} create_symlink_info;

typedef struct {
  rbtree *dirs, *files, *symlinks;
  int dirs_count, files_count, symlinks_count;
} create_pkg_info;

static create_opts * alloc_create_opts( void );
static create_pkg_info * alloc_pkginfo( create_opts * );
static int build_pkg_descr_dirs( create_opts *, create_pkg_info *,
				 pkg_descr * );
static int build_pkg_descr_files( create_opts *, create_pkg_info *,
				  pkg_descr * );
static int build_pkg_descr_symlinks( create_opts *, create_pkg_info *,
				     pkg_descr * );
static int build_pkg_descr( create_opts *, create_pkg_info *, pkg_descr ** );
static void build_pkg( create_opts * );
#ifdef PKGFMT_V1
static int check_path_for_descr( const char * );
#endif /* PKGFMT_V1 */
static int create_parse_options( create_opts *, int, char ** );
static void * dir_info_copier( void * );
static void dir_info_free( void * );
static int emit_descr( create_opts *, pkg_descr *, tar_writer * );
static int emit_files( create_opts *, create_pkg_info *, tar_writer * );
static void * file_info_copier( void * );
static void file_info_free( void * );
static void free_create_opts( create_opts * );
static void free_pkginfo( create_pkg_info * );
static int get_dirs_enabled( create_opts * );
static int get_files_enabled( create_opts * );
static time_t get_pkg_mtime( create_opts * );
static char * get_pkg_name( create_opts * );
static int get_symlinks_enabled( create_opts * );
static char * guess_pkg_name_from_input_directory( const char * );
static char * guess_pkg_name_from_output_file( const char * );
static int scan_directory_tree_internal( create_opts *, create_pkg_info *,
					 const char *, const char * );
static int scan_directory_tree( create_opts *, create_pkg_info * );
static int set_compression_arg( create_opts *, char * );
static void set_default_opts( create_opts * );
static int set_pkg_time_arg( create_opts *, char * );
static int set_version_arg( create_opts *, char * );
static void * symlink_info_copier( void * );
static void symlink_info_free( void * );

static create_opts * alloc_create_opts( void ) {
  create_opts *opts;

  opts = malloc( sizeof( *opts ) );
  if ( opts ) {
    opts->input_directory = NULL;
    opts->pkg_name = NULL;
    opts->dirs = DEFAULT;
    opts->files = DEFAULT;
    opts->symlinks = DEFAULT;
    opts->emit = malloc( sizeof( *(opts->emit) ) );
    if ( opts->emit ) {
      opts->emit->output_file = NULL;
      opts->emit->pkg_mtime = 0;
      opts->emit->compression = DEFAULT_COMPRESSION;
      opts->emit->version = DEFAULT_VERSION;
    }
    else {
      free( opts );
      opts = NULL;
    }
  }

  return opts;
}

static create_pkg_info * alloc_pkginfo( create_opts *opts ) {
  create_pkg_info *temp;
  int error;

  if ( opts ) {
    temp = malloc( sizeof( *temp ) );
    if ( temp ) {
      error = 0;
      temp->dirs = NULL;
      temp->files = NULL;
      temp->symlinks = NULL;
      temp->dirs_count = 0;
      temp->files_count = 0;
      temp->symlinks_count = 0;
   
      /*
       * Use pre_path_comparator() for all of these so things appear
       * in creation order.
       */

      if ( get_dirs_enabled( opts ) && !error ) {
	temp->dirs = rbtree_alloc( post_path_comparator,
				   rbtree_string_copier,
				   rbtree_string_free,
				   dir_info_copier,
				   dir_info_free );
	if ( !(temp->dirs) ) error = 1;
      }

      if ( get_files_enabled( opts ) && !error ) {
	temp->files = rbtree_alloc( post_path_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    file_info_copier,
				    file_info_free );
	if ( !(temp->files) ) error = 1;
      }

      if ( get_symlinks_enabled( opts ) && !error ) {
	temp->symlinks = rbtree_alloc( post_path_comparator,
				       rbtree_string_copier,
				       rbtree_string_free,
				       symlink_info_copier,
				       symlink_info_free );
	if ( !(temp->symlinks) ) error = 1;
      }
      
      if ( error ) {
	if ( temp->dirs ) rbtree_free( temp->dirs );
	if ( temp->files ) rbtree_free( temp->files );
	if ( temp->symlinks ) rbtree_free( temp->symlinks );
	free( temp );
	temp = NULL;
      }
    }
    /* else error, temp is already NULL */
  }
  else temp = NULL;

  return temp;
}

static int build_pkg_descr_dirs( create_opts *opts, create_pkg_info *pkginfo,
				 pkg_descr *descr ) {
  int status, result, i, count;
  rbtree_node *n;
  char *path, *temp, *cmp, *marker;
  void *info_v;
  create_dir_info *di;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && descr ) {
    if ( pkginfo->dirs_count > 0 ) {
      if ( pkginfo->dirs && /* dirs must exist */
	   /* the caller must have allocated enough space */
	   descr->num_entries + pkginfo->dirs_count <=
	   descr->num_entries_alloced ) {
	/* count how many entries we see */
	count = 0;
	n = NULL;
	do {
	  di = NULL;
	  path = rbtree_enum( pkginfo->dirs, n, &info_v, &n );
	  if ( path ) {
	    if ( info_v ) {
	      di = (create_dir_info *)info_v;

#ifdef PKGFMT_V1
	      /*
	       * Don't allow anything named starting in
	       * /package-description in V1, since the
	       * package-description file lives in the same tarball
	       * with the package contents.
	       */

	      if ( get_version( opts->emit ) == V1 ) {
		result = check_path_for_descr( path );
		if ( result == 1 ) {
		  fprintf( stderr,
			   "Can't use path %s in V1 format\n",
			   path );
		  status = CREATE_ERROR;
		}
		else if ( result == -1 ) {
		  fprintf( stderr, "Unable to allocate memory\n" );
		  status = CREATE_ERROR;
		}
	      }
#endif /* PKGFMT_V1 */

	      if ( status == CREATE_SUCCESS ) {
		/* don't do more than dirs_count */
		if ( count < pkginfo->dirs_count ) {
		  i = descr->num_entries;
		  if ( i < descr->num_entries_alloced ) {
		    descr->entries[i].type = ENTRY_DIRECTORY;
		    descr->entries[i].filename = copy_string( path );
		    descr->entries[i].owner = copy_string( di->owner );
		    descr->entries[i].group = copy_string( di->group );
		    descr->entries[i].u.d.mode = di->mode;
			  
		    if ( descr->entries[i].filename &&
			 descr->entries[i].owner &&
			 descr->entries[i].group ) {
		      ++(descr->num_entries);
		      ++count;
		    }
		    else {
		      if ( descr->entries[i].filename )
			free( descr->entries[i].filename );
		      if ( descr->entries[i].owner )
			free( descr->entries[i].owner );
		      if ( descr->entries[i].group )
			free( descr->entries[i].group );
		      fprintf( stderr, "Unable to allocate memory\n" );
		      status = CREATE_ERROR;
		    }
		  }
		  else {
		    fprintf( stderr,
			     "Internal error during package creation (ran out of pkg_descr_entry slots at directory %s)\n",
			     path );
		    status = CREATE_ERROR;
		  }
		}
		else {
		  fprintf( stderr,
			   "Internal error during package creation (saw too many dirs in rbtree at directory %s)\n",
			   path );
		  status = CREATE_ERROR;
		}
	      }
	      /*
	       * else we already emitted the error message when we
	       * checked the path for V1 validity
	       */
	    }
	    else {
	      fprintf( stderr,
		       "Internal error during package creation (enumerating dirs rbtree, path %s)\n",
		       path );
	      status = CREATE_ERROR;
	    }
	  }
	  /* else we're done */
	} while ( n && status == CREATE_SUCCESS );
      }
      else status = CREATE_ERROR;
    }
    /* else nothing to do */
  }
  else status = CREATE_ERROR;

  return status;
}

static int build_pkg_descr_files( create_opts *opts, create_pkg_info *pkginfo,
				  pkg_descr *descr ) {
  int status, result, i, count;
  rbtree_node *n;
  char *path;
  void *info_v;
  create_file_info *fi;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && descr ) {
    if ( pkginfo->files_count > 0 ) {
      if ( pkginfo->files && /* files must exist */
	   /* the caller must have allocated enough space */
	   descr->num_entries + pkginfo->files_count <=
	   descr->num_entries_alloced ) {
	/* count how many entries we see */
	count = 0;
	n = NULL;
	do {
	  fi = NULL;
	  path = rbtree_enum( pkginfo->files, n, &info_v, &n );
	  if ( path ) {
	    if ( info_v ) {
	      fi = (create_file_info *)info_v;

#ifdef PKGFMT_V1
	      /*
	       * Don't allow anything named starting in
	       * /package-description in V1, since the
	       * package-description file lives in the same tarball
	       * with the package contents.
	       */

	      if ( get_version( opts->emit ) == V1 ) {
		result = check_path_for_descr( path );
		if ( result == 1 ) {
		  fprintf( stderr,
			   "Can't use path %s in V1 format\n",
			   path );
		  status = CREATE_ERROR;
		}
		else if ( result == -1 ) {
		  fprintf( stderr, "Unable to allocate memory\n" );
		  status = CREATE_ERROR;
		}
	      }
#endif /* PKGFMT_V1 */

	      if ( status == CREATE_SUCCESS ) {
		/* don't do more than files_count */
		if ( count < pkginfo->files_count ) {
		  i = descr->num_entries;
		  if ( i < descr->num_entries_alloced ) {
		    descr->entries[i].type = ENTRY_FILE;
		    descr->entries[i].filename = copy_string( path );
		    descr->entries[i].owner = copy_string( fi->owner );
		    descr->entries[i].group = copy_string( fi->group );
		    descr->entries[i].u.f.mode = fi->mode;
		    memcpy( descr->entries[i].u.f.hash, fi->hash,
			    sizeof( fi->hash ) );
			  
		    if ( descr->entries[i].filename &&
			 descr->entries[i].owner &&
			 descr->entries[i].group ) {
		      ++(descr->num_entries);
		      ++count;
		    }
		    else {
		      if ( descr->entries[i].filename )
			free( descr->entries[i].filename );
		      if ( descr->entries[i].owner )
			free( descr->entries[i].owner );
		      if ( descr->entries[i].group )
			free( descr->entries[i].group );
		      fprintf( stderr, "Unable to allocate memory\n" );
		      status = CREATE_ERROR;
		    }
		  }
		  else {
		    fprintf( stderr,
			     "Internal error during package creation (ran out of pkg_descr_entry slots at file %s)\n",
			     path );
		    status = CREATE_ERROR;
		  }
		}
		else {
		  fprintf( stderr,
			   "Internal error during package creation (saw too many files in rbtree at file %s)\n",
			   path );
		  status = CREATE_ERROR;
		}
	      }
	      /*
	       * else we already emitted the error message when we
	       * checked the path for V1 validity
	       */
	    }
	    else {
	      fprintf( stderr,
		       "Internal error during package creation (enumerating files rbtree, path %s)\n",
		       path );
	      status = CREATE_ERROR;
	    }
	  }
	  /* else we're done */
	} while ( n && status == CREATE_SUCCESS );
      }
      else status = CREATE_ERROR;
    }
    /* else nothing to do */
  }
  else status = CREATE_ERROR;

  return status;
}

static int build_pkg_descr_symlinks( create_opts *opts, create_pkg_info *pkginfo,
				     pkg_descr *descr ) {
  int status, result, i, count;
  rbtree_node *n;
  char *path;
  void *info_v;
  create_symlink_info *si;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && descr ) {
    if ( pkginfo->symlinks_count > 0 ) {
      if ( pkginfo->symlinks && /* symlinks must exist */
	   /* the caller must have allocated enough space */
	   descr->num_entries + pkginfo->symlinks_count <=
	   descr->num_entries_alloced ) {
	/* count how many entries we see */
	count = 0;
	n = NULL;
	do {
	  si = NULL;
	  path = rbtree_enum( pkginfo->symlinks, n, &info_v, &n );
	  if ( path ) {
	    if ( info_v ) {
	      si = (create_symlink_info *)info_v;

#ifdef PKGFMT_V1
	      /*
	       * Don't allow anything named starting in
	       * /package-description in V1, since the
	       * package-description file lives in the same tarball
	       * with the package contents.
	       */

	      if ( get_version( opts->emit ) == V1 ) {
		result = check_path_for_descr( path );
		if ( result == 1 ) {
		  fprintf( stderr,
			   "Can't use path %s in V1 format\n",
			   path );
		  status = CREATE_ERROR;
		}
		else if ( result == -1 ) {
		  fprintf( stderr, "Unable to allocate memory\n" );
		  status = CREATE_ERROR;
		}
	      }
#endif /* PKGFMT_V1 */

	      if ( status == CREATE_SUCCESS ) {
		/* don't do more than dirs_count */
		if ( count < pkginfo->symlinks_count ) {
		  i = descr->num_entries;
		  if ( i < descr->num_entries_alloced ) {
		    descr->entries[i].type = ENTRY_SYMLINK;
		    descr->entries[i].filename = copy_string( path );
		    descr->entries[i].owner = copy_string( si->owner );
		    descr->entries[i].group = copy_string( si->group );
		    descr->entries[i].u.s.target = copy_string( si->target );
			  
		    if ( descr->entries[i].filename &&
			 descr->entries[i].owner &&
			 descr->entries[i].group &&
			 descr->entries[i].u.s.target ) {
		      ++(descr->num_entries);
		      ++count;
		    }
		    else {
		      if ( descr->entries[i].filename )
			free( descr->entries[i].filename );
		      if ( descr->entries[i].owner )
			free( descr->entries[i].owner );
		      if ( descr->entries[i].group )
			free( descr->entries[i].group );
		      if ( descr->entries[i].u.s.target )
			free( descr->entries[i].u.s.target );
		      fprintf( stderr, "Unable to allocate memory\n" );
		      status = CREATE_ERROR;
		    }
		  }
		  else {
		    fprintf( stderr,
			     "Internal error during package creation (ran out of pkg_descr_entry slots at symlink %s)\n",
			     path );
		    status = CREATE_ERROR;
		  }
		}
		else {
		  fprintf( stderr,
			   "Internal error during package creation (saw too many symlinks in rbtree at symlink %s)\n",
			   path );
		  status = CREATE_ERROR;
		}
	      }
	      /*
	       * else we already emitted the error message when we
	       * checked the path for V1 validity
	       */	      
	    }
	    else {
	      fprintf( stderr,
		       "Internal error during package creation (enumerating symlinks rbtree, path %s)\n",
		       path );
	      status = CREATE_ERROR;
	    }
	  }
	  /* else we're done */
	} while ( n && status == CREATE_SUCCESS );
      }
      else status = CREATE_ERROR;
    }
    /* else nothing to do */
  }
  else status = CREATE_ERROR;

  return status;
}

static int build_pkg_descr( create_opts *opts, create_pkg_info *pkginfo,
			    pkg_descr **descr_out ) {
  int status, result, i;
  pkg_descr *descr;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && descr_out ) {
    descr = malloc( sizeof( *descr) );
    if ( descr ) {
      descr->entries = NULL;
      descr->hdr.pkg_name = copy_string( get_pkg_name( opts ) );
      if ( !(descr->hdr.pkg_name) ) {
	fprintf( stderr, "Unable to allocate memory\n" );
	status = CREATE_ERROR;
      }
      descr->hdr.pkg_time = get_pkg_mtime( opts );
      descr->num_entries = 0;
      descr->num_entries_alloced = 0;

      /* Allocate enough space for everyting */
      if ( get_dirs_enabled( opts ) )
	descr->num_entries_alloced += pkginfo->dirs_count;
      if ( get_files_enabled( opts ) )
	descr->num_entries_alloced += pkginfo->files_count;
      if ( get_symlinks_enabled( opts ) )
	descr->num_entries_alloced += pkginfo->symlinks_count;

      if ( descr->num_entries_alloced > 0 ) {
	descr->entries =
	  malloc( sizeof( *(descr->entries) ) *
		  descr->num_entries_alloced );
	if ( descr->entries ) {
	  memset( descr->entries, sizeof( *(descr->entries) ) *
		  descr->num_entries_alloced, 0 );
	}
	else {
	  fprintf( stderr, "Unable to allocate memory\n" );
	  descr->num_entries_alloced = 0;
	  status = CREATE_ERROR;
	}
      }

      if ( status == CREATE_SUCCESS ) {
	/*
	 * Now we fill in descr->entries by traversing the
	 * rbtrees constructed in scan_directory_tree().
	 */

	/* Do the directories first */
	if ( pkginfo->dirs && get_dirs_enabled( opts ) &&
	     pkginfo->dirs_count > 0 ) {
	  result = build_pkg_descr_dirs( opts, pkginfo, descr );
	  if ( result != CREATE_SUCCESS ) status = result;
	}
	/* else nothing to do for dirs */

	/* Now do the files */
	if ( status == CREATE_SUCCESS && pkginfo->files &&
	     get_files_enabled( opts ) && pkginfo->files_count > 0 ) {
	  result = build_pkg_descr_files( opts, pkginfo, descr );
	  if ( result != CREATE_SUCCESS ) status = result;
	}
	/* else nothing to do for files */

	/* Do the symlinks last */
	if ( pkginfo->symlinks && get_symlinks_enabled( opts ) &&
	     pkginfo->symlinks_count > 0 ) {
	  result = build_pkg_descr_symlinks( opts, pkginfo, descr );
	  if ( result != CREATE_SUCCESS ) status = result;
	}
	/* else nothing to do for symlinks */
      }

      if ( status == CREATE_SUCCESS ) *descr_out = descr;
      else {
	free_pkg_descr( descr );
	*descr_out = NULL;
      }
    }
    else {
      fprintf( stderr, "Unable to allocate memory\n" );
      status = CREATE_ERROR;
    }
  }
  else status = CREATE_ERROR;

  return status;
}

static void build_pkg( create_opts *opts ) {
  int status, result;
  create_pkg_info *pkginfo;
  emit_pkg_streams *streams;
  pkg_descr *descr;

  status = CREATE_SUCCESS;
  if ( opts ) {
    pkginfo = alloc_pkginfo( opts );
    if ( pkginfo ) {
      streams = NULL;
      descr = NULL;

      /*
       * scan_directory_tree() recursively walks the directory tree
       * starting from opts->input_directory, and assembles the three
       * rbtrees in pkginfo; the keys are paths starting from the base
       * of the directory tree, and the values are create_dir_info,
       * create_file_info or create_symlink_info structures which can
       * be used later to assemble the pkgdescr entries in the correct
       * order, and, in the case of create_file_info, locate the
       * needed files when we emit the tarball.
       */

      result = scan_directory_tree( opts, pkginfo );
      if ( result == CREATE_SUCCESS ) {
	/* Construct the pkg_descr */
	result = build_pkg_descr( opts, pkginfo, &descr );
	if ( result != CREATE_SUCCESS ) status = result;

	/* Now we can free the directory and symlink rbtrees */
	if ( pkginfo->dirs ) {
	  rbtree_free( pkginfo->dirs );
	  pkginfo->dirs = NULL;
	}
	if ( pkginfo->symlinks ) {
	  rbtree_free( pkginfo->symlinks );
	  pkginfo->symlinks = NULL;
	}

	if ( status == CREATE_SUCCESS ) {
	  streams = start_pkg_streams( opts->emit );
	  if ( streams ) {
	    result = emit_descr( opts, descr, streams->pkg_tw );
	    if ( result != CREATE_SUCCESS ) {
	      fprintf( stderr, "Unable to emit package-description\n" );
	      status = result;
	    }

	    if ( status == CREATE_SUCCESS ) {
	      /* We can free descr here */
	      free_pkg_descr( descr );
	      descr = NULL;
	      /* Get ready to emit content */
	      result = start_pkg_content( opts->emit, streams );
	      if ( result == EMIT_SUCCESS ) {
		/* Now emit the files */
		result = emit_files( opts, pkginfo, streams->emit_tw );
		if ( result != CREATE_SUCCESS ) {
		  fprintf( stderr, "Unable to emit package contents\n" );
		  status = result;
		}
		finish_pkg_content( opts->emit, streams );
	      }
	      else {
		fprintf( stderr, "Unable to emit package contents\n" );
		status = CREATE_ERROR;
	      }
	    }

	    finish_pkg_streams( opts->emit, streams );
	    /* Remove the output if we had an error with it */
	    if ( status != CREATE_SUCCESS ) unlink( opts->emit->output_file );
	  }
	  else {
	    fprintf( stderr,
		     "Unable to open output streams for %s\n",
		     opts->emit->output_file );
	    status = CREATE_ERROR;
	  }

	  /*
	   * Make sure we free descr if we didn't get far enough to
	   * use and free it above.
	   */
	  if ( descr ) free_pkg_descr( descr );
	}
	else {
	  fprintf( stderr, "Unable to build package-description\n" );
	}

	/* Now we can free the files rbtree */
	if ( pkginfo->files ) {
	  rbtree_free( pkginfo->files );
	  pkginfo->files = NULL;
	}
      }
      else {
	fprintf( stderr,
		 "Failed to scan directory tree %s for package build\n",
		 opts->input_directory );
	status = result;
      }

      free_pkginfo( pkginfo );
    }
    else {
      fprintf( stderr, "Unable to allocate memory\n" );
    }
  }
  /* else can't-happen error */
}

#ifdef PKGFMT_V1

static int check_path_for_descr( const char *path ) {
  int result;
  char *temp, *cmp, *marker;

  /*
   * Check if the first component of path is package-description; if
   * it is, then this path is disallowed in V1 format packages.
   */
  result = 0;
  if ( path ) {
    temp = copy_string( path );
    if ( temp ) {
      cmp = get_path_component( temp, &marker );
      if ( cmp && strcmp( cmp, "package-description" ) == 0 ) result = 1;
      free( temp );
    }
    /* Signal error */
    else result = -1;
  }

  return result;
}

#endif /* PKGFMT_V1 */

void create_help( void ) {
  printf( "Create new packages from a directory of files.  Usage:\n\n" );
  printf( "mpkg [global options] create [options] <input> [<name>] " );
  printf( "<output>\n\n" );
  printf( "[options] may be one or more of:\n" );
  printf( "  --disable-dirs: Don't include directories in the " );
  printf( "package-description\n" );
  printf( "  --enable-dirs: Include directories in the " );
  printf( "package-description\n" );
  printf( "\n" );
  printf( "  --disable-files: Don't include regular files in the " );
  printf( "package-description\n" );
  printf( "  --enable-files: Include regular files in the " );
  printf( "package-description\n" );
  printf( "\n" );
  printf( "  --disable-symlinks: Don't include symlinks in the " );
  printf( "package-description\n" );
  printf( "  --enable-symlinks: Include symlinks in the " );
  printf( "package-description\n" );
  printf( "\n" );
  printf( "  --set-compression <compression>: use <compression> in the " );
  printf( "output.\n" );
  printf( "  <compression> can be one of:\n" );
#ifdef COMPRESSION_BZIP2
  printf( "    bzip2\n" );
#endif /* COMPRESSION_BZIP2 */
#ifdef COMPRESSION_GZIP
  printf( "    gzip\n" );
#endif /* COMPRESSION_GZIP */
  printf( "    none\n" );
  printf( "\n" );
  printf( "  --set-version <version>: use <version> in the output.\n" );
  printf( "  <version> can be one of:\n" );
#ifdef PKGFMT_V1
  printf( "    v1\n" );
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
  printf( "    v2\n" );
#endif /* PKGFMT_V2 */
  printf( "\n" );
  printf( "<input> is a directory of files to make the package from; " );
  printf( "<output> is the filename of the package to create.  If you do " );
  printf( "not specify --set-compression or --set-version, these will be " );
  printf( "guessed from <output>.  Emission of directories is disabled by " );
  printf( "default; emission of files and symlinks is enabled by default." );
  printf( "  If <name> is specified, it will be the name of the package; if" );
  printf( " it is omitted, the name will be guessed from <input> and " );
  printf( "<output>.\n" );
}

void create_main( int argc, char **argv ) {
  int result;
  create_opts *opts;

  opts = alloc_create_opts();
  if ( opts ) {
    set_default_opts( opts );
    result = create_parse_options( opts, argc, argv );
    if ( result == CREATE_SUCCESS ) build_pkg( opts );
    free_create_opts( opts );
  }
  else {
    fprintf( stderr,
	     "Unable to allocate memory trying to create package\n" );
  }
}

int create_parse_options( create_opts *opts, int argc, char **argv ) {
  int i, status;
  char *temp;

  status = CREATE_SUCCESS;
  if ( opts && argc >= 0 && argv ) {
    for ( i = 0; i < argc && status == CREATE_SUCCESS; ++i ) {
      if ( *(argv[i]) == '-' ) {
	/* It's an option */
	if ( strcmp( argv[i], "--" ) == 0 ) {
	  /* End option parsing here, advancing i past the -- */
	  ++i;
	  break;
	}
	else if ( strcmp( argv[i], "--enable-dirs" ) == 0 ) {
	  if ( opts->dirs == DEFAULT ) opts->dirs = ENABLED;
	  else {
	    fprintf( stderr,
		     "Only one of --{disable|enable}-dirs permitted\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--disable-dirs" ) == 0 ) {
	  if ( opts->dirs == DEFAULT ) opts->dirs = DISABLED;
	  else {
	    fprintf( stderr,
		     "Only one of --{disable|enable}-dirs permitted\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--enable-files" ) == 0 ) {
	  if ( opts->files == DEFAULT ) opts->files = ENABLED;
	  else {
	    fprintf( stderr,
		     "Only one of --{disable|enable}-files permitted\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--disable-files" ) == 0 ) {
	  if ( opts->files == DEFAULT ) opts->files = DISABLED;
	  else {
	    fprintf( stderr,
		     "Only one of --{disable|enable}-files permitted\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--enable-symlinks" ) == 0 ) {
	  if ( opts->symlinks == DEFAULT ) opts->symlinks = ENABLED;
	  else {
	    fprintf( stderr,
		     "Only one of --{disable|enable}-symlinks permitted\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--disable-symlinks" ) == 0 ) {
	  if ( opts->symlinks == DEFAULT ) opts->symlinks = DISABLED;
	  else {
	    fprintf( stderr,
		     "Only one of --{disable|enable}-symlinks permitted\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--set-compression" ) == 0 ) {
	  if ( i + 1 < argc ) {
	    status = set_compression_arg( opts, argv[i + 1] );
	    /* Consume the extra arg */
	    ++i;
	  }
	  else {
	    fprintf( stderr,
		     "The --set-compression option requires a parameter; try \'mpkg help create\'\n" );
	    status = CREATE_ERROR;
	  }	  
	}
	else if ( strcmp( argv[i], "--set-pkg-time" ) == 0 ) {
	  if ( i + 1 < argc ) {
	    status = set_pkg_time_arg( opts, argv[i + 1] );
	    /* Consume the extra arg */
	    ++i;
	  }
	  else {
	    fprintf( stderr,
		     "The --set-pkg-time option requires a parameter; try \'mpkg help create\'\n" );
	    status = CREATE_ERROR;
	  }
	}
	else if ( strcmp( argv[i], "--set-version" ) == 0 ) {
	  if ( i + 1 < argc ) {
	    status = set_version_arg( opts, argv[i + 1] );
	    /* Consume the extra arg */
	    ++i;
	  }
	  else {
	    fprintf( stderr,
		     "The --set-version option requires a parameter; try \'mpkg help create\'\n" );
	    status = CREATE_ERROR;
	  }
	}
	else {
	  fprintf( stderr,
		   "Unknown option for create %s; try \'mpkg help create\'\n",
		   argv[i] );
	  status = CREATE_ERROR;
	}
      }
      /* Not an option; break out and handle the filename args */
      else break;
    }

    if ( status == CREATE_SUCCESS ) {
      if ( i + 2 == argc || i + 3 == argc ) {
	temp = get_current_dir();
	if ( temp ) {
	  opts->input_directory = concatenate_paths( temp, argv[i] );
	  if ( i + 2 == argc) {
	    opts->emit->output_file = concatenate_paths( temp, argv[i + 1] );
	    opts->pkg_name =
	      guess_pkg_name_from_output_file( opts->emit->output_file );
	    if ( !(opts->pkg_name) ) {
	      opts->pkg_name =
		guess_pkg_name_from_input_directory( opts->input_directory );
	      if ( !(opts->pkg_name) ) {
		fprintf( stderr,
			 "Unable to guess package name, and it wasn't specified in the command.\n" );
		status = CREATE_ERROR;
	      }
	    }
	  }
	  else {
	    opts->emit->output_file = concatenate_paths( temp, argv[i + 2] );
	    opts->pkg_name = copy_string( argv[i + 1] );
	  }

	  if ( status == CREATE_SUCCESS ) {
	    if ( !( opts->input_directory && opts->emit->output_file &&
		    opts->pkg_name ) ) {
	      if ( opts->input_directory ) {
		free( opts->input_directory );
		opts->input_directory = NULL;
	      }
	      if ( opts->emit->output_file ) {
		free( opts->emit->output_file );
		opts->emit->output_file = NULL;
	      }
	      if ( opts->pkg_name ) {
		free( opts->pkg_name );
		opts->pkg_name = NULL;
	      }

	      fprintf( stderr,
		       "Unable to allocate memory trying to create package\n" );
	      status = CREATE_ERROR;
	    }
	  }
	  /*
	   * else CREATE_ERROR was already set, so we had trouble
	   * guessing pkg_name
	   */

	  if ( status == CREATE_SUCCESS ) {
	    guess_compression_and_version_from_filename( opts->emit );
	  }

	  free( temp );
	}
	else {
	  fprintf( stderr,
		   "Unable to allocate memory trying to create package\n" );
	  status = CREATE_ERROR;
	}
      }
      else {
	if ( i == argc ) {
	  fprintf( stderr,
		   "Directory and output filename are required; try \'mpkg help create\'\n" );
	  status = CREATE_ERROR;
	}
	else if ( i + 1 == argc ) {
	  fprintf( stderr,
		   "Output filename is required; try \'mpkg help create\'\n" );
	  status = CREATE_ERROR;
	}
	else {
	  fprintf( stderr,
		   "There are %d excess arguments; try \'mpkg help create\'\n",
		   argc - ( i + 3 ) );
	  status = CREATE_ERROR;
	}
      }
    }
  }
  else status = CREATE_ERROR;

  return status;
}

static void * dir_info_copier( void *v ) {
  create_dir_info *di, *rdi;

  rdi = NULL;
  if ( v ) {
    di = (create_dir_info *)v;
    rdi = malloc( sizeof( *rdi ) );
    if ( rdi ) {
      rdi->mode = di->mode;

      rdi->owner = copy_string( di->owner );
      rdi->group = copy_string( di->group );

      if ( !( ( rdi->owner || !(di->owner) ) &&
	      ( rdi->group || !(di->group) ) ) ) {

	fprintf( stderr,
		 "Unable to allocate memory in dir_info_copier()\n" );

	if ( rdi->owner ) free( rdi->owner );
	if ( rdi->group ) free( rdi->group );

	free( rdi );
	rdi = NULL;
      }
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory in dir_info_copier()\n" );
    }
  }

  return rdi;
}

static void dir_info_free( void *v ) {
  create_dir_info *di;

  if ( v ) {
    di = (create_dir_info *)v;

    if ( di->owner ) free( di->owner );
    if ( di->group ) free( di->group );

    free( di );
  }
}

static int emit_descr( create_opts * opts, pkg_descr *descr,
		       tar_writer *tw ) {
  int status, result, fd, len;
  char *tmpl; /* template for mkstemp() */
  const char *descr_name = "package-description";
  tar_file_info ti;

  status = CREATE_SUCCESS;
  if ( opts && descr && tw ) {
    /*
     * The temporary name is
     * get_temp()/package-description.getpid().XXXXXX
     */

    /*
     * 4 chars for /, . twice and the terminating \0, plus six for the
     * XXXXXX, leaves 22 for getpid(), which is more than sufficient.
     */
    len = strlen( get_temp() ) + strlen( descr_name ) + 32;
    tmpl = malloc( sizeof( *tmpl ) * len );
    if ( tmpl ) {
      snprintf( tmpl, len, "%s/%s.%d.XXXXXX",
		get_temp(), descr_name, getpid() );
      result = canonicalize_path( tmpl );
      if ( result == 0 ) {
	fd = mkstemp( tmpl );
	if ( fd >= 0 ) {
	  close( fd );
	  result = write_pkg_descr_to_file( descr, tmpl );
	  if ( result == 0 ) {
	    strncpy( ti.filename, descr_name,
		   TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	    strncpy( ti.target, "", TAR_TARGET_LEN + 1 );
	    ti.owner = 0;
	    ti.group = 0;
	    ti.mode = 0644;
	    ti.mtime = get_pkg_mtime( opts );
	    
	    /* Now emit it */
	    result = emit_file( tmpl, &ti, tw );
	    if ( result != EMIT_SUCCESS ) {
	      fprintf( stderr, "Couldn't emit %s to tarball\n", descr_name );
	      status = CREATE_ERROR;
	    }
	  }
	  else {
	    fprintf( stderr,
		     "Couldn't write package description to temp file %s\n",
		     tmpl );
	    status = CREATE_ERROR;
	  }

	  /* Clear out the temp file when we're done */
	  unlink( tmpl );
	}
	else {
	  fprintf( stderr,
		   "Couldn't mkstemp( %s ) emitting %s: %s\n",
		   tmpl, descr_name, strerror( errno ) );
	  status = CREATE_ERROR;
	}
      }
      else {
	fprintf( stderr,
		 "Couldn't canonicalize path emitting %s\n", descr_name );
	status = CREATE_ERROR;
      }
      free( tmpl );
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory emitting %s\n", descr_name );
      status = CREATE_ERROR;
    }
  }
  else status = CREATE_ERROR;

  return status;
}

static int emit_files( create_opts *opts, create_pkg_info *pkginfo,
		       tar_writer *tw ) {
  int status, result;
  rbtree_node *n;
  char *path, *tar_filename;
  void *info_v;
  create_file_info *fi;
  tar_file_info ti;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && tw ) {
    if ( pkginfo->files && pkginfo->files_count > 0 ) {
      n = NULL;
      do {
	fi = NULL;
	path = rbtree_enum( pkginfo->files, n, &info_v, &n );
	if ( path ) {
	  if ( info_v ) {
	    fi = (create_file_info *)info_v;

	    /* Strip off leading slashes */
	    tar_filename = path;
	    while ( *tar_filename == '/' ) ++tar_filename;
	    /* Now emit the file */
	    ti.type = TAR_FILE;
	    strncpy( ti.filename, tar_filename,
		     TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	    strncpy( ti.target, "", TAR_TARGET_LEN + 1 );
	    ti.owner = 0;
	    ti.group = 0;
	    ti.mode = 0644;
	    ti.mtime = get_pkg_mtime( opts );
	    result = emit_file( fi->src_path, &ti, tw );
	    if ( result != EMIT_SUCCESS ) {
	      fprintf( stderr, "Error emitting file %s\n", fi->src_path );
	      status = CREATE_ERROR;
	    }
	  }
	  else {
	    fprintf( stderr,
		     "Internal error while emitting package files (path %s)\n",
		     path );
	    status = CREATE_ERROR;
	  }
	}
	/* else nothing to do */
      } while ( n && status == CREATE_SUCCESS );
    }
    /* else nothing to do */
  }
  else status = CREATE_ERROR;

  return status;
}

static void * file_info_copier( void *v ) {
  create_file_info *fi, *rfi;

  rfi = NULL;
  if ( v ) {
    fi = (create_file_info *)v;
    rfi = malloc( sizeof( *rfi ) );
    if ( rfi ) {
      rfi->mode = fi->mode;
      memcpy( rfi->hash, fi->hash, sizeof( rfi->hash ) );

      rfi->src_path = copy_string( fi->src_path );
      rfi->owner = copy_string( fi->owner );
      rfi->group = copy_string( fi->group );

      if ( !( ( rfi->src_path || !(fi->src_path) ) && 
	      ( rfi->owner || !(fi->owner) ) &&
	      ( rfi->group || !(fi->group) ) ) ) {
	fprintf( stderr,
		 "Unable to allocate memory in file_info_copier()\n" );

	if ( rfi->src_path ) free( rfi->src_path );
	if ( rfi->owner ) free( rfi->owner );
	if ( rfi->group ) free( rfi->group );

	free( rfi );
	rfi = NULL;
      }
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory in file_info_copier()\n" );
    }
  }

  return rfi;
}

static void file_info_free( void *v ) {
  create_file_info *fi;

  if ( v ) {
    fi = (create_file_info *)v;

    if ( fi->src_path ) free( fi->src_path );
    if ( fi->owner ) free( fi->owner );
    if ( fi->group ) free( fi->group );

    free( fi );
  }
}

static void free_create_opts( create_opts *opts ) {
  if ( opts ) {
    if ( opts->input_directory ) free( opts->input_directory );
    if ( opts->pkg_name ) free( opts->pkg_name );
    if ( opts->emit ) free_emit_opts( opts->emit );
    free( opts );
  }
}

static void free_pkginfo( create_pkg_info *pkginfo ) {
  if ( pkginfo ) {
    if ( pkginfo->dirs ) rbtree_free( pkginfo->dirs );
    if ( pkginfo->files ) rbtree_free( pkginfo->files );
    if ( pkginfo->symlinks ) rbtree_free( pkginfo->symlinks );
    free( pkginfo );
  }
}

static int get_dirs_enabled( create_opts *opts ) {
  int result;

  if ( opts && opts->dirs == ENABLED ) result = 1;
  /* default off */
  else result = 0;

  return result;
}

static int get_files_enabled( create_opts *opts ) {
  int result;

  if ( opts && opts->files == DISABLED ) result = 0;
  /* default on */
  else result = 1;

  return result;
}

static time_t get_pkg_mtime( create_opts *opts ) {
  time_t result;

  if ( opts && opts->emit ) result = opts->emit->pkg_mtime;
  else result = 0;

  return result;
}

static char * get_pkg_name( create_opts *opts ) {
  char *result;

  if ( opts ) result = opts->pkg_name;
  else result = NULL;

  return result;
}

static int get_symlinks_enabled( create_opts *opts ) {
  int result;

  if ( opts && opts->symlinks == DISABLED ) result = 0;
  /* default on */
  else result = 1;

  return result;
}

static char * guess_pkg_name_from_input_directory( const char *indir ) {
  char *lcmp, *result;

  result = NULL;
  if ( indir ) {
    lcmp = get_last_component( indir );
    if ( lcmp ) result = lcmp;
  }

  return result;
}

static char * guess_pkg_name_from_output_file( const char *outfile ) {
  char *lcmp, *result;
  int n, len;

  result = NULL;
  if ( outfile ) {
    lcmp = get_last_component( outfile );
    if ( lcmp ) {
      /* Strip off any extension and guess the part before the first . */
      len = strlen( lcmp );
      n = 0;
      while ( lcmp[n] != '.' && n < len ) ++n;
      if ( n > 0 ) {
	result = malloc( sizeof( *result ) * ( n + 1 ) );
	if ( result ) {
	  memcpy( result, lcmp, sizeof( *result ) * n );
	  result[n + 1] = '\0';
	}
	/* else fail due to malloc() failure */
      }
      /* else it starts with ., give up */

      free( lcmp );
    }
  }

  return result;
}

static int scan_directory_tree_internal( create_opts *opts,
					 create_pkg_info *pkginfo,
					 const char *path_prefix,
					 const char *prefix ) {
  int status, result;
  struct passwd *pwd;
  struct group *grp;
  struct stat st;
  create_dir_info di;
  create_file_info fi;
  create_symlink_info si;
  DIR *cwd;
  struct dirent *dentry;
  char *next_path, *next_prefix;
  int next_prefix_len, prefix_len;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && path_prefix && prefix ) {
    cwd = opendir( path_prefix );
    if ( cwd ) {
      do {
	/* Iterate over directory entries */
	errno = 0;
	dentry = readdir( cwd );
	if ( dentry ) {
	  if ( !( strcmp( dentry->d_name, "." ) == 0 ||
		  strcmp( dentry->d_name, ".." ) == 0 ) ) {
	    /* It's a real one (not . or ..) */

	    /* Construct the full path */
	    next_path = concatenate_paths( path_prefix, dentry->d_name );
	    if ( next_path ) {
	      /* Stat it so we can check the type */
	      result = lstat( next_path, &st );
	      if ( result == 0 ) {
		/* lstat() was okay, test st_mode */
		if ( S_ISREG( st.st_mode ) ) {
		  /* Regular file */

		  if ( get_files_enabled( opts ) && pkginfo->files ) {
		    next_prefix = concatenate_paths( prefix, dentry->d_name );
		    if ( next_prefix ) {
		      /* Mask off type bits from mode */
		      fi.mode = st.st_mode & 0xfff;
		      fi.src_path = next_path;

		      /* Reverse-lookup owner/group */
		      pwd = getpwuid( st.st_uid );
		      if ( pwd ) fi.owner = pwd->pw_name;
		      else fi.owner = "root";

		      grp = getgrgid( st.st_gid );
		      if ( grp ) fi.group = grp->gr_name;
		      else fi.group = "root";

		      /* Get the file's MD5 */
		      result = get_file_hash( next_path, fi.hash );
		      if ( result != 0 ) {
			fprintf( stderr, "Unable to get MD5 for file %s\n",
				 next_path );
			status = CREATE_ERROR;
		      }

		      if ( status == CREATE_SUCCESS ) {
			result =
			  rbtree_insert( pkginfo->files, next_prefix, &fi );
			if ( result == RBTREE_SUCCESS ) {
			  ++(pkginfo->files_count);
			}
			else {
			  fprintf( stderr,
				   "Unable to allocate memory for file %s\n",
				   next_path );
			  status = CREATE_ERROR;
			}
		      }

		      free( next_prefix );
		    }
		    else {
		      fprintf( stderr,
			       "Unable to allocate memory for file %s\n",
			       next_path );
		      status = CREATE_ERROR;
		    }
		  }
		}
		else if ( S_ISDIR( st.st_mode ) ) {
		  /*
		   * Directory, so we need to recurse into it whether
		   * or not dirs_enabled
		   */

		  prefix_len = strlen( prefix );
		  next_prefix_len =
		    prefix_len + strlen( dentry->d_name ) + 2;
		  next_prefix =
		    malloc( sizeof( *next_prefix ) * ( next_prefix_len + 1 ) );
		  if ( next_prefix ) {
		    /*
		     * Construct the prefix to recurse into the new
		     * directory with (and to use for the
		     * package-description entry name if dirs_enabled
		     */

		    if ( prefix_len > 0 ) {
		      if ( prefix[prefix_len - 1] == '/' ) {
			snprintf( next_prefix, next_prefix_len,
				  "%s%s/", prefix, dentry->d_name );
		      }
		      else {
			snprintf( next_prefix, next_prefix_len,
				  "%s/%s/", prefix, dentry->d_name );
		      }
		    }
		    else {
		      snprintf( next_prefix, next_prefix_len,
				"/%s/", dentry->d_name );
		    }

		    if ( get_dirs_enabled( opts ) && pkginfo->dirs ) {
		      /* Mask off type bits from mode */
		      di.mode = st.st_mode & 0xfff;

		      /* Reverse-lookup owner/group */
		      pwd = getpwuid( st.st_uid );
		      if ( pwd ) di.owner = pwd->pw_name;
		      else di.owner = "root";

		      grp = getgrgid( st.st_gid );
		      if ( grp ) di.group = grp->gr_name;
		      else di.group = "root";

		      result =
			rbtree_insert( pkginfo->dirs, next_prefix, &di );
		      if ( result == RBTREE_SUCCESS ) {
			++(pkginfo->dirs_count);
		      }
		      else {
			fprintf( stderr,
				 "Unable to allocate memory for directory %s\n",
				 next_path );
			status = CREATE_ERROR;
		      }
		    }

		    if ( status == CREATE_SUCCESS ) {
		      /* Handle the recursion */

		      result =
			scan_directory_tree_internal( opts, pkginfo,
						      next_path,
						      next_prefix );
		      if ( result != CREATE_SUCCESS ) status = result;
		    }

		    free( next_prefix );
		  }
		  else {
		    fprintf( stderr,
			     "Unable to allocate memory for directory %s\n",
			     next_path );
		    status = CREATE_ERROR;
		  }
		}
		else if ( S_ISLNK( st.st_mode ) ) {
		  /* Symlink */

		  if ( get_symlinks_enabled( opts ) && pkginfo->symlinks ) {
		    next_prefix = concatenate_paths( prefix, dentry->d_name );
		    if ( next_prefix ) {
		      /* Reverse-lookup owner/group */
		      pwd = getpwuid( st.st_uid );
		      if ( pwd ) si.owner = pwd->pw_name;
		      else si.owner = "root";

		      grp = getgrgid( st.st_gid );
		      if ( grp ) si.group = grp->gr_name;
		      else si.group = "root";

		      /* Get symlink target */
		      result = read_symlink_target( next_path, &(si.target) );
		      if ( result == READ_SYMLINK_SUCCESS ) {
			result =
			  rbtree_insert( pkginfo->symlinks, next_prefix, &si );
			if ( result == RBTREE_SUCCESS ) {
			  ++(pkginfo->symlinks_count);
			}
			else {
			  fprintf( stderr,
				   "Unable to allocate memory for symlink %s\n",
				   next_path );
			  status = CREATE_ERROR;
			}

			free( si.target );
			si.target = NULL;
		      }
		      else {
			fprintf( stderr,
				 "Unable to read target of symlink %s\n",
				 next_path );
			status = CREATE_ERROR;
		      }

		      free( next_prefix );
		    }
		    else {
		      fprintf( stderr,
			       "Unable to allocate memory for symlink %s\n",
			       next_path );
		      status = CREATE_ERROR;
		    }
		  }
		}
		else {
		  /* Unknown type */

		  fprintf( stderr,
			   "Warning: %s is not a regular file, directory or symlink\n",
			   next_path );
		}
	      }
	      else {
		fprintf( stderr, "Unable to stat %s: %s\n",
			 next_path, strerror( errno ) );
		status = CREATE_ERROR;
	      }
	      free( next_path );
	    }
	    else{
	      fprintf( stderr, "Unable to allocate memory for %s in %s\n",
		       dentry->d_name, path_prefix );
	      status = CREATE_ERROR;
	    }
	  }
	  /* else skip . and .. */
	}
	else {
	  if ( errno != 0 ) {
	    fprintf( stderr,
		     "Error reading directory entries from %s: %s\n",
		     path_prefix, strerror( errno ) );
	    status = CREATE_ERROR;
	  }
	  /* else out of entries */
	}
      } while ( dentry != NULL && status == CREATE_SUCCESS );
      /* done or error here */

      closedir( cwd );
    }
    else {
      fprintf( stderr,
	       "Unable to open directory %s: %s\n",
	       path_prefix, strerror( errno ) );
      status = CREATE_ERROR;
    }
  }
  else status = CREATE_ERROR;

  return status;
}


static int scan_directory_tree( create_opts *opts,
				create_pkg_info *pkginfo ) {
  int status, result;
  struct stat st;
  struct passwd *pwd;
  struct group *grp;
  create_dir_info di;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo ) {
    if ( opts->input_directory ) {
      if ( get_dirs_enabled( opts ) && pkginfo->dirs ) {
	/* Handle the top-level directory */

	/*
	 * Use stat(), not lstat(), so we can handle a symlink to the
	 * package tree
	 */
	result = stat( opts->input_directory, &st );
	if ( result == 0 ) {
	  if ( S_ISDIR( st.st_mode ) ) {
	    di.mode = st.st_mode & 0xfff;
	    pwd = getpwuid( st.st_uid );
	    if ( pwd ) di.owner = pwd->pw_name;
	    else di.owner = "root";
	    grp = getgrgid( st.st_gid );
	    if ( grp ) di.group = grp->gr_name;
	    else di.group = "root";

	    result = rbtree_insert( pkginfo->dirs, "/", &di );
	    if ( result == RBTREE_SUCCESS )
	      ++(pkginfo->dirs_count);
	    else {
	      fprintf( stderr, "Unable to allocate memory for %s\n",
		       opts->input_directory );
	      status = CREATE_ERROR;
	    }
	  }
	  else {
	    fprintf( stderr, "%s is not a directory\n",
		     opts->input_directory );
	    status = CREATE_ERROR;
	  }
	}
	else {
	  fprintf( stderr, "Unable to stat() %s: %s\n",
		   opts->input_directory, strerror( errno ) );
	  status = CREATE_ERROR;
	}
      }

      if ( status != CREATE_ERROR ) {
	result = scan_directory_tree_internal( opts, pkginfo,
					       opts->input_directory, "/" );
	if ( result != CREATE_SUCCESS ) status = result;
      }
    }
    else status = CREATE_ERROR;
  }
  else status = CREATE_ERROR;

  return status;
}

static int set_compression_arg( create_opts *opts, char *arg ) {
  int result;

  result = CREATE_SUCCESS;
  if ( opts && opts->emit && arg ) {
    if ( opts->emit->compression == DEFAULT_COMPRESSION ) {
      if ( strcmp( arg, "none" ) == 0 ) opts->emit->compression = NONE;
#ifdef COMPRESSION_GZIP
      else if ( strcmp( arg, "gzip" ) == 0 ) opts->emit->compression = GZIP;
#endif
#ifdef COMPRESSION_BZIP2
      else if ( strcmp( arg, "bzip2" ) == 0 ) opts->emit->compression = BZIP2;
#endif
      else {
	fprintf( stderr,
		 "Unknown or unsupported compression type %s\n",
		 arg );
	result = CREATE_ERROR;
      }
    }
    else {
      fprintf( stderr,
	       "Only one --set-compression option is permitted.\n" );
      result = CREATE_ERROR;
    }
  }
  else result = CREATE_ERROR;

  return result;
}

static void set_default_opts( create_opts *opts ) {
  opts->input_directory = NULL;
  opts->pkg_name = NULL;
  opts->emit->output_file = NULL;
  opts->emit->pkg_mtime = time( NULL );
  opts->emit->compression = DEFAULT_COMPRESSION;
  opts->emit->version = DEFAULT_VERSION;
  opts->files = DEFAULT;
  opts->dirs = DEFAULT;
  opts->symlinks = DEFAULT;
}

static int set_pkg_time_arg( create_opts *opts, char *arg ) {
  int result;
  long t;

  result = CREATE_SUCCESS;
  if ( opts && arg ) {
    if ( sscanf( arg, "%ld", &t ) == 1 ) {
      opts->emit->pkg_mtime = (time_t)t;
    }
    else {
      fprintf( stderr, "Unable to parse time \"%s\".\n", arg );
      result = CREATE_ERROR;
    }
  }
  else result = CREATE_ERROR;

  return result;
}

static int set_version_arg( create_opts *opts, char *arg ) {
  int result;

  result = CREATE_SUCCESS;
  if ( opts && arg ) {
    if ( opts->emit->version == DEFAULT_VERSION ) {
#ifdef PKGFMT_V1
      if ( strcmp( arg, "v1" ) == 0 ) opts->emit->version = V1;
# ifdef PKGFMT_V2
      else if ( strcmp( arg, "v2" ) == 0 ) opts->emit->version = V2;
# endif
#else
# ifdef PKGFMT_V2
      if ( strcmp( arg, "v2" ) == 0 ) opts->emit->version = V2;
# else
#  error At least one of PKGFMT_V1 or PKGFMT_V2 must be defined
# endif
#endif
      else {
	fprintf( stderr,
		 "Unknown or unsupported version %s\n",
		 arg );
	result = CREATE_ERROR;
      }
    }
    else {
      fprintf( stderr,
	       "Only one --set-version option is permitted.\n" );
      result = CREATE_ERROR;
    }
  }
  else result = CREATE_ERROR;

  return result;
}

static void * symlink_info_copier( void *v ) {
  create_symlink_info *si, *rsi;

  rsi = NULL;
  if ( v ) {
    si = (create_symlink_info *)v;
    rsi = malloc( sizeof( *rsi ) );
    if ( rsi ) {
      rsi->owner  = copy_string( si->owner  );
      rsi->group  = copy_string( si->group  );
      rsi->target = copy_string( si->target );

      if ( !( ( rsi->owner  || !(si->owner)  ) &&
	      ( rsi->group  || !(si->group)  ) &&
	      ( rsi->target || !(si->target) ) ) ) {

	fprintf( stderr,
		 "Unable to allocate memory in symlink_info_copier()\n" );

	if ( rsi->owner  ) free( rsi->owner  );
	if ( rsi->group  ) free( rsi->group  );
	if ( rsi->target ) free( rsi->target );

	free( rsi );
	rsi = NULL;
      }
    }
    else {
      fprintf( stderr,
	       "Unable to allocate memory in symlink_info_copier()\n" );
    }
  }

  return rsi;
}

static void symlink_info_free( void *v ) {
  create_symlink_info *si;

  if ( v ) {
    si = (create_symlink_info *)v;

    if ( si->owner  ) free( si->owner  );
    if ( si->group  ) free( si->group  );
    if ( si->target ) free( si->target );

    free( si );
  }
}
