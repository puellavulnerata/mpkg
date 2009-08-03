#include <stdlib.h>
#include <string.h>

#include <pkg.h>

static int get_last_and_base( const char *, int *, const char **, int * );

char * canonicalize_and_copy( const char *path ) {
  char *tmp, *tmp2;
  unsigned long len, slashes, i, j, n, parts, initial_dotdots;
  int in_slash_run, absolute, starting;
  struct {
    unsigned long start, len;
  } *part_idx, *parts_to_copy, *parts_tmp;

  if ( path ) {
    len = strlen( path );
    slashes = 0;
    for ( i = 0; i < len; ++i ) {
      if ( path[i] == '/' ) ++slashes;
    }
    tmp = malloc( sizeof( char ) * ( len + 1 ) );
    if ( tmp ) {
      if ( slashes > 0 ) {
	part_idx = malloc( sizeof( *part_idx ) * ( slashes + 1 ) );
	if ( part_idx ) {
	  /*
	   * We parse paths according to standard UNIX conventions: /
	   * is the separator, and several consecutive instances of /
	   * are equivalent to a single /.  A path that begins with /
	   * is absolute, one that does not is relative.  The elements
	   * of the path are the names between the /s.  The elements
	   * . and .. are special.  A . collapses into its
	   * predecessor.  A .. removes its predecessor, unless that
	   * predecessor is also a .., in which case they stack up.
	   */

	  in_slash_run = 0;
	  starting = 1;
	  parts = 0;
	  for ( i = 0, n = 0; i < len; ++i ) {
	    if ( path[i] == '/' ) {
	      if ( starting ) {
		in_slash_run = 1;
		starting = 0;
		absolute = 1;
	      }
	      else {
		if ( !in_slash_run ) {
		  in_slash_run = 1;
		  ++n;
		}
	      }
	    }
	    else {
	      if ( starting ) {
		in_slash_run = 0;
		starting = 0;
		absolute = 0;

		parts = n + 1;
		part_idx[n].start = i;
		part_idx[n].len = 1;
	      }
	      else {
		if ( in_slash_run ) {
		  /* Start a new part at this char */
		  in_slash_run = 0;

		  parts = n + 1;
		  part_idx[n].start = i;
		  part_idx[n].len = 1;
		}
		else {
		  /* Make the existing part one char longer */
		  ++(part_idx[n].len);
		}
	      }
	    }
	  }

	  if ( parts > 0 ) {
	    parts_tmp = realloc( part_idx, sizeof( *part_idx ) * parts );
	  }
	  else parts_tmp = 0;

	  if ( parts_tmp || parts == 0 ) {
	    part_idx = parts_tmp;

	    if ( parts > 0 ) {
	      parts_to_copy = malloc( sizeof( *parts_to_copy ) * parts );
	    }
	    else parts_to_copy = NULL;

	    if ( parts_to_copy || parts == 0 ) {
	      /*
	       * We already know whether the path is absolute or
	       * relative.  Since .. can always cancel against its
	       * non-.. or . predecessor, except for in the root, as
	       * can ., and .. acts like . at the root, an absolute
	       * path always consists of some finite number of non
	       * . or .. elements.  A relative path consists of some
	       * finite number of repetitions of .., followed by some
	       * finite number of non . or .. elements.  We must find
	       * the number of repetitions in the relative case, and
	       * list the elements in both cases, and then transcribe
	       * as appropriate.
	       */

	      initial_dotdots = n = 0;
	      for ( i = 0; i < parts; ++i ) {
		if ( part_idx[i].len == 1 ) {
		  if ( path[part_idx[i].start] == '.' ) continue;
		}
		else if ( part_idx[i].len == 2 ) {
		  if ( path[part_idx[i].start] == '.' &&
		       path[part_idx[i].start + 1] == '.' ) {
		    if ( n > 0 ) --n;
		    else ++initial_dotdots;
		    continue;
		  }
		}

		parts_to_copy[n].start = part_idx[i].start;
		parts_to_copy[n].len = part_idx[i].len;
	        ++n;
	      }

	      if ( n > 0 ) {
		parts_tmp = realloc( parts_to_copy,
				     sizeof( *parts_to_copy ) * n );
	      }
	      else parts_tmp = NULL;

	      if ( parts_tmp || n == 0 ) {
		parts_to_copy = parts_tmp;

		i = 0;
		if ( absolute ) tmp[i++] = '/';
		else {
		  for ( j = 0; j < initial_dotdots; ++j ) {
		    tmp[i] = '.';
		    tmp[i+1] = '.';
		    tmp[i+2] = '/';
		    i += 3;
		  }
		}
		for ( j = 0; j < n; ++j ) {
		  memcpy( &(tmp[i]), &(path[parts_to_copy[j].start]),
			  parts_to_copy[j].len );
		  i += parts_to_copy[j].len;
		  if ( j + 1 < n ) tmp[i++] = '/';
		}
		tmp[i++] = '\0';

		tmp2 = realloc( tmp, sizeof( char ) * i );
		if ( tmp2 ) tmp = tmp2;
		else {
		  free( tmp );
		  tmp = NULL;
		}
	      }
	      else {
		free( tmp );
		tmp = NULL;
	      }

	      if ( parts_to_copy ) free( parts_to_copy );
	    }
	    else {
	      free( tmp );
	      tmp = NULL;
	    }
	  }

	  if ( part_idx ) free( part_idx );
	  return tmp;
	}
	else {
	  free( tmp );
	  return NULL;
	}
      }
      else {
	strncpy( tmp, path, len + 1 );
	return tmp;
      }
    }
    else return NULL;
  }
  else return NULL;
}

