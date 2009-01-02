#include <pkg.h>

static char *pkg = NULL;
static char *root = NULL;
static char *temp = NULL;

static char * adjust_path( const char * );

static char * adjust_path( const char *path_in ) {
  char *adjusted;
  char *cwd;

  adjusted = NULL;
  if ( path_in ) {
    if ( is_absolute( path_in ) ) {
      adjusted = canonicalize_and_copy( path_in );
    }
    else {
      cwd = get_current_dir();
      if ( cwd ) {
	adjusted = concatenate_paths( cwd, path_in );
	free( cwd );
      }
      /* else error, return NULL */
    }
  }
  /* else error, return NULL */

  return adjusted;
}

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
  char *tmp;

  if ( pkg_in ) {
    tmp = adjust_path( pkg_in );
    if ( tmp ) {
      if ( pkg && pkg != DEFAULT_PKG_STRING ) free( pkg );
      pkg = tmp;
    }
    else pkg = DEFAULT_PKG_STRING;
  }
  else pkg = DEFAULT_PKG_STRING;
}

const char * get_root( void ) {
  return root;
}

void set_root( const char *root_in ) {
  char *tmp;

  if ( root_in ) {
    tmp = adjust_path( root_in );
    if ( tmp ) {
      if ( root && root != DEFAULT_ROOT_STRING ) free( root );
      root = tmp;
    }
    else root = DEFAULT_ROOT_STRING;
  }
  else root = DEFAULT_ROOT_STRING;
}

const char * get_temp( void ) {
  return temp;
}

void set_temp( const char *temp_in ) {
  char *tmp;

  if ( temp_in ) {
    tmp = adjust_path( temp_in );
    if ( tmp ) {
      if ( temp && temp != DEFAULT_TEMP_STRING ) free( temp );
      temp = tmp;
    }
    else temp = DEFAULT_TEMP_STRING;
  }
  else temp = DEFAULT_TEMP_STRING;
}
