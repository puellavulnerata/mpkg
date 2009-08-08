#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <pkg.h>

static char hex_digit_to_char( unsigned char );

/*
 * int copy_file( const char *dest, const char *src );
 *
 * Copy a file.  Return LINK_OR_COPY error codes.
 */

#define COPY_BUF_SIZE 1024

int copy_file( const char *dest, const char *src ) {
  int result, status, srcfd, dstfd;
  long count, written, wcount;
  struct stat st;
  mode_t dst_mode;
  char buf[COPY_BUF_SIZE];

  status = LINK_OR_COPY_SUCCESS;
  if ( dest && src ) {
    result = lstat( dest, &st );
    if ( result == 0 ) {
      /* Something exists, try to unlink it. */

      result = unlink( dest );
      if ( result != 0 ) status = LINK_OR_COPY_ERROR;
    }
    else if ( errno != ENOENT ) status = LINK_OR_COPY_ERROR;

    if ( status == LINK_OR_COPY_SUCCESS ) {
      srcfd = open( src, O_RDONLY );
      if ( srcfd != -1 ) {
	/* Stat it to get the source mode */
	result = fstat( srcfd, &st );
	if ( result == 0 ) dst_mode = st.st_mode;
	else dst_mode = 0600;

	/* The destination name should be clear now, try to copy it */
	dstfd = open( dest, O_RDWR | O_CREAT | O_EXCL, 0600 );
	if ( dstfd != -1 ) {
	  while ( ( count = read( srcfd, buf, COPY_BUF_SIZE ) ) > 0 ) {
	    written = 0;
	    while ( written < count &&
		    ( wcount = write( dstfd, buf + written,
				      count - written ) ) >= 0 ) {
	      written += wcount;
	    }

	    if ( wcount < 0 ) {
	      /* Error writing */
	      fprintf( stderr,
		       "copy_file(): write error during copy: %s\n",
		       strerror( errno ) );
	      
	      if ( errno == ENOSPC ) status = LINK_OR_COPY_OUT_OF_DISK;
	      else status = LINK_OR_COPY_ERROR;
	      /* Break out of read loop */
	      break;
	    }
	  }

	  /*
	   * We fell out of the loop, so count <= 0.  count == 0 means
	   * EOF, count < 0 means error.  Check status in case we set
	   * it on a write error and broke out of the loop.
	   */

	  if ( status == LINK_OR_COPY_SUCCESS && count < 0 ) {
	    fprintf( stderr,
		     "copy_file(): read error during copy: %s\n",
		     strerror( errno ) );
	    status = LINK_OR_COPY_ERROR;
	  }
	  
	  close( dstfd );

	  /* Adjust the mode */
	  chmod( dest, dst_mode );
	}
	else {
	  /* Couldn't open dest for copy */
	  
	  fprintf( stderr,
		   "copy_file(): couldn't open dest %s for copy: %s\n",
		   dest, strerror( errno ) );
	    
	  if ( errno == ENOSPC ) status = LINK_OR_COPY_OUT_OF_DISK;
	  else status = LINK_OR_COPY_ERROR;
	}
	
	close( srcfd );
      }
      else {
	/* Couldn't open source for copy */
	    
	fprintf( stderr,
		 "copy_file(): couldn't open src %s for copy: %s\n",
		 src, strerror( errno ) );   
	status = LINK_OR_COPY_ERROR;
      }
    }
  }
  else status = LINK_OR_COPY_ERROR;

  return status;
}

/*
 * char * copy_string( const char *s );
 *
 * Copy a string into a newly-allocated block of memory.
 */

char * copy_string( const char *s ) {
  int len;
  char *t;

  if ( s ) {
    len = strlen( s );
    t = malloc( sizeof( *s ) * ( len + 1 ) );
    if ( t ) {
      strncpy( t, s, len + 1 );
      return t;
    }
    else return NULL;
  }
  else return NULL;
}

/*
 * dbg_printf()
 *
 * Called by the debug macros when enabled
 */

void dbg_printf( char const *file, int line, char const *fmt, ... ) {
  va_list s;

  fprintf( stderr, "%s(%d): ", file, line );
  va_start( s, fmt );
  vfprintf( stderr, fmt, s );
  va_end( s );
  fprintf( stderr, "\n" );
}

#define INITIAL_DIR_ALLOC 16

