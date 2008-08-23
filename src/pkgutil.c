#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#include <pkg.h>

static char hex_digit_to_char( unsigned char );

char * copy_string( char *s ) {
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

void dbg_printf( char const *file, int line, char const *fmt, ... ) {
  va_list s;

  fprintf( stderr, "%s(%d): ", file, line );
  va_start( s, fmt );
  vfprintf( stderr, fmt, s );
  va_end( s );
  fprintf( stderr, "\n" );
}

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

int strlistlen( char **list ) {
  int len;

  if ( list ) {
    len = 0;
    while ( *list++ ) ++len;
    return len;
  }
  else return -1;
}
