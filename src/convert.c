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
static int convert_descr( pkg_handle *, emit_pkg_streams * );
static int convert_files( pkg_handle *, emit_pkg_streams * );
static int convert_pkg( convert_opts * );
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

static int convert_descr( pkg_handle *ipkg, emit_pkg_streams *opkg ) {
  int status, result;
  const char *descr_name = "package-description";
  tar_file_info ti;

  status = CONVERT_SUCCESS;
  if ( ipkg && ipkg->descr && ipkg->descr_file && opkg && opkg->pkg_tw ) {
    ti.type = TAR_FILE;
    strncpy( ti.filename, descr_name,
	     TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
    strncpy( ti.target, "", TAR_TARGET_LEN + 1 );
    ti.owner = 0;
    ti.group = 0;
    ti.mode = 0644;
    ti.mtime = ipkg->descr->hdr.pkg_time;
    result = emit_file( ipkg->descr_file, &ti, opkg->pkg_tw );
    if ( result != EMIT_SUCCESS ) {
      fprintf( stderr, "Couldn't emit %s to tarball\n", descr_name );
      status = CONVERT_ERROR;
    }
  }
  else status = CONVERT_ERROR;

  return status;
}

static int convert_files( pkg_handle *ipkg, emit_pkg_streams *opkg ) {
  int status, result, i;
  pkg_descr_entry *e;
  tar_file_info ti;
  char *src_filename;
  char *tar_filename;
  char *temp;

  status = CONVERT_SUCCESS;
  if ( ipkg && ipkg->descr && opkg && opkg->emit_tw ) {
    /* Initialize fixed fields of tar_file_info struct */
    ti.type = TAR_FILE;
    strncpy( ti.target, "", TAR_TARGET_LEN + 1 );
    ti.owner = 0;
    ti.group = 0;
    ti.mode = 0644;
    ti.mtime = ipkg->descr->hdr.pkg_time;

    /* Iterate over the input package entries */
    for ( i = 0; i < ipkg->descr->num_entries; ++i ) {
      e = &(ipkg->descr->entries[i]);
      if ( e->type == ENTRY_FILE ) {
	src_filename = NULL;
	temp = concatenate_paths( ipkg->unpacked_dir, "package-content" );
	if ( temp ) {
	  src_filename = concatenate_paths( temp, e->filename );
	  free( temp );
	  temp = NULL;
	}

	if ( src_filename ) {
	  tar_filename = e->filename;

	  /* Strip off leading slashes */
	  while ( *tar_filename == '/' ) ++tar_filename;
	  strncpy( ti.filename, tar_filename,
		   TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );

	  /* Emit it */
	  result = emit_file( src_filename, &ti, opkg->emit_tw );
	  if ( result != EMIT_SUCCESS ) {
	    fprintf( stderr, "Error emitting file %s\n", src_filename );
	    status = CONVERT_ERROR;
	  }

	  free( src_filename );
	}
	else {
	  fprintf( stderr, "Unable to allocate memory trying to emit %s\n",
		   e->filename );
	  status = CONVERT_ERROR;
	}
      }
      /* else nothing to emit for non-files */

      if ( status != CONVERT_SUCCESS ) break;
    }
  }
  else status = CONVERT_ERROR;

  return status;
}

void convert_help( void ) {
  printf( "Convert formats of package files.  Usage:\n\n" );
  printf( "mpkg [global options] convert [options] <input> <output>\n\n" );
  printf( "The options are:\n" );
  printf( "  --set-compression <compression>: force output with " );
  printf( "compression <compression>\n" );
  printf( "  <compression> can be one of:\n" );
#ifdef COMPRESSION_BZIP2
  printf( "    bzip2\n" );
#endif /* COMPRESSION_BZIP2 */
#ifdef COMPRESSION_GZIP
  printf( "    gzip\n" );
#endif /* COMPRESSION_GZIP */
  printf( "    none\n" );
  printf( "\n" );
  printf( "  --set-version <version>: force output with version <version>\n" );
  printf( "  <version> can be one of:\n" );
#ifdef PKGFMT_V1
  printf( "    v1\n" );
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
  printf( "    v2\n" );
#endif /* PKGFMT_V2 */
  printf( "\n" );
  printf( "If you do not specify the compression and version, the output " );
  printf( "format will be guessed from the filename, and follow the input" );
  printf( " where that is ambiguous.\n" );
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

static int convert_pkg( convert_opts *opts ) {
  int status, result;
  pkg_handle *ipkg;
  emit_pkg_streams *opkg;

  status = CONVERT_SUCCESS;
  if ( opts && opts->emit && opts->emit->output_file && opts->input_file) {
    /*
     * If the compression or version options were not set on the
     * command line, try to guess based on the output filename.
     */

    guess_compression_and_version_from_filename( opts->emit );

    /*
     * We need to open the input file here, because if version or
     * compression options for the output were not set on the command
     * line or determined from the output filename, we fall back to
     * matching the input.
     */

    ipkg = open_pkg_file( opts->input_file );
    if ( ipkg ) {
      /*
       * If we still don't know what compression and vesion to use for
       * output, match the input.
       */

      if ( opts->emit->compression == DEFAULT_COMPRESSION )
	opts->emit->compression = ipkg->compression;

      if ( opts->emit->version == DEFAULT_VERSION )
	opts->emit->version = ipkg->version;

      /*
       * If all else fails, leave compression set to defaults, and
       * emit.c will pick one.
       */

      opkg = start_pkg_streams( opts->emit );
      if ( opkg ) {
	result = convert_descr( ipkg, opkg );
	if ( result != CONVERT_SUCCESS ) {
	  fprintf( stderr, "Unable to emit package-description\n" );
	  status = result;
	}

	if ( status == CONVERT_SUCCESS ) {
	  result = start_pkg_content( opts->emit, opkg );
	  if ( result == EMIT_SUCCESS ) {
	    result = convert_files( ipkg, opkg );
	    if ( result != CONVERT_SUCCESS ) {
	      fprintf( stderr, "Unable to emit package contents\n" );
	      status = result;
	    }
	    finish_pkg_content( opts->emit, opkg );
	  }
	  else {
	    fprintf( stderr, "Unable to emit package contents\n" );
	    status = CONVERT_ERROR;
	  }
	}

	finish_pkg_streams( opts->emit, opkg );
	/* Remove output if there was a problem */
	if ( status != CONVERT_SUCCESS ) unlink( opts->emit->output_file );
      }
      else {
	fprintf( stderr, "Unable to open output package %s for conversion\n",
		 opts->emit->output_file );
	status = CONVERT_ERROR;
      }

      /* Make sure we close the package handle */
      close_pkg( ipkg );
    }
    else {
      fprintf( stderr, "Unable to open input package %s for conversion\n",
	       opts->input_file );
      status = CONVERT_ERROR;
    }
  }
  else status = CONVERT_ERROR;

  return status;
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