char * get_current_dir( void ) {
  int alloced, len;
  char *cwd, *temp;

  alloced = INITIAL_DIR_ALLOC;
  cwd = NULL;
  while ( !cwd ) {
    cwd = malloc( sizeof( *cwd ) * alloced );
    if ( !(getcwd( cwd, alloced ) ) ) {
      free( cwd );
      cwd = NULL;
      alloced *= 2;
    }
  }
  len = strlen( cwd );
  temp = realloc( cwd, sizeof( *cwd ) * ( len + 1 ) );
  if ( temp ) cwd = temp;
  else {
    free( cwd );
    cwd = NULL;
  }

  return cwd;
}

char * get_path_component( char *p, char **tmp ) {
  enum {
    START,
    IN_NAME,
    IN_SLASH
  } parse_state;
  char *curr, *start;

  if ( tmp ) {
    if ( p ) {
      *tmp = p;
      curr = p;
      parse_state = START;
    }
    else {
      if ( *tmp ) {
	curr = *tmp;
	*curr = '/';
	parse_state = IN_SLASH;
      }
      else return NULL;
    }
    
    while ( *curr ) {
      switch ( parse_state ) {
      case START:
	if ( *curr == '/' ) {
	  parse_state = IN_SLASH;
	}
	else {
	  parse_state = IN_NAME;
	  start = curr;
	}
	break;
      case IN_SLASH:
	if ( *curr != '/' ) {
	  parse_state = IN_NAME;
	  start = curr;
	}
	break;
      case IN_NAME:
	if ( *curr == '/' ) {
	  parse_state = IN_SLASH;
	  *curr = 0;
	  *tmp = curr;
	  return start;
	}
	break;
      default:
	return NULL;
      }
      ++curr;
    }

    if ( parse_state == IN_NAME ) {
      *tmp = NULL;
      return start;
    }
    else {
      *tmp = NULL;
      return NULL;
    }
  }
  else return NULL;
}

/*
 * char * get_temp_dir( void );
 *
 * Create a temporary directory
 */

char * get_temp_dir( void ) {
  const char *basedir;
  char *template;
  char *temp_dir, *tmp;

  temp_dir = NULL;
  basedir = get_temp();
  if ( basedir ) {
    template = malloc( sizeof( *template ) * ( strlen( basedir ) + 64 ) );
    if ( template ) {
      strcpy( template, basedir );
      sprintf( template, "%s/mpkg.%d.XXXXXX", basedir, getpid() );
      if ( canonicalize_path( template ) == 0 ) {
	temp_dir = mkdtemp( template );
	if ( temp_dir ) {
	  tmp = realloc( temp_dir,
			 sizeof( *tmp ) * ( strlen( temp_dir ) + 1 ) );
	  if ( tmp ) temp_dir = tmp;
	}
	else free( template );
      }
      else free( template );
    }
  }

  return temp_dir;
}

/*
 * char * hash_to_string( unsigned char *hash, unsigned long len );
 *
 * Convert an MD5 hash to a printable string
 */

char * hash_to_string( unsigned char *hash, unsigned long len ) {
  char *str_temp;
  unsigned long i;

  if ( hash && len > 0 ) {
    str_temp = malloc( 2 * len + 1 );
    if ( str_temp ) {
      for ( i = 0; i < len; ++i ) {
	str_temp[2*i] = hex_digit_to_char( ( hash[i] >> 4 ) & 0xf );
	str_temp[2*i+1] = hex_digit_to_char( hash[i] & 0xf );
      }
      str_temp[2*len] = '\0';
      return str_temp;
    }
    else return NULL;
  }
  else return NULL;
}

/*
 * char hex_digit_to_char( unsigned char );
 *
 * Convert a hex digit to a printable character
 */

static char hex_digit_to_char( unsigned char x ) {
  switch ( x ) {
  case 0x0:
    return '0';
    break;
  case 0x1:
    return '1';
    break;
  case 0x2:
    return '2';
    break;
  case 0x3:
    return '3';
    break;
  case 0x4:
    return '4';
    break;
  case 0x5:
    return '5';
    break;
  case 0x6:
    return '6';
    break;
  case 0x7:
    return '7';
    break;
  case 0x8:
    return '8';
    break;
  case 0x9:
    return '9';
    break;
  case 0xa:
    return 'a';
    break;
  case 0xb:
    return 'b';
    break;
  case 0xc:
    return 'c';
    break;
  case 0xd:
    return 'd';
    break;
  case 0xe:
    return 'e';
    break;
  case 0xf:
    return 'f';
    break;
  default:
    return -1;
  }
}

