#include <stdio.h>
#include <stdlib.h>

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

static void create_build_pkg( create_opts * );
static int create_parse_options( create_opts *, int, char ** );
static void free_create_opts( create_opts * );
static int set_compression_arg( create_opts *, char * );
static void set_default_opts( create_opts * );
static int set_version_arg( create_opts *, char * );

static void create_build_pkg( create_opts *opts ) {
  /* TODO */
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

static void free_create_opts( create_opts *opts ) {
  if ( opts ) {
    if ( opts->input_directory ) free( opts->input_directory );
    if ( opts->output_file ) free( opts->output_file );
    free( opts );
  }
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
