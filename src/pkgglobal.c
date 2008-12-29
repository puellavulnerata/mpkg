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
  int status, result;
  char *old_cwd;

  if ( pkg_in ) {
    old_cwd = get_current_dir();
    if ( old_cwd ) {
      result = chdir( pkg_in );
      if ( result == 0 ) {
	if ( pkg && pkg != DEFAULT_PKG_STRING ) free( pkg );
	pkg = get_current_dir();
	if ( pkg ) {
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
		   "Warning: get_current_dir() failed while setting pkgdir.\n" );
	  pkg = DEFAULT_PKG_STRING;
	}
	chdir( old_cwd );
      }
      free( old_cwd );
    }
    else {
      fprintf( stderr,
	       "Warning: get_current_dir() failed while setting pkgdir.\n" );
      pkg = DEFAULT_PKG_STRING;
    }
  }
  else pkg = DEFAULT_PKG_STRING;
}

const char * get_root( void ) {
  return root;
}

void set_root( const char *root_in ) {
  int status, result;
  char *old_cwd;

  if ( root_in ) {
    old_cwd = get_current_dir();
    if ( old_cwd ) {
      result = chdir( root_in );
      if ( result == 0 ) {
	if ( root && root != DEFAULT_ROOT_STRING ) free( root );
	root = get_current_dir();
	if ( root ) {
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
		   "Warning: get_current_dir() failed while setting installroot.\n" );
	  root = DEFAULT_ROOT_STRING;
	}
	chdir( old_cwd );
      }
      free( old_cwd );
    }
    else {
      fprintf( stderr,
	       "Warning: get_current_dir() failed while setting installroot.\n" );
      root = DEFAULT_ROOT_STRING;
    }
  }
  else root = DEFAULT_ROOT_STRING;
}

const char * get_temp( void ) {
  return temp;
}

void set_temp( const char *temp_in ) {
  int status, result;
  char *old_cwd;

  if ( temp_in ) {
    old_cwd = get_current_dir();
    if ( old_cwd ) {
      result = chdir( temp_in );
      if ( result == 0 ) {
	if ( temp && temp != DEFAULT_TEMP_STRING ) free( temp );
	temp = get_current_dir();
	if ( temp ) {
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
		   "Warning: get_current_dir() failed while setting tempdir.\n" );
	  temp = DEFAULT_TEMP_STRING;
	}
	chdir( old_cwd );
      }
      free( old_cwd );
    }
    else {
      fprintf( stderr,
	       "Warning: get_current_dir() failed while setting tempdir.\n" );
      temp = DEFAULT_TEMP_STRING;
    }
  }
  else temp = DEFAULT_TEMP_STRING;
}