/*
 * int is_whitespace( char *str );
 *
 * Return 1 if str is all whitespace
 */

int is_whitespace( char *str ) {
  char c;

  if ( str ) {
    while ( c = *str++ ) {
      if ( !isspace( c ) ) return 0;
    }
    return 1;
  }
  else return -1;
}

#define LINK_OR_COPY_BUF_SIZE 1024

int link_or_copy( const char *dest, const char *src ) {
  struct stat st;
  int result, status, dstfd, srcfd;
  unsigned char buf[LINK_OR_COPY_BUF_SIZE];
  ssize_t count, wcount, written;

  status = LINK_OR_COPY_SUCCESS;
  if ( src && dest ) {
    /*
     * link() will not overwrite an existing file, so check for one and unlink
     * if necessary.
     */
    result = lstat( dest, &st );
    if ( result == 0 ) {
      /* Something exists, try to unlink it. */

      result = unlink( dest );
      if ( result != 0 ) status = LINK_OR_COPY_ERROR;
    }
    else if ( errno != ENOENT ) status = LINK_OR_COPY_ERROR;

    if ( status == LINK_OR_COPY_SUCCESS ) {
      /* The destination name should be clear now, try to link it */

      result = link( src, dest );
      if ( result != 0 ) {
	/* The call to link() failed.  Why? */

	if ( errno == ENOSPC ) status = LINK_OR_COPY_OUT_OF_DISK;
	else if ( errno == EXDEV || /* Cross-device hard link error */
		  /* Could be a filesystem with no hard link support */
		  errno == EPERM ||
		  /* Maximum link count for src already */
		  errno == EMLINK ) {
	  /* Go ahead and try the copy */
	  dstfd = open( dest, O_RDWR | O_CREAT | O_EXCL, 0600 );
	  if ( dstfd != -1 ) {
	    srcfd = open( src, O_RDONLY );
	    if ( srcfd != -1 ) {
	      while ( ( count = read( srcfd, buf, LINK_OR_COPY_BUF_SIZE ) )
		      > 0 ) {
		written = 0;
		while ( written < count &&
			( wcount = write( dstfd, buf + written,
					  count - written ) ) >= 0 ) {
		  written += wcount;
		}

		if ( wcount < 0 ) {
		  /* Error writing */
		  fprintf( stderr,
			   "link_or_copy(): write error during copy: %s\n",
			   strerror( errno ) );

		  if ( errno == ENOSPC ) status = LINK_OR_COPY_OUT_OF_DISK;
		  else status = LINK_OR_COPY_ERROR;
		  /* Break out of read loop */
		  break;
		}
	      }

	      /*
	       * We fell out of the loop, so count <= 0.  count == 0
	       * means EOF, count < 0 means error.  Check status in
	       * case we set it on a write error and broke out of the
	       * loop.
	       */

	      if ( status == LINK_OR_COPY_SUCCESS && count < 0 ) {
		fprintf( stderr,
			 "link_or_copy(): read error during copy: %s\n",
			 strerror( errno ) );
		status = LINK_OR_COPY_ERROR;
	      }

	      close( srcfd );
	    }
	    /* Source open failed */
	    else {
	      fprintf( stderr,
		       "link_or_copy(): couldn't open src %s for copy: %s\n",
		       src, strerror( errno ) );
	      status = LINK_OR_COPY_ERROR;
	    }

	    close( dstfd );
	    /* If we failed somewhere, unlink it */
	    if ( status != LINK_OR_COPY_SUCCESS ) unlink( dest );
	  }
	  else {
	    /* Couldn't open dest for copy */

	      fprintf( stderr,
		       "link_or_copy(): couldn't open dest %s for copy: %s\n",
		       dest, strerror( errno ) );
	    
	    if ( errno == ENOSPC ) status = LINK_OR_COPY_OUT_OF_DISK;
	    else status = LINK_OR_COPY_ERROR;
	  }
	}
	/* Not worth trying copy */
	else status = LINK_OR_COPY_ERROR;
      }
    }
  }

  return status;
}

/*
 * int parse_strings_from_line( char *line, char ***strings_out );
 *
 * Find non-whitespace substrings of line; put NUL characters in line
 * to terminate them, and allocate a block of memory to hold a
 * NULL-terminated array of pointers into line at the start of each
 * substring.  Pass this array to the caller in strings_out.
 */

#define INITIAL_STRINGS_ALLOC 4

