#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include <pkg.h>

#define EMIT_BUF_LEN 1024

int emit_file( const char *src, tar_file_info *ti, tar_writer *tw ) {
  int status;
  read_stream *rs;
  write_stream *ws;
  char buf[EMIT_BUF_LEN];
  long len;

  status = EMIT_SUCCESS;
  if ( src && ti && tw ) {
    rs = open_read_stream_none( src );
    if ( rs ) {
      ws = put_next_file( tw, ti );
      if ( ws ) {
        while ( ( len = read_from_stream( rs, buf, EMIT_BUF_LEN ) ) > 0 ) {
          if ( write_to_stream( ws, buf, len ) != len ) {
            fprintf( stderr, "Unable to write to tarball for %s\n", src );
            status = EMIT_ERROR;
            break;
          }
        }
        close_write_stream( ws );
      }
      else {
        fprintf( stderr,
                 "Unable to open write stream to tarball for %s\n", src );
        status = EMIT_ERROR;
      }
      close_read_stream( rs );
    }
    else {
      fprintf( stderr, "Unable to read from file %s\n", src );
      status = EMIT_ERROR;
    }
  }
  else status = EMIT_ERROR;

  return status;
}


void finish_pkg_content( emit_opts *opts, emit_pkg_streams *streams ) {
  if ( opts && streams ) {
    switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
    case V1:
      /*
       * In V1, emit_tw == pkg_tw, so we don't need to do anything
       * special for it here; pkg_tw is closed in finish_streams().
       */
      streams->emit_tw = NULL;
      streams->content_ws = NULL;
      streams->content_out_ws = NULL;
      break;
#endif
#ifdef PKGFMT_V2
    case V2:
      /*
       * In V2, we need to close emit_tw and the underlying
       * write_streams here.
       */
      if ( streams->emit_tw ) {
	close_tar_writer( streams->emit_tw );
	streams->emit_tw = NULL;
      }
      /* content_ws is either content_out_ws or comp_ws */
      streams->content_ws = NULL;
      if ( streams->comp_ws ) {
	close_write_stream( streams->comp_ws );
	streams->comp_ws = NULL;
      }
      if ( streams->content_out_ws ) {
	/*
	 * This is where everything gets flushed out to the outer
	 * tarball.
	 */
	close_write_stream( streams->content_out_ws );
	streams->content_out_ws = NULL;
      }
      break;
#endif
    default:
      fprintf( stderr, "Internal error with get_version()\n" );
    }
  }
}

void finish_pkg_streams( emit_opts *opts, emit_pkg_streams *streams ) {
  if ( opts && streams ) {
    /* Make sure we aren't emitting content */
    switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
    case V1:
      /*
       * In V1, emit_tw is just pkg_tw, and content_ws/content_out_ws
       * are NULL
       */
      streams->emit_tw = NULL;
      streams->content_out_ws = NULL;
      streams->content_ws = NULL;
      break;
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
    case V2:
      if ( streams->emit_tw ) {
	close_tar_writer( streams->emit_tw );
	streams->emit_tw = NULL;
      }
      /* content_ws is just comp_ws or content_out_ws */
      streams->content_ws = NULL;
      if ( streams->comp_ws ) {
	close_write_stream( streams->comp_ws );
	streams->comp_ws = NULL;
      }
      if ( streams->content_out_ws ) {
	close_write_stream( streams->content_out_ws );
	streams->content_out_ws = NULL;
      }
      break;
#endif /* PKGFMT_V2 */
    default:
      fprintf( stderr, "Internal error with get_version()\n" );
    }

    /* First, close pkg_tw */
    if ( streams->pkg_tw ) {
      close_tar_writer( streams->pkg_tw );
      streams->pkg_tw = NULL;
    }

    /*
     * ws is either out_ws or comp_ws, so just set it to NULL
     * without closing it.
     */
    streams->ws = NULL;
#ifdef PKGFMT_V1
    if ( get_version( opts ) == V1 ) {
      if ( streams->comp_ws ) {
	close_write_stream( streams->comp_ws );
	streams->comp_ws = NULL;
      }
    }
#endif

    /* Now we have comp_ws closed if needed, so do out_ws */
    if ( streams->out_ws ) {
      close_write_stream( streams->out_ws );
      streams->out_ws = NULL;
    }

    free( streams );
  }
}

void free_emit_opts( emit_opts *opts ) {
  if ( opts ) {
    if ( opts->output_file ) free( opts->output_file );
    free( opts );
  }
}

