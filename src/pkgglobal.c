#include <pkg.h>

static char *pkg = NULL;
static char *root = NULL;
static char *temp = NULL;

void free_pkg_globals( void ) {
  if ( pkg && pkg != DEFAULT_PKG_STRING ) {
    free( pkg );
    pkg = NULL;
  }
  if ( root && root != DEFAULT_ROOT_STRING ) {
    free( root );
    root = NULL;
  }
  if ( temp && temp != DEFAULT_TEMP_STRING ) {
    free( temp );
    temp = NULL;
  }
}

void init_pkg_globals( void ) {
  pkg = DEFAULT_PKG_STRING;
  root = DEFAULT_ROOT_STRING;
  temp = DEFAULT_TEMP_STRING;
}

const char * get_pkg( void ) {
  return pkg;
}

void set_pkg( const char *pkg_in ) {
  int status;

  if ( pkg_in ) {
    if ( pkg && pkg != DEFAULT_PKG_STRING ) free( pkg );
    pkg = malloc( sizeof( *pkg ) * ( strlen( pkg_in ) + 1 ) );
    if ( pkg ) {
      strcpy( pkg, pkg_in );
      status = canonicalize_path( pkg );
      if ( status != 0 ) {
	fprintf( stderr,
		 "Warning: couldn't canonicalize while setting pkgdir to \"%s\".\n",
		 pkg_in );
	free( pkg );
	pkg = DEFAULT_PKG_STRING;
      }
    }
    else {
      fprintf( stderr,
	       "Warning: malloc() error while setting pkgdir to \"%s\".\n",
	       pkg_in );
      pkg = DEFAULT_PKG_STRING;
    }
  }
  else pkg = DEFAULT_PKG_STRING;
}

const char * get_root( void ) {
  return root;
}

void set_root( const char *root_in ) {
  int status;

  if ( root_in ) {
    if ( root && root != DEFAULT_ROOT_STRING ) free( root );
    root = malloc( sizeof( *root ) * ( strlen( root_in ) + 1 ) );
    if ( root ) {
      strcpy( root, root_in );
      status = canonicalize_path( root );
      if ( status != 0 ) {
	fprintf( stderr,
		 "Warning: couldn't canonicalize while setting installroot to \"%s\".\n",
		 root_in );
	free( root );
	root = DEFAULT_ROOT_STRING;
      }
    }
    else {
      fprintf( stderr,
	       "Warning: malloc() error while setting installroot to \"%s\".\n",
	       root_in );
      root = DEFAULT_ROOT_STRING;
    }
  }
  else root = DEFAULT_ROOT_STRING;
}

const char * get_temp( void ) {
  return temp;
}

void set_temp( const char *temp_in ) {
  int status;

  if ( temp_in ) {
    if ( temp && temp != DEFAULT_TEMP_STRING ) free( temp );
    temp = malloc( sizeof( *temp ) * ( strlen( temp_in ) + 1 ) );
    if ( temp ) {
      strcpy( temp, temp_in );
      status = canonicalize_path( temp );
      if ( status != 0 ) {
	fprintf( stderr,
		 "Warning: couldn't canonicalize while setting tempdir to \"%s\".\n",
		 temp_in );
	free( temp );
	temp = DEFAULT_TEMP_STRING;
      }
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