int parse_strings_from_line( char *line, char ***strings_out ) {
  /*
   * Parse out all non-whitespace substrings of at least one char;
   * return write to strings_out a block of pointers to them, and
   * write a NUL afer each.  Terminate strings_out will a NULL
   * pointer.
   */
  int strings_seen, strings_alloced, new_alloced, last_one;
  char **strings, **temp;
  char *curr_string;

  if ( line && strings_out ) {
    strings_seen = strings_alloced = 0;
    strings = NULL;
    curr_string = NULL;
    last_one = 0;
    do {
      /* Check if this is the last char before we write any NULs out */
      if ( *line == '\0' ) {
	last_one = 1;
      }
      if ( curr_string ) {
	/*
	 * We're currently in a non-WS string
	 */
	if ( isspace( *line ) || *line == '\0' ) {
	  /*
	   * It just ended on this char, so we need to terminate it
	   * with a NUL and add it to our list.
	   */

	  /* Write out the NUL */
	  *line = '\0';
	  /* Expand strings as necessary */
	  while ( !( strings_alloced > 0 &&
		     strings_seen < strings_alloced ) ) {
	    if ( strings_alloced > 0 ) {
	      new_alloced = 2 * strings_alloced;
	      temp = realloc( strings, sizeof( *strings ) * new_alloced );
	      if ( !temp ) {
		if ( strings ) free( strings );
		fprintf( stderr, "Error allocating memory in parse_strings_from_line()\n" );
		return -1;
	      }
	      strings = temp;
	      strings_alloced = new_alloced;
	    }
	    else {
	      new_alloced = INITIAL_STRINGS_ALLOC;
	      temp = malloc( sizeof( *strings ) * new_alloced );
	      if ( !temp ) {
		if ( strings ) free( strings );
		fprintf( stderr, "Error allocating memory in parse_strings_from_line()\n" );
		return -1;
	      }
	      strings = temp;
	      strings_alloced = new_alloced;
	    }
	  }
	  /* We know we have a big enough buffer now */
	  /* Add this string to the buffer */
	  strings[strings_seen++] = curr_string;
	  /* And we're not in a non-WS string any more */
	  curr_string = NULL;
	}
	/*
	 * Else it wasn't a whitespace, so it was just another one in
	 * the current string.  Keep scanning ahead.
	 */
      }
      else {
	/* We're not in a non-WS string */
	if ( !( isspace( *line ) || *line == '\0' ) ) {
	  /* It's not whitespace, start a new string */
	  curr_string = line;
	}
	/* else it's just more whitespace, keep scanning ahead */
      }
      /* Done processing this char */
      ++line;
    } while ( !last_one );
    /* Resize the buffer to strings_seen + 1 */
    new_alloced = strings_seen + 1;
    if ( strings_alloced > 0 ) {
      temp = realloc( strings, sizeof( *strings ) * new_alloced );
      if ( !temp ) {
	if ( strings ) free( strings );
	fprintf( stderr,
		 "Error allocating memory in parse_strings_from_line()\n" );
	return -1;
      }
    }
    else {
      temp = malloc( sizeof( *strings ) * new_alloced );
      if ( !temp ) {
	if ( strings ) free( strings );
	fprintf( stderr,
		 "Error allocating memory in parse_strings_from_line()\n" );
	return -1;
      }
    }
    strings = temp;
    strings_alloced = new_alloced;
    /* NULL-terminate it */
    strings[strings_seen] = NULL;
    /* Output it and return */
    *strings_out = strings;
    return 0;
  }
  else return -1;
}

/*
 * int post_path_comparator( void *left, void *right );
 *
 * Path comparator for rbtrees which sorts paths ahead of their prefixes
 */

