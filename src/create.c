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
static int build_pkg_descr( create_opts *, create_pkg_info *,
			    tar_writer *, pkg_descr ** );
static void create_build_pkg( create_opts * );
static int create_parse_options( create_opts *, int, char ** );
static void * dir_info_copier( void * );
static void dir_info_free( void * );
static int emit_file_to_tar_writer( const char *, tar_file_info *, 
				    tar_writer * );
static void * file_info_copier( void * );
static void file_info_free( void * );
static void free_create_opts( create_opts * );
static void free_pkginfo( create_pkg_info * );
static create_compression_opt get_compression( create_opts * );
static int get_dirs_enabled( create_opts * );
static int get_files_enabled( create_opts * );
static time_t get_pkg_mtime( create_pkg_info * );
static char * get_pkg_name( create_opts * );
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
   
      /*
       * Use pre_path_comparator() for all of these so things appear
       * in creation order.
       */

      if ( get_dirs_enabled( opts ) && !error ) {
	temp->dirs = rbtree_alloc( pre_path_comparator,
				   rbtree_string_copier,
				   rbtree_string_free,
				   dir_info_copier,
				   dir_info_free );
	if ( !(temp->dirs) ) error = 1;
      }

      if ( get_files_enabled( opts ) && !error ) {
	temp->files = rbtree_alloc( pre_path_comparator,
				    rbtree_string_copier,
				    rbtree_string_free,
				    file_info_copier,
				    file_info_free );
	if ( !(temp->files) ) error = 1;
      }

      if ( get_symlinks_enabled( opts ) && !error ) {
	temp->symlinks = rbtree_alloc( pre_path_comparator,
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

static int build_pkg_descr( create_opts *opts, create_pkg_info *pkginfo,
			    tar_writer *emit_tw, pkg_descr **descr_out ) {
  int status, result, i;
  pkg_descr *descr;
  /* These are used when enumerating after scan_directory_tree() */
  rbtree_node *n;
  void *info_v;
  char *path;
  create_dir_info *di;
  create_file_info *fi;
  create_symlink_info *si;
  char *tar_filename;
  tar_file_info ti;

  status = CREATE_SUCCESS;
  if ( opts && pkginfo && emit_tw && descr_out ) {
    descr = malloc( sizeof( *descr) );
    if ( descr ) {
      descr->entries = NULL;
      descr->hdr.pkg_name = copy_string( get_pkg_name( opts ) );
      if ( !(descr->hdr.pkg_name) ) {
	fprintf( stderr, "Unable to allocate memory\n" );
	status = CREATE_ERROR;
      }
      descr->hdr.pkg_time = get_pkg_mtime( pkginfo );
      descr->num_entries = 0;
      descr->num_entries_alloced = 0;

      /* Allocate enough space for everyting */
      if ( get_dirs_enabled( opts ) )
	descr->num_entries_alloced += pkginfo->dirs_count;
      if ( get_files_enabled( opts ) )
	descr->num_entries_alloced += pkginfo->files_count;
      if ( get_symlinks_enabled( opts ) )
	descr->num_entries_alloced += pkginfo->symlinks_count;
      ++(descr->num_entries_alloced); /* Allow for ENTRY_LAST */

      if ( descr->num_entries_alloced > 0 ) {
	descr->entries =
	  malloc( sizeof( *(descr->entries) ) *
		  descr->num_entries_alloced );
	if ( !(descr->entries) ) {
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

	/*
	 * TODO refactor this: break out dir/file/symlink loops into
	 * their own functions
	 */
	
	/* Do the directories first */
	if ( pkginfo->dirs && get_dirs_enabled( opts ) &&
	     pkginfo->dirs_count > 0 ) {
	  n = NULL;
	  do {
	    di = NULL;
	    path = rbtree_enum( pkginfo->dirs, n, &info_v, &n );
	    if ( path ) {
	      if ( info_v ) {
		di = (create_dir_info *)info_v;
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
		  }
		  else {
		    if ( descr->entries[i].filename )
		      free( descr->entries[i].filename );
		    if ( descr->entries[i].owner )
		      free( descr->entries[i].owner );
		    if ( descr->entries[i].group )
		      free( descr->entries[i].group );
		    /*
		     * Mark it as the last one for when we
		     * free if we have an error.
		     */
		    descr->entries[i].type = ENTRY_LAST;
		    fprintf( stderr, "Unable to allocate memory\n" );
		    status = CREATE_ERROR;
		  }
		}
		else {
		  fprintf( stderr,
			   "Internal error while create package description (ran out of pkg_descr_entry slots at directory %s)\n",
			   path );
		  status = CREATE_ERROR;
		}
	      }
	      else {
		fprintf( stderr,
			 "Internal error during package create (enumerating dirs rbtree, path %s)\n",
			 path );
		status = CREATE_ERROR;
	      }
	    }
	    /* else we're done */
	  } while ( n && status == CREATE_SUCCESS );
	}
	/* else nothing to do for dirs */

	/* We can free the directory rbtree now */
	if ( pkginfo->dirs ) {
	  rbtree_free( pkginfo->dirs );
	  pkginfo->dirs = NULL;
	}

	/* Now do the files */
	if ( status == CREATE_SUCCESS && pkginfo->files &&
	     get_files_enabled( opts ) && pkginfo->files_count > 0 ) {
	  n = NULL;
	  do {
	    fi = NULL;
	    path = rbtree_enum( pkginfo->files, n, &info_v, &n );
	    if ( path ) {
	      /* TODO check for package-description and throw an error */
	      if ( info_v ) {
		fi = (create_file_info *)info_v;
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
		    ti.mode = 0600;
		    ti.mtime = get_pkg_mtime( pkginfo );
		    result =
		      emit_file_to_tar_writer( fi->src_path, &ti, emit_tw );
		    if ( result != CREATE_SUCCESS ) status = result;
		  }
		  else {
		    if ( descr->entries[i].filename )
		      free( descr->entries[i].filename );
		    if ( descr->entries[i].owner )
		      free( descr->entries[i].owner );
		    if ( descr->entries[i].group )
		      free( descr->entries[i].group );
		    /*
		     * Mark it as the last one for when we
		     * free if we have an error.
		     */
		    descr->entries[i].type = ENTRY_LAST;
		    fprintf( stderr, "Unable to allocate memory\n" );
		    status = CREATE_ERROR;
		  }
		}
		else {
		  fprintf( stderr,
			   "Internal error while create package description (ran out of pkg_descr_entry slots at file %s)\n",
			   path );
		  status = CREATE_ERROR;
		}
	      }
	      else {
		fprintf( stderr,
			 "Internal error during package create (enumerating files rbtree, path %s)\n",
			 path );
		status = CREATE_ERROR;
	      }
	    }
	    /* else we're done */
	  } while ( n && status == CREATE_SUCCESS );
	}
	/* else nothing to do for files */

	/* We can free the file rbtree now */
	if ( pkginfo->files ) {
	  rbtree_free( pkginfo->files );
	  pkginfo->dirs = NULL;
	}

	/* Do the symlinkslast */
	if ( pkginfo->symlinks && get_symlinks_enabled( opts ) &&
	     pkginfo->symlinks_count > 0 ) {
	  n = NULL;
	  do {
	    di = NULL;
	    path = rbtree_enum( pkginfo->symlinks, n, &info_v, &n );
	    if ( path ) {
	      if ( info_v ) {
		si = (create_symlink_info *)info_v;
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

		    /*
		     * Mark it as the last one for when we
		     * free if we have an error.
		     */
		    descr->entries[i].type = ENTRY_LAST;
		    fprintf( stderr, "Unable to allocate memory\n" );
		    status = CREATE_ERROR;
		  }
		}
		else {
		  fprintf( stderr,
			   "Internal error while create package description (ran out of pkg_descr_entry slots at symlink %s)\n",
			   path );
		  status = CREATE_ERROR;
		}
	      }
	      else {
		fprintf( stderr,
			 "Internal error during package create (enumerating symlinks rbtree, path %s)\n",
			 path );
		status = CREATE_ERROR;
	      }
	    }
	    /* else we're done */
	  } while ( n && status == CREATE_SUCCESS );
	}
	/* else nothing to do for symlinks */

	/* We can free the symlink rbtree now */
	if ( pkginfo->symlinks ) {
	  rbtree_free( pkginfo->symlinks );
	  pkginfo->symlinks = NULL;
	}

	/* Add an ENTRY_LAST, and we're done */
	if ( status == CREATE_SUCCESS ) {
	  i = descr->num_entries;
	  if ( i < descr->num_entries_alloced ) {
	    descr->entries[i].type = ENTRY_LAST;
	    descr->entries[i].filename = NULL;
	    descr->entries[i].owner = NULL;
	    descr->entries[i].group = NULL;
	    ++(descr->num_entries);
	  }
	  else {
	    fprintf( stderr,
		     "Internal error while create package description (ran out of pkg_descr_entry slots at ENTRY_LAST)\n" );
	    status = CREATE_ERROR;
	  }
	}
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

static void create_build_pkg( create_opts *opts ) {
  create_pkg_info *pkginfo;
  int status, result;
  /* Bare ws for the output file */
  write_stream *out_ws;
  /*
   * Compressed ws wrapped around out_ws for V1, and around content_ws
   * for V2
   */
  write_stream *comp_ws;
  /*
   * Either out_ws or comp_ws, depending on version and compression.
   * We attach the outer tar_writer to this.
   */
  write_stream *ws;
  /* The outer tar_writer for V2, the only tar_writer for V1 */
  tar_writer *pkg_tw;
#ifdef PKGFMT_V2
  /* The bare ws for the package-content.tar.{|gz|bz2} file */
  write_stream *content_out_ws;
  /* We use comp_ws for the compressed ws around it in the V2 case */
  /* content_ws is either comp_ws or content_out_ws, as appropriate */
  write_stream *content_ws;
#endif /* PKGFMT_V2 */
  /*
   * This is the tar_writer to emit files to.  It will be just pkg_tw
   * for V1, and will be the inner tar_writer for V2
   */
  tar_writer *emit_tw;
  /*
   * This is package description to emit, assembled after
   * scan_directory_tree() and then emitted to package-description.
   */
  pkg_descr *descr;

  if ( opts ) {
    pkginfo = alloc_pkginfo( opts );
    if ( pkginfo ) {
      out_ws = NULL;
      comp_ws = NULL;
      ws = NULL;
      pkg_tw = NULL;
#ifdef PKGFMT_V2
      /* tar_file_info for use with the outer tarball in V2 */
      tar_file_info ti_outer;
      content_out_ws = NULL;
      content_ws = NULL;
#endif /* PKGFMT_V2 */
      emit_tw = NULL;
      descr = NULL;

      /*
       * We need to set up the output file here.  For a V1 package, we
       * set up the appropriate compression stream, and then fit a
       * tar_writer to it, and then emit files into that, and emit a
       * package-description at the end.  For a V2 package, we open a
       * bare write_stream, fit a tar_writer to it, start writing a
       * single file called package-content.tar{|.bz2|.gz}, fit a
       * compression stream to that if needed, then fit another
       * tar_writer to that, and emit files.  When we're done emitting
       * files, we close that tar writer and emit package-description
       * in the outer tarball.
       */
	   
      out_ws = open_write_stream_none( opts->output_file );
      if ( out_ws ) {
	switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
	case V1:
	  switch ( get_compression( opts ) ) {
	  case NONE:
	    ws = out_ws;
	    break;
#ifdef COMPRESSION_GZIP
	  case GZIP:
	    comp_ws = open_write_stream_from_stream_gzip( out_ws );
	    if ( comp_ws ) {
	      ws = out_ws;
	    }
	    else {
	      fprintf( stderr,
		       "Unable to open gzip output stream for file %s\n",
		       opts->output_file );
	      status = CREATE_ERROR;
	    }
	    break;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
	  case BZIP2:
	    comp_ws = open_write_stream_from_stream_bzip2( out_ws );
	    if ( comp_ws ) {
	      ws = out_ws;
	    }
	    else {
	      fprintf( stderr,
		       "Unable to open bzip2 output stream for file %s\n",
		       opts->output_file );
	      status = CREATE_ERROR;
	    }
	    break;
#endif /* COMPRESSION_BZIP2 */
	  default:
	    fprintf( stderr, "Internal error with get_compression()\n" );
	    status = CREATE_ERROR;
	  }
	  break;
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
	case V2:
	  ws = out_ws;
	  break;
#endif /* PKGFMT_V2 */
	default:
	  fprintf( stderr, "Internal error with get_version()\n" );
	  status = CREATE_ERROR;
	}
      }
      else {
	fprintf( stderr,
		 "Unable to open output file %s\n",
		 opts->output_file );
	status = CREATE_ERROR;
      }

      if ( status == CREATE_SUCCESS ) {
	/*
	 * At this point, we have ws (which is identical to either
	 * out_ws or comp_ws), and we can set up the outer tar writer
	 * on it.
	 */

	pkg_tw = start_tar_writer( ws );
	if ( pkg_tw ) {
	  switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
	  case V1:
	    /* We just emit straight to the outer tar_writer in V1 */
	    emit_tw = pkg_tw;
	    break;
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
	  case V2:
	    ti_outer.type = TAR_FILE;
	    switch ( get_compression( opts ) ) {
	    case NONE:
	      strncpy( ti_outer.filename, "package-content.tar",
		       TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	      break;
#ifdef COMPRESSION_GZIP
	    case GZIP:
	      strncpy( ti_outer.filename, "package-content.tar.gz",
		       TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	      break;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
	    case BZIP2:
	      strncpy( ti_outer.filename, "package-content.tar.bz2",
		       TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1 );
	      break;
#endif /* COMPRESSION_BZIP2 */
	    default:
	      fprintf( stderr, "Internal error with get_compression()\n" );
	      status = CREATE_ERROR;
	    }
	    strncpy( ti_outer.target, "", TAR_TARGET_LEN + 1 );
	    ti_outer.owner = 0;
	    ti_outer.group = 0;
	    ti_outer.mode = 0755;
	    ti_outer.mtime = get_pkg_mtime( pkginfo );
	    content_out_ws = put_next_file( pkg_tw, &ti_outer );
	    if ( content_out_ws ) {
	      switch ( get_compression( opts ) ) {
	      case NONE:
		content_ws = content_out_ws;
		break;
#ifdef COMPRESSION_GZIP
	      case GZIP:
		/* We reuse comp_ws, since V2 didn't use it earlier */
		comp_ws = open_write_stream_from_stream_gzip( content_out_ws );
		if ( comp_ws ) content_ws = comp_ws;
		else {
		  fprintf( stderr,
			   "Error setting up gzip compressed stream for inner content tarball in output file %s\n",
			   opts->output_file );
		  status = CREATE_ERROR;
		}
		break;
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
	      case BZIP2:
		/* We reuse comp_ws, since V2 didn't use it earlier */
		comp_ws =
		  open_write_stream_from_stream_bzip2( content_out_ws );
		if ( comp_ws ) content_ws = comp_ws;
		else {
		  fprintf( stderr,
			   "Error setting up bzip2 compressed stream for inner content tarball in output file %s\n",
			   opts->output_file );
		  status = CREATE_ERROR;
		}
		break;
#endif /* COMPRESSION_BZIP2 */
	      default:
		fprintf( stderr, "Internal error with get_compression()\n" );
		status = CREATE_ERROR;
	      }

	      if ( status == CREATE_SUCCESS ) {
		/*
		 * content_ws will be write_stream for the compressed
		 * content tarball; fit emit_tw to it.
		 */
		emit_tw = start_tar_writer( content_ws );
		if ( !emit_tw ) {
		  fprintf( stderr,
			   "Error writing inner content tarball in output file %s\n",
			   opts->output_file );
		  status = CREATE_ERROR;
		}
	      }
	    }
	    else {
	      fprintf( stderr,
		       "Error emitting entry in outer tarball for package content in output file %s\n",
		       opts->output_file );
	      status = CREATE_ERROR;
	    }
	    break;
#endif /* PKGFMT_V2 */
	  default:
	    fprintf( stderr, "Internal error with get_version()\n" );
	    status = CREATE_ERROR;
	  }
	}
	else status = CREATE_ERROR;

	if ( status == CREATE_SUCCESS ) {
	  /*
	   * scan_directory_tree() recursively walks the directory
	   * tree starting from opts->input_directory, and assembles
	   * the three rbtrees in pkginfo; the keys are paths starting
	   * from the base of the directory tree, and the values are
	   * create_dir_info, create_file_info or create_symlink_info
	   * structures which can be used later to assemble the
	   * pkgdescr entries in the correct order, and, in the case
	   * of create_file_info, locate the needed files when we emit
	   * the tarball.
	   */

	  result = scan_directory_tree( opts, pkginfo );
	  if ( status == CREATE_SUCCESS ) {
	    /*
	     * Next, we iterate over the rbtrees that
	     * scan_directory_tree() created, first pkginfo->dirs, then
	     * files, then symlinks.  As we go, we assemble a pkg_descr,
	     * which we will eventually write out to the
	     * package-description file.  Also, for pkginfo->files, we
	     * emit files to the output package as we go.  Thus,
	     * package-description is emitted last, after all of the
	     * package contents.
	     */

	    result = build_pkg_descr( opts, pkginfo, emit_tw, &descr );
	    if ( result != CREATE_SUCCESS ) status = result;
	  }
	  else {
	    fprintf( stderr,
		     "Failed to scan directory tree %s for package build\n",
		     opts->input_directory );
	    status = result;
	  }
	} /* else problems with pkg_tw/emit_tw */

	/* Close down emit_tw/pkg_tw, etc. */
	switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
	case V1:
	  /*
	   * In V1,emit_tw == pkg_tw, so we don't need to do anything
	   * special for it here; pkg_tw is closed later anyway.
	   */
	  emit_tw = NULL;
	  break;
#endif
#ifdef PKGFMT_V2
	case V2:
	  /*
	   * In V2, we need to close emit_tw and the underlying
	   * write_streams here, and then emit the
	   * package-description.
	   */
	  if ( emit_tw ) {
	    close_tar_writer( emit_tw );
	    emit_tw = NULL;
	  }
	  /* content_ws is either content_out_ws or comp_ws */
	  content_ws = NULL;
	  if ( comp_ws) {
	    close_write_stream( comp_ws );
	    comp_ws = NULL;
	  }
	  if ( content_out_ws ) {
	    /*
	     * This is where everything gets flushed out to the outer
	     * tarball.
	     */
	    close_write_stream( content_out_ws );
	    content_out_ws = NULL;
	  }
	  break;
#endif
	default:
	  fprintf( stderr, "Internal error with get_version()\n" );
	  status = CREATE_ERROR;
	}

	if ( status == CREATE_SUCCESS ) {
	  /* TODO emit package-description to pkg_tw */
	}

	/* Now we close down pkg_tw */
	if ( pkg_tw ) {
	  close_tar_writer( pkg_tw );
	  pkg_tw = NULL;
	}
      } /* else problems getting ws ready */

      /*
       * Close ws/comp_s/out_ws, and remove the output file in case of
       * error.
       */

      ws = NULL;
      switch ( get_version( opts ) ) {
#ifdef PKGFMT_V1
      case V1:
	if ( comp_ws ) {
	  close_write_stream( comp_ws );
	  comp_ws = NULL;
	}
	break;
#endif
#ifdef PKGFMT_V2
      case V2:
	/* Don't need to do anything in this case */
	break;
#endif
      }
      close_write_stream( out_ws );
      out_ws = NULL;
      /* Remove the output if we had an error with it */
      if ( status != CREATE_SUCCESS ) unlink( opts->output_file );
      
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