int canonicalize_path( char *path ) {
  char *tmp;
  unsigned long pathlen, tmplen;
  int status;

  status = 0;
  if ( path ) {
    pathlen = strlen( path );
    tmp = canonicalize_and_copy( path );
    if ( tmp ) {
      tmplen = strlen( tmp );
      if ( tmplen <= pathlen ) strncpy( path, tmp, pathlen + 1 );
      else status = -1;
      free( tmp );
    }
    else status = -1;
  }
  else status = -1;

  return status;
}

char * concatenate_paths( const char *a, const char *b ) {
  char *tmp, *concat;
  unsigned long alen, blen;

  if ( a && b ) {
    alen = strlen( a );
    blen = strlen( b );
    tmp = malloc( sizeof( char ) * ( alen + blen + 2 ) );
    memcpy( tmp, a, alen );
    tmp[alen] = '/';
    memcpy( tmp + alen + 1, b, blen );
    tmp[alen+blen+1] = '\0';
    concat = canonicalize_and_copy( tmp );
    free( tmp );
    return concat;
  }
  else return NULL;
}

char * get_base_path( const char *path ) {
  const char *lastcomp_base;
  int baselen, lastcomp_len, result;
  char *base;

  base = NULL;
  if ( path ) {
    result = get_last_and_base( path, &baselen,
				&lastcomp_base, &lastcomp_len );
    if ( result == 0 ) {
      base = malloc( sizeof( *base ) * ( baselen + 1 ) );
      if ( base ) {
	memcpy( base, path, sizeof( *base ) * baselen );
	base[baselen] = '\0';
      }
    }
  }

  return base;
}

static int get_last_and_base( const char *path, int *baselen_out,
			      const char **lastcomp_base_out,
			      int *lastcomp_len_out ) {
  enum {
    GLC_START,
    GLC_COMP,
    GLC_SLASH,
    GLC_DONE
  } state;
  const char *curr, *currcomp;
  int baselen, currlen, currcomp_len, status;

  status = 0;
  if ( path && baselen_out && lastcomp_base_out && lastcomp_len_out ) {
    state = GLC_START;
    curr = path;
    currcomp = NULL;
    currcomp_len = 0;
    currlen = 0;
    while ( state != GLC_DONE ) {
      switch ( state ) {
      case GLC_START:
	if ( *curr == '\0' ) state = GLC_DONE;
	else if ( *curr == '/' ) state = GLC_SLASH;
	else {
	  state = GLC_COMP;
	  currcomp = curr;
	  currcomp_len = 1;
	}
	break;
      case GLC_COMP:
	if ( *curr == '\0' ) state = GLC_DONE;
	else if ( *curr == '/' ) state = GLC_SLASH;
	else ++currcomp_len;
	break;
      case GLC_SLASH:
	if ( *curr == '\0' ) state = GLC_DONE;
	else if ( *curr != '/' ) {
	  state = GLC_COMP;
	  currcomp = curr;
	  currcomp_len = 1;
	}
	/* if ( *curr == '/' ) do nothing */
	break;
      default:
	/* error case */
	state = GLC_DONE;
	currcomp = NULL;
	currcomp_len = 0;
	status = -1;
      }

      ++curr;
    }

    *baselen_out = currcomp - path;
    *lastcomp_base_out = currcomp;
    *lastcomp_len_out = currcomp_len;
  }
  else status = -1;

  return status;
}

char * get_last_component( const char *path ) {
  const char *lastcomp_base;
  int baselen, lastcomp_len, result;
  char *lastcomp;

  lastcomp = NULL;
  if ( path ) {
    result = get_last_and_base( path, &baselen,
				&lastcomp_base, &lastcomp_len );
    if ( result == 0 ) {
      lastcomp = malloc( sizeof( *lastcomp ) * ( lastcomp_len + 1 ) );
      if ( lastcomp ) {
	memcpy( lastcomp, lastcomp_base, sizeof( *lastcomp ) * lastcomp_len );
	lastcomp[lastcomp_len] = '\0';
      }
    }
  }

  return lastcomp;
}

int is_absolute( const char *path ) {
  if ( path ) {
    return ( *path == '/' ) ? 1 : 0;
  }
  else return 0;
}

char * remove_path_prefix( const char *path, const char *prefix ) {
  char *result, *temp;

  /*
   * The path and prefix must both be canonical, and must either both
   * be absolute or both be relative.  We test for
   * absolute/relativeness, but assume canonicalness.
   */

  result = NULL;
  if ( path && prefix ) {
    if ( is_absolute( path ) == is_absolute( prefix ) ) {
      /*
       * Since both paths are canonical, .. can only appear at the
       * beginning, and / occurs singly.  Thus, prefix must be a
       * prefix of path as a string to be a prefix as a path.
       */
      
      temp = strstr( path, prefix );
      if ( temp == path ) {
	/* It is a prefix of path */
	if ( strlen( prefix ) <= strlen( path ) ) {
	  temp = malloc( ( strlen( path ) - strlen( prefix ) + 2 ) *
			 sizeof( *temp ) );
	  if ( temp ) {
	    temp[0] = '/'; /* Force it to be absolute */
	    strncpy( temp + 1, path + strlen( prefix ),
		     strlen( path ) - strlen( prefix ) + 1 );
	    result = canonicalize_and_copy( temp );
	    free( temp );
	  }
	}
      }
    }
    /* else NULL */
  }

  return result;
}