int post_path_comparator( void *left, void *right ) {
  char *ls, *rs, *lbuf, *rbuf;
  char *lcmp, *rcmp, *ltmp, *rtmp;
  int result, temp;

  ls = (char *)left;
  rs = (char *)right;
  if ( ls && rs ) {
    lbuf = malloc( sizeof( *lbuf ) * ( strlen( ls ) + 1 ) );
    rbuf = malloc( sizeof( *rbuf ) * ( strlen( rs ) + 1 ) );
    if ( lbuf && rbuf ) {
      strcpy( lbuf, ls );
      strcpy( rbuf, rs );
      lcmp = get_path_component( lbuf, &ltmp );
      rcmp = get_path_component( rbuf, &rtmp );
      while ( 1 ) {
	if ( lcmp && rcmp ) {
	  temp = strcmp( lcmp, rcmp );
	  if ( temp > 0 ) {
	    /*
	     * rcmp is alphabetically prior to lcmp in the first
	     * component after a common prefix, so rs sorts first.
	     */
	    result = 1;
	    break;
	  }
	  else if ( temp < 0 ) {
	    /* As above, but ls sorts first. */
	    result = -1;
	    break;
	  }
	  else {
	    /* These components match; get the next ones */
	    lcmp = get_path_component( NULL, &ltmp );
	    rcmp = get_path_component( NULL, &rtmp );
	  }
	}
	else {
	  /* We ran out of data on at least one */
	  if ( lcmp ) {
	    /*
	     * We still have a left component, but no right component,
	     * so ls is a prefix of rs, and rs sorts first.
	     */
	    result = 1;
	  }
	  else if ( rcmp ) {
	    /* As above, but rs is a prefix of ls */
	    result = -1;
	  }
	  else {
	    /* We finished both simultaneously; they must be identical */
	    result = 0;
	  }
	  
	  /* We're done */
	  break;
	}
      }

      free( lbuf );
      free( rbuf );
    }
    else {
      /* Alloc error */
      fprintf( stderr,
	       "Warning: unable to allocagte memory in post_path_comparator()\n" );

      if ( lbuf ) free( lbuf );
      if ( rbuf ) free( rbuf );

      result = 0;
    }
  }
  else {
    if ( !ls && rs ) result = 1; /* NULL is a prefix of everything */
    else if ( ls && !rs ) result = -1;
    else result = 0;
  }

  return result;
}

/*
 * int pre_path_comparator( void *left, void *right );
 *
 * Path comparator for rbtrees which sorts paths after their prefixes.
 * This is the opposite of post_path_comparator().
 */

int pre_path_comparator( void *left, void *right ) {
  return post_path_comparator( right, left );
}

/*
 * char * read_line_from_file( FILE *fp );
 *
 * Read one line from a file into a newly allocated block of memory.
 */

#define INITIAL_LINE_ALLOC 16

char * read_line_from_file( FILE *fp ) {
  char *line, *temp;
  int num_chars, num_alloced, new_alloced;
  int c, eof;
  char ch;

  if ( fp ) {
    line = NULL;
    num_chars = num_alloced = 0;
    eof = 1;
    do {
      c = fgetc( fp );
      /* EOF, newline or NULL terminate a line */
      if ( !( c == EOF || c == '\n' || c == 0 ) ) ch = (char)c;
      else ch = 0;
      if ( ch != 0 ) {
	while ( !( num_alloced > 0 && num_chars < num_alloced ) ) {
	  /*
	   * We don't have enough allocated, reallocate as needed
	   */
	  if ( num_alloced > 0 ) {
	    /*
	     * We've already allocate some chars, resize it bigger
	     */
	    new_alloced = 2 * num_alloced;
	    temp = realloc( line, sizeof( *line ) * new_alloced );
	    if ( !temp ) {
	      fprintf( stderr,
		       "Error allocating memory in read_line_from_file()\n" );
	      free( line );
	      return NULL;
	    }
	    line = temp;
	    num_alloced = new_alloced;
	  }
	  else {
	    /*
	     * This is the first allocation
	     */
	    new_alloced = INITIAL_LINE_ALLOC;
	    temp = malloc( sizeof( *line ) * new_alloced );
	    if ( !temp ) {
	      fprintf( stderr,
		       "Error allocating memory in read_line_from_file()\n" );
	      return NULL;
	    }
	    line = temp;
	    num_alloced = new_alloced;
	  }
	}
	line[num_chars++] = ch;
      }
      else if ( c == EOF ) eof = 1;
    } while ( ch != 0 );
    if ( eof == 0 || num_chars > 0 ) {
      /*
       * Resize to fit the number of chars we actually got
       */
      temp = realloc( line, sizeof( *line ) * ( num_chars + 1 ) );
      if ( temp ) {
	line = temp;
	line[num_chars] = '\0';
      }
      else {
	fprintf( stderr,
		 "Error allocating memory in read_line_from_file()\n" );
	if ( line ) free( line );
	line = NULL;
      }
    }
    else {
      /*
       * We saw an EOF as the first char, return NULL because there's nothing
       * there.
       */
      if ( line ) free( line );
      line = NULL;
    }
    return line;
  }
  else return NULL;
}

/*
 * int read_symlink_target( const char *path, char **target_out );
 *
 * Read the target of a symlink into a newly allocated buffer.
 */

