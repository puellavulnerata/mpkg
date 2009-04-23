#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <pkg.h>

#define CREATE_SUCCESS 0
#define CREATE_ERROR -1

typedef enum {
  ENABLED,
  DISABLED,
  DEFAULT
} create_boolean_opt;

typedef enum {
  NONE,
#ifdef COMPRESSION_GZIP
  GZIP,
#endif
#ifdef COMPRESSION_BZIP2
  BZIP2,
#endif
  DEFAULT_COMPRESSION
} create_compression_opt;

typedef enum {
#ifdef PKGFMT_V1
    V1,
#endif
#ifdef PKGFMT_V2
    V2,
#endif
    DEFAULT_VERSION
} create_version_opt;

typedef struct {
  char *input_directory, *output_file;
  create_compression_opt compression;
  create_version_opt version;
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

static create_pkg_info * alloc_pkginfo( create_opts * );
static void create_build_pkg( create_opts * );
static int create_parse_options( create_opts *, int, char ** );
static void * dir_info_copier( void * );
static void dir_info_free( void * );
static void * file_info_copier( void * );
static void file_info_free( void * );
static void free_create_opts( create_opts * );
static void free_pkginfo( create_pkg_info * );
static create_compression_opt get_compression( create_opts * );
static int get_dirs_enabled( create_opts * );
static int get_files_enabled( create_opts * );
static int get_symlinks_enabled( create_opts * );
static create_version_opt get_version( create_opts * );
static int scan_directory_tree_internal( create_opts *, create_pkg_info *,
					 const char *, const char * );
static int scan_directory_tree( create_opts *, create_pkg_info * );
static int set_compression_arg( create_opts *, char * );
static void set_default_opts( create_opts * );
static int set_version_arg( create_opts *, char * );
static void * symlink_info_copier( void * );
static void symlink_info_free( void * );

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
   
      if ( get_dirs_enabled( opts ) && !error ) {
	temp->dirs = rbtree_alloc( rbtree_string_comparator,
				   rbtree_string_copier,
				   rbtree_string_free,
				   dir_info_copier,
				   dir_info_free );
	if ( !(temp->dirs) ) error = 1;
      }

      if ( get_files_enabled( opts ) && !error ) {
	temp->files = rbtree_alloc( rbtree_string_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    file_info_copier,
				    file_info_free );
	if ( !(temp->files) ) error = 1;
      }

      if ( get_symlinks_enabled( opts ) && !error ) {
	temp->symlinks = rbtree_alloc( rbtree_string_comparator,
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

static void create_build_pkg( create_opts *opts ) {
  create_pkg_info *pkginfo;
  int status;

  if ( opts ) {
    pkginfo = alloc_pkginfo( opts );
    if ( pkginfo ) {
      status = scan_directory_tree( opts, pkginfo );
      if ( status == CREATE_SUCCESS ) {
	printf( "Saw %d directories, %d files and %d symlinks\n",
		pkginfo->dirs_count, pkginfo->files_count,
		pkginfo->symlinks_count );

	/* TODO real package builder */
      }
      else {
	fprintf( stderr,
		 "Failed to scan directory tree %s for package build\n",
		 opts->input_directory );
      }
      
      free_pkginfo( pkginfo );
    }
    else {
      fprintf( stderr, "Unable to allocate memory\n" );
    }
  }
}

void create_main( int argc, char **argv ) {
  int result;
  create_opts *opts;

  opts = malloc( sizeof( *opts ) );
  if ( opts ) {
    set_default_opts( opts );
    result = create_parse_options( opts, argc, argv );
    if ( result == CREATE_SUCCESS ) create_build_pkg( opts );
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
	  if ( i + 1 < argc )
	    status = set_compression_arg( opts, argv[i + 1] );
	  else {
	    fprintf( stderr,
		     "The --set-compression option requires a parameter; try \'mpkg help create\'\n" );
	    status = CREATE_ERROR;
	  }	  
	}
	else if ( strcmp( argv[i], "--set-version" ) == 0 ) {
	  if ( i + 1 < argc )
	    status = set_version_arg( opts, argv[i + 1] );
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
      if ( i + 2 == argc ) {
	temp = get_current_dir();
	if ( temp ) {
	  opts->input_directory = concatenate_paths( temp, argv[i] );
	  opts->output_file = concatenate_paths( temp, argv[i + 1] );
	  if ( !( opts->input_directory && opts->output_file ) ) {
	    if ( opts->input_directory ) {
	      free( opts->input_directory );
	      opts->input_directory = NULL;
	    }
	    if ( opts->output_file ) {
	      free( opts->output_file );
	      opts->output_file = NULL;
	    }

	    fprintf( stderr,
		     "Unable to allocate memory trying to create package\n" );
	    status = CREATE_ERROR;
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
		   argc - ( i + 2 ) );
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
    if ( opts->output_file ) free( opts->output_file );
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

static create_compression_opt get_compression( create_opts *opts ) {
  create_compression_opt result;

#ifdef COMPRESSION_BZIP2
  /* Default to bzip2 if available */
  result = BZIP2;
#else
# ifdef COMPRESSION_GZIP
  /* Use gzip if we have that and no bzip2 */
  result = GZIP;
# else
  /* No compression available */
  result = NONE;
# endif
#endif

  if ( opts && opts->compression != DEFAULT_COMPRESSION )
    result = opts->compression;

  return result;
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

static int get_symlinks_enabled( create_opts *opts ) {
  int result;

  if ( opts && opts->symlinks == DISABLED ) result = 0;
  /* default on */
  else result = 1;

  return result;
}

static create_version_opt get_version( create_opts *opts ) {
  create_version_opt result;

#ifdef PKGFMT_V1
# ifdef PKGFMT_V2
  /* default to V2 if we have both */
  result = V2;
  /* The contents of the option only matter if we have V1 and V2 */
  if ( opts && opts->version == V1 ) result = V1;
# else
  /* we have V1 but not V2 */
  result = V1;
# endif
#else
# ifdef PKGFMT_V2
  /* we have V2 but not V1 */
  result = V2;
# else
#  error At least one of PKGFMT_V1 or PKGFMT_V2 must be defined
# endif
#endif

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
  if ( opts && arg ) {
    if ( opts->compression == DEFAULT_COMPRESSION ) {
      if ( strcmp( arg, "none" ) == 0 ) opts->compression = NONE;
#ifdef COMPRESSION_GZIP
      else if ( strcmp( arg, "gzip" ) == 0 ) opts->compression = GZIP;
#endif
#ifdef COMPRESSION_BZIP2
      else if ( strcmp( arg, "bzip2" ) == 0 ) opts->compression = BZIP2;
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
  opts->output_file = NULL;
  opts->compression = DEFAULT_COMPRESSION;
  opts->version = DEFAULT_VERSION;
  opts->files = DEFAULT;
  opts->dirs = DEFAULT;
  opts->symlinks = DEFAULT;
}

static int set_version_arg( create_opts *opts, char *arg ) {
  int result;

  result = CREATE_SUCCESS;
  if ( opts && arg ) {
    if ( opts->version == DEFAULT_VERSION ) {
#ifdef PKGFMT_V1
      if ( strcmp( arg, "v1" ) == 0 ) opts->compression = V1;
# ifdef PKGFMT_V2
      else if ( strcmp( arg, "v2" ) == 0 ) opts->compression = V2;
# endif
#else
# ifdef PKGFMT_V2
      if ( strcmp( arg, "v2" ) == 0 ) opts->compression = V2;
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
