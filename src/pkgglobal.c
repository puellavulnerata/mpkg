#include <pkg.h>

static char *temp;

void init_pkg_globals( void ) {
  temp = DEFAULT_TEMP_STRING;
}

const char * get_temp( void ) {
  return temp;
}

void set_temp( const char *temp_in ) {
  if ( temp_in ) {
    temp = malloc( sizeof( *temp ) * ( strlen( temp_in ) + 1 ) );
    if ( temp ) {
      strcpy( temp, temp_in );
    }
    else {
      fprintf( stderr,
	       "Warning: malloc() error while setting tempdir to \"%s\".\n",
	       temp_in );
      temp = DEFAULT_TEMP_STRING;
    }
  }
  else temp = DEFAULT_TEMP_STRING;
}