int read_symlink_target( const char *path, char **target_out ) {
  int status, result;
  struct stat st;
  char *target;

  status = READ_SYMLINK_SUCCESS;
  if ( path && target_out ) {
    result = lstat( path, &st );
    if ( result == 0 ) {
      if ( S_ISLNK( st.st_mode ) ) {
	/* Okay, path exists and is a symlink */

	target = malloc( sizeof( *target ) * ( st.st_size + 1 ) );
	if ( target ) {
	  result = readlink( path, target, st.st_size );
	  if ( result >= 0 ) {
	    target[result] = '\0';
	    *target_out = target;
	  }
	  else {
	    free( target );
	    status = READ_SYMLINK_READLINK_ERROR;
	  }
	}
	else status = READ_SYMLINK_MALLOC_ERROR;
      }
      else status = READ_SYMLINK_NOT_SYMLINK;
    }
    else status = READ_SYMLINK_LSTAT_ERROR;
  }
  else status = READ_SYMLINK_ERROR;

  return status;
}

/*
 * int recrm( const char *path );
 *
 * Recursively unlink a directory tree
 */

int recrm( const char *path ) {
  int status, result, len;
  struct stat st;
  DIR *d;
  struct dirent *de;
  char *s;

  result = 0;
  status = lstat( path, &st );
  if ( status == 0 ) {
    if ( S_ISDIR( st.st_mode ) ) {
      d = opendir( path );
      if ( d ) {
	len = strlen( path ) + NAME_MAX + 1;
	s = malloc( sizeof( *s ) * ( len + 1 ) );
	if ( s ) {
	  while ( de = readdir( d ) ) {
	    if ( strcmp( de->d_name, "." ) != 0 &&
		 strcmp( de->d_name, ".." ) != 0 ) {
	      snprintf( s, len, "%s/%s", path, de->d_name );
	      status = recrm( s );
	      if ( status != 0 && result == 0 ) result = status;
	    }
	  }
	  free( s );
	}
	else result = ENOMEM;
	status = closedir( d );
	if ( status != 0 ) result = errno;
      }
      else result = errno;
      status = rmdir( path );
      if ( status != 0 && result == 0 ) result = errno;
    }
    else {
      status = unlink( path );
      if ( status != 0 ) result = errno;
    }
  }
  else result = errno;

  return result;
}

/*
 * int rename_to_temp( const char *name );
 *
 * Rename the file name to a temporary.
 */

char * rename_to_temp( const char *name ) {
  char *temp;
  int len, status;

  temp = NULL;
  if ( name ) {
    len = strlen( name );
    len += 32; /* + 5 for .old. plus 10 for pid plus null, plus margin */
    temp = malloc( sizeof( *temp ) * len );
    if ( temp ) {
      snprintf( temp, len, "%s.old.%d", name, getpid() );
      status = link( name, temp );
      if ( status == 0 ) {
	status = unlink( name );
	if ( status != 0 ) {
	  fprintf( stderr, "Couldn't rename %s to %s: %s\n",
		   name, temp, strerror( errno ) );
	  unlink( temp );
	  free( temp );
	  temp = NULL;
	}
	/* else succeed and return temp */
      }
      else {
	fprintf( stderr, "Couldn't rename %s to %s: %s\n",
		 name, temp, strerror( errno ) );
	free( temp );
	temp = NULL;
      }
    }
  }

  return temp;
}

/*
 * int strlistlen( char **list );
 *
 * Return the length of a null-terminated list of strings
 */

int strlistlen( char **list ) {
  int len;

  if ( list ) {
    len = 0;
    while ( *list++ ) ++len;
    return len;
  }
  else return -1;
}

/*
 * int unlink_if_needed( const char *filename );
 *
 * Remove an existing file or symlink at a given filename, if one
 * exists, and return 1 if the filename is now clear.  Returns 0 if it
 * is unable to remove an existing object at that filename.
 */

int unlink_if_needed( const char *filename ) {
  int status, result;
  struct stat st;

  status = 0;
  if ( filename ) {
    result = lstat( filename, &st );
    if ( result == 0 ) {
      if ( S_ISREG( st.st_mode ) || S_ISLNK( st.st_mode ) ) {
	result = unlink( filename );
	if ( result == 0 ) {
	  /* Unlink succeeded */
	  status = 1;
	}
      }
      /* else can't remove directory or other type */
    }
    else {
      /* lstat() failed; why? */
      if ( errno == ENOENT ) {
	/* The path is already clear */
	status = 1;
      }
    }
  }

  return status;
}
