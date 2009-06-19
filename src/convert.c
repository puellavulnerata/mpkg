#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <pkg.h>

#define CONVERT_SUCCESS 0
#define CONVERT_ERROR -1

typedef struct {
  char *input_file;
  emit_opts *emit;
} convert_opts;

static convert_opts * alloc_convert_opts( void );
static int convert_pkg( const convert_opts * );
static void free_convert_opts( convert_opts * );
static int set_compression_arg( convert_opts *, const char * );
static int set_input_file( convert_opts *, const char * );
static int set_output_file( convert_opts *, const char * );
static int set_version_arg( convert_opts *, const char * );

static convert_opts * alloc_convert_opts( void ) {
  convert_opts *opts;

  opts = malloc( sizeof( *opts ) );
  if ( opts ) {
    opts->input_file = NULL;
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

void convert_main( int argc, char **argv ) {
  int status, result, i;
  convert_opts *opts;

  opts = NULL;
  status = CONVERT_SUCCESS;
  if ( argc >= 2 ) {
    opts = alloc_convert_opts();
    if ( opts ) {
      i = 0;
      while ( i < argc && status == CONVERT_SUCCESS ) {
	/* Break out on non-option */
	if ( *(argv[i]) != '-' ) break;
	else {
	  if ( strcmp( argv[i], "--" ) == 0 ) {
	    /* Break out of option parsing on next arg */
	    ++i;
	    break;
	  }
	  else if ( strcmp( argv[i], "--set-version" ) == 0 ) {
	    if ( i + 1 < argc ) {
	      result = set_version_arg( opts, argv[i+1] );
	      if ( result != CONVERT_SUCCESS ) status = result;
	      /* Consume this and the extra arg */
	      i += 2;
	    }
	    else {
	      fprintf( stderr, "--set-version requires an argument\n" );
	      status = CONVERT_ERROR;
	    }
	  }
	  else if ( strcmp( argv[i], "--set-compression" ) == 0 ) {
	    if ( i + 1 < argc ) {
	      result = set_compression_arg( opts, argv[i+1] );
	      if ( result != CONVERT_SUCCESS ) status = result;
	      /* Consume this and the extra arg */
	      i += 2;
	    }
	    else {
	      fprintf( stderr, "--set-compression requires an argument\n" );
	      status = CONVERT_ERROR;
	    }
	  }
	  else {
	    fprintf( stderr, "Unknown option %s\n", argv[i] );
	    status = CONVERT_ERROR;
	  }
	}
      }
 
      if ( status == CONVERT_SUCCESS ) {
	if ( i + 2 == argc ) {
	  result = set_input_file( opts, argv[i] );
	  if ( result == CONVERT_SUCCESS ) {
	    result = set_output_file( opts, argv[i+1] );
	    if ( result != CONVERT_SUCCESS ) status = result;
	  }
	  else status = result;
	}
	else if ( i + 2 < argc ) {
	  fprintf( stderr, "Syntax error: %d extra arguments\n",
		   argc - ( i + 2 ) );
	  status = CONVERT_ERROR;
	}
	else {
	  /* i + 2 > argc, not enough args */
	  if ( i + 1 == argc ) {
	    fprintf( stderr, "Syntax error: need output filename\n" );
	  }
	  else {
	    fprintf( stderr,
		     "Syntax error: need input and output filenames\n" );
	  }
	  status = CONVERT_ERROR;
	}
      }
    }
    else status = CONVERT_ERROR;
  }
  else status = CONVERT_ERROR;

  if ( opts && status == CONVERT_SUCCESS ) {
    status = convert_pkg( opts );
  }

  if ( opts ) {
    free_convert_opts( opts );
    opts = NULL;
  }
}

static void free_convert_opts( convert_opts *opts ) {
  if ( opts ) {
    if ( opts->input_file ) {
      free( opts->input_file );
      opts->input_file = NULL;
    }

    if ( opts->emit ) {
      if ( opts->emit->output_file ) {
	free( opts->emit->output_file );
	opts->emit->output_file = NULL;
      }

      free( opts->emit );
      opts->emit = NULL;
    }

    free( opts );
    opts = NULL;
  }
}

static int set_compression_arg( convert_opts *opts, const char *arg ) {
  int result;

  result = CONVERT_SUCCESS;
  if ( opts && opts->emit && arg ) {
    if ( opts->emit->compression == DEFAULT_COMPRESSION ) {
      if ( strcmp( arg, "none" ) == 0 ) opts->emit->compression = NONE;
#ifdef COMPRESSION_GZIP
      else if ( strcmp( arg, "gzip" ) == 0 ) opts->emit->compression = GZIP;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
      else if ( strcmp( arg, "bzip2" ) == 0 ) opts->emit->compression = BZIP2;
#endif /* COMPRESSION_BZIP2 */
      else {
	fprintf( stderr,
		 "Unknown or unsupported compression type %s\n",
		 arg );
	result = CONVERT_ERROR;
      }
    }
    else {
      fprintf( stderr,
	       "Only one --set-compression option is permitted.\n" );
      result = CONVERT_ERROR;
    }
  }
  else result = CONVERT_ERROR;

  return result;
}

static int set_input_file( convert_opts *opts, const char *arg ) {
  int result;
  char *temp;

  result = CONVERT_SUCCESS;
  if ( opts && arg ) {
    temp = copy_string( arg );
    if ( temp ) {
      if ( opts->input_file ) free( opts->input_file );
      opts->input_file = temp;
    }
    else {
      fprintf( stderr, "Unable to allocate memory while parsing args\n" );
      result = CONVERT_ERROR;
    }
  }
  else result = CONVERT_ERROR;

  return result;
}

static int set_output_file( convert_opts *opts, const char *arg ) {
  int result;
  char *temp;

  result = CONVERT_SUCCESS;
  if ( opts && opts->emit && arg ) {
    temp = copy_string( arg );
    if ( temp ) {
      if ( opts->emit->output_file ) free( opts->emit->output_file );
      opts->emit->output_file = temp;
    }
    else {
      fprintf( stderr, "Unable to allocate memory while parsing args\n" );
      result = CONVERT_ERROR;
    }
  }
  else result = CONVERT_ERROR;

  return result;
}

static int set_version_arg( convert_opts *opts, const char *arg ) {
  int result;

  result = CONVERT_SUCCESS;
  if ( opts && opts->emit && arg ) {
    if ( opts->emit->version == DEFAULT_VERSION ) {
#ifdef PKGFMT_V1
      if ( strcmp( arg, "v1" ) == 0 ) opts->emit->version = V1;
# ifdef PKGFMT_V2
      else if ( strcmp( arg, "v2" ) == 0 ) opts->emit->version = V2;
# endif /* PKGFMT_V2 */
#else /* PKGFMT_V1 */
# ifdef PKGFMT_V2
      if ( strcmp( arg, "v2" ) == 0 ) opts->emit->version = V2;
# else /* PKGMFT_V2 */
#  error At least one of PKGFMT_V1 or PKGFMT_V2 must be defined
# endif /* PKGFMT_V2 */
#endif /* PKGFMT_V1 */
      else {
	fprintf( stderr,
		 "Unknown or unsupported version %s\n",
		 arg );
	result = CONVERT_ERROR;
      }
    }
    else {
      fprintf( stderr,
	       "Only one --set-version option is permitted.\n" );
      result = CONVERT_ERROR;
    }
  }
  else result = CONVERT_ERROR;

  return result;
}
