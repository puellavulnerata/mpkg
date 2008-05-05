#include <stdlib.h>
#include <pkg.h>

char * canonicalize_and_copy( char *path ) {
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

	  parts_tmp = realloc( part_idx, sizeof( *part_idx ) * parts );
	  if ( parts_tmp ) {
	    part_idx = parts_tmp;

	    parts_to_copy = malloc( sizeof( *parts_to_copy ) * parts );
	    if ( parts_to_copy ) {
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

	      parts_tmp = realloc( parts_to_copy,
				   sizeof( *parts_to_copy ) * n );
	      if ( parts_tmp ) {
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

	      free( parts_to_copy );
	    }
	    else {
	      free( tmp );
	      tmp = NULL;
	    }
	  }

	  free( part_idx );
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

char * concatenate_paths( char *a, char *b ) {
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

int is_absolute( char *path ) {
  if ( path ) {
    return ( *path == '/' ) ? 1 : 0;
  }
  else return 0;
}