pkg_compression_t get_compression( emit_opts *opts ) {
  pkg_compression_t result;

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

pkg_version_t get_version( emit_opts *opts ) {
  pkg_version_t result;

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

void guess_compression_and_version_from_filename( emit_opts *opts ) {
  int n, len;
  char *temp;
#ifdef PKGFMT_V1
# ifdef COMPRESSION_BZIP2
  const char *v1bz2_postfix = ".tar.bz2";
# endif /* COMPRESSION_BZIP2 */
# ifdef COMPRESSION_GZIP
  const char *v1gz_postfix = ".tar.gz";
# endif /* COMPRESSION_GZIP */
  const char *v1none_postfix = ".tar";
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
  const char *v2_postfix = ".mpkg";
#endif /* PKGFMT_V2 */

  if ( opts && opts->output_file ) {
    n = strlen( opts->output_file );
#ifdef PKGFMT_V1
# ifdef COMPRESSION_BZIP2
    len = strlen( v1bz2_postfix );
    if ( n > len ) {
      temp = opts->output_file + ( n - len );
      if ( strcmp( temp, v1bz2_postfix ) == 0 ) {
	/*
	 * Don't guess from filename if we've already seen settings
	 * inconsistent with it
	 */
	if ( ( opts->compression == DEFAULT_COMPRESSION ||
	       opts->compression == BZIP2 ) &&
	     ( opts->version == DEFAULT_VERSION ||
	       opts->version == V1 ) ) {
	  opts->compression = BZIP2;
	  opts->version = V1;
	}
      }
    }
# endif /* COMPRESSION_BZIP2 */
# ifdef COMPRESSION_GZIP
    len = strlen( v1gz_postfix );
    if ( n >len ) {
      temp = opts->output_file + ( n - len );
      if ( strcmp( temp, v1gz_postfix ) == 0 ) {
	/*
	 * Don't guess from filename if we've already seen settings
	 * inconsistent with it
	 */
	if ( ( opts->compression == DEFAULT_COMPRESSION ||
	       opts->compression == GZIP ) &&
	     ( opts->version == DEFAULT_VERSION ||
	       opts->version == V1 ) ) {
	  opts->compression = GZIP;
	  opts->version = V1;
	}
      }
    }
# endif /* COMPRESSION_GZIP */
    len = strlen( v1none_postfix );
    if ( n > len ) {
      temp = opts->output_file + ( n - len );
      if ( strcmp( temp, v1none_postfix ) == 0 ) {
	/*
	 * Don't guess from filename if we've already seen settings
	 * inconsistent with it
	 */
	if ( ( opts->compression == DEFAULT_COMPRESSION ||
	       opts->compression == NONE ) &&
	     ( opts->version == DEFAULT_VERSION ||
	       opts->version == V1 ) ) {
	  opts->compression = NONE;
	  opts->version = V1;
	}
      }
    }
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
    len = strlen( v2_postfix );
    if ( n > len ) {
      temp = opts->output_file + ( n - len );
      if ( strcmp( temp, v2_postfix ) == 0 ) {
	/*
	 * Don't guess from filename if we've already seen settings
	 * inconsistent with it (V2 can't infer compression from
	 * filename)
	 */
	if ( opts->version == DEFAULT_VERSION ||
	     opts->version == V2 ) {
	  opts->version = V2;
	}
      }
    }
#endif /* PKGFMT_V2 */
  }
}

int start_pkg_content( emit_opts *opts,
		       emit_pkg_streams *streams ) {
  int status;

  status = EMIT_SUCCESS;
  if ( opts && streams ) {
    if ( streams->pkg_tw ) {
      switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
      case V1:
	/* We just emit straight to the outer tar_writer in V1 */
	streams->emit_tw = streams->pkg_tw;
	streams->content_out_ws = NULL;
	streams->content_ws = NULL;
	break;
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
      case V2:
	streams->ti_outer.type = TAR_FILE;
	switch ( get_compression( opts ) ) {
	case NONE:
	  strncpy( streams->ti_outer.filename, "package-content.tar",
		   TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	  break;
#ifdef COMPRESSION_GZIP
	case GZIP:
	  strncpy( streams->ti_outer.filename, "package-content.tar.gz",
		   TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	  break;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
	case BZIP2:
	  strncpy( streams->ti_outer.filename, "package-content.tar.bz2",
		   TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	  break;
#endif /* COMPRESSION_BZIP2 */
	default:
	  fprintf( stderr, "Internal error with get_compression()\n" );
	  status = EMIT_ERROR;
	}
	strncpy( streams->ti_outer.target, "", TAR_TARGET_LEN + 1 );
	streams->ti_outer.owner = 0;
	streams->ti_outer.group = 0;
	streams->ti_outer.mode = 0644;
	streams->ti_outer.mtime = opts->pkg_mtime;

	if ( status == EMIT_SUCCESS ) {
	  streams->content_out_ws =
	    put_next_file( streams->pkg_tw, &(streams->ti_outer) );
	  if ( streams->content_out_ws ) {
	    switch ( get_compression( opts ) ) {
	    case NONE:
	      streams->content_ws = streams->content_out_ws;
	      break;
#ifdef COMPRESSION_GZIP
	    case GZIP:
	      /* We reuse comp_ws, since V2 didn't use it in prepare_streams() */
	      streams->comp_ws =
		open_write_stream_from_stream_gzip( streams->content_out_ws );
	      if ( streams->comp_ws )
		streams->content_ws = streams->comp_ws;
	      else {
		fprintf( stderr,
			 "Error setting up gzip compressed stream for inner content tarball in output file %s\n",
			 opts->output_file );
		status = EMIT_ERROR;
	      }
	      break;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
	    case BZIP2:
	      /* We reuse comp_ws, since V2 didn't use it in prepare_streams() */
	      streams->comp_ws =
		open_write_stream_from_stream_bzip2( streams->content_out_ws );
	      if ( streams->comp_ws )
		streams->content_ws = streams->comp_ws;
	      else {
		fprintf( stderr,
			 "Error setting up bzip2 compressed stream for inner content tarball in output file %s\n",
			 opts->output_file );
		status = EMIT_ERROR;
	      }
	      break;
#endif /* COMPRESSION_BZIP2 */
	    default:
	      fprintf( stderr, "Internal error with get_compression()\n" );
	      status = EMIT_ERROR;
	    }

	    if ( status == EMIT_SUCCESS ) {
	      /*
	       * content_ws will be write_stream for the compressed
	       * content tarball; fit emit_tw to it.
	       */
	      streams->emit_tw = start_tar_writer( streams->content_ws );
	      if ( !(streams->emit_tw) ) {
		fprintf( stderr,
			 "Error writing inner content tarball in output file %s\n",
			 opts->output_file );
		status = EMIT_ERROR;
	      }
	    }
	  }
	  else {
	    fprintf( stderr,
		     "Error emitting entry in outer tarball for package content in output file %s\n",
		     opts->output_file );
	    status = EMIT_ERROR;
	  }
	  break;
#endif /* PKGFMT_V2 */
	default:
	  fprintf( stderr, "Internal error with get_version()\n" );
	  status = EMIT_ERROR;
	}
      }

      /* Clean up if we had a problem */
      if ( status != EMIT_SUCCESS ) {
	switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
	case V1:
	  streams->emit_tw = NULL;
	  streams->content_ws = NULL;
	  streams->content_out_ws = NULL;
	  break;
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
	case V2:
	  if ( streams->emit_tw ) {
	    close_tar_writer( streams->emit_tw );
	    streams->emit_tw = NULL;
	  }
	  streams->content_ws = NULL;
	  if ( streams->comp_ws ) {
	    close_write_stream( streams->comp_ws );
	    streams->comp_ws = NULL;
	  }
	  if ( streams->content_out_ws ) {
	    close_write_stream( streams->content_out_ws );
	    streams->content_out_ws = NULL;
	  }
	  break;
#endif /* PKGFMT_V2 */
	default:
	  fprintf( stderr, "Internal error with get_version()\n" );
	}
      }
    }
    else status = EMIT_ERROR;
  }
  else status = EMIT_ERROR;

  return status;
}

emit_pkg_streams * start_pkg_streams( emit_opts *opts ) {
  int status, result;
  emit_pkg_streams *streams;
  struct stat st;

  streams = NULL;
  status = EMIT_SUCCESS;
  if ( opts ) {
    streams = malloc( sizeof( *streams ) );
    if ( streams ) {
      streams->out_ws = NULL;
      streams->comp_ws = NULL;
      streams->ws = NULL;
      streams->pkg_tw = NULL;
#ifdef PKGFMT_V2
      memset( &(streams->ti_outer), sizeof( streams->ti_outer ), 0 );
      /* tar_file_info for use with the outer tarball in V2 */
      streams->content_out_ws = NULL;
      streams->content_ws = NULL;
#endif /* PKGFMT_V2 */
      streams->emit_tw = NULL;

      /* Clear the output file if it exists */
      result = lstat( opts->output_file, &st );
      if ( result == 0 ) {
	/* lstat() succeeded, so it exists */
	if ( S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode ) ) {
	  result = unlink( opts->output_file );
	  if ( result != 0 ) {
	    fprintf( stderr, "Couldn't remove existing file at %s: %s\n",
		     opts->output_file, strerror( errno ) );
	    status = EMIT_ERROR;
	  }
	}
	else {
	  fprintf( stderr,
		   "%s already exists and is not a regular file or symlink\n",
		   opts->output_file );
	  status = EMIT_ERROR;
	}
      }
      else {
	if ( errno != ENOENT ) {
	  fprintf( stderr, "Couldn't stat %s: %s\n",
		   opts->output_file, strerror( errno ) );
	  status = EMIT_ERROR;
	}
	/* else it didn't exist, nothing to do */
      }

      if ( status == EMIT_SUCCESS ) {
	/*
	 * We need to set up the output file here.  For a V1 package,
	 * we set up the appropriate compression stream, and then fit
	 * a tar_writer to it, and then emit files into that, and emit
	 * a package-description at the end.  For a V2 package, we
	 * open a bare write_stream and fit a tar_writer to it.  In
	 * start_pkg_content(), we will start writing a single file
	 * called package-content.tar{|.bz2|.gz}, fit a compression
	 * stream to that if needed, then fit another tar_writer to
	 * that, and emit files.  When we're done emitting files, we
	 * close that tar writer and emit package-description in the
	 * outer tarball.
	 */

	streams->out_ws = open_write_stream_none( opts->output_file );
	if ( streams->out_ws ) {
	  switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
	  case V1:
	    switch ( get_compression( opts ) ) {
	    case NONE:
	      streams->ws = streams->out_ws;
	      break;
#ifdef COMPRESSION_GZIP
	    case GZIP:
	      streams->comp_ws =
		open_write_stream_from_stream_gzip( streams->out_ws );
	      if ( streams->comp_ws ) {
		streams->ws = streams->comp_ws;
	      }
	      else {
		fprintf( stderr,
			 "Unable to open gzip output stream for file %s\n",
			 opts->output_file );
		status = EMIT_ERROR;
	      }
	      break;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
	    case BZIP2:
	      streams->comp_ws = 
		open_write_stream_from_stream_bzip2( streams->out_ws );
	      if ( streams->comp_ws ) {
		streams->ws = streams->comp_ws;
	      }
	      else {
		fprintf( stderr,
			 "Unable to open bzip2 output stream for file %s\n",
			 opts->output_file );
		status = EMIT_ERROR;
	      }
	      break;
#endif /* COMPRESSION_BZIP2 */
	    default:
	      fprintf( stderr, "Internal error with get_compression()\n" );
	      status = EMIT_ERROR;
	    }
	    break;
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
	  case V2:
	    streams->ws = streams->out_ws;
	    break;
#endif /* PKGFMT_V2 */
	  default:
	    fprintf( stderr, "Internal error with get_version()\n" );
	    status = EMIT_ERROR;
	  }
	}
	else {
	  fprintf( stderr,
		   "Unable to open output file %s\n",
		   opts->output_file );
	  status = EMIT_ERROR;
	}
      }

      /*
       * if we have EMIT_SUCCESS here, ws has been initialized and
       * is ready for the tar_writer.
       */

      if ( status == EMIT_SUCCESS ) {
	/*
	 * At this point, we have ws (which is identical to either
	 * out_ws or comp_ws), and we can set up the outer tar writer
	 * on it.
	 */

	streams->pkg_tw = start_tar_writer( streams->ws );
	if ( !(streams->pkg_tw) ) status = EMIT_ERROR;
      }

      if ( status != EMIT_SUCCESS ) {
	if ( streams->pkg_tw ) {
	  close_tar_writer( streams->pkg_tw );
	  streams->pkg_tw = NULL;
	}
	/* ws is just out_ws or comp_ws, so set it to NULL without closing */
	streams->ws = NULL;
	if ( streams->comp_ws ) {
	  close_write_stream( streams->comp_ws );
	  streams->comp_ws = NULL;
	}
	if ( streams->out_ws ) {
	  close_write_stream( streams->out_ws );
	  streams->out_ws = NULL;
	  /* Make sure we remove the file we created when we opened out_ws */
	  unlink( opts->output_file );
	}
	free( streams );
	streams = NULL;
      }
    }
    else {
      fprintf( stderr, "Unable to allocate memory\n" );
      /* it's already NULL */
    }
  }
  /* else it's already NULL */

  return streams;
}
