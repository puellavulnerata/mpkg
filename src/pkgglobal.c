#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <pkg.h>

static int check_md5;

static char *pkg = NULL;
static char *root = NULL;
static char *temp = NULL;

static char * adjust_path( const char * );

char * adjust_path_against_root( const char *path_in ) {
  char *temp, *adjusted;

  adjusted = NULL;
  temp = adjust_path( path_in );
  if ( temp ) {
    /*
     * So now temp is canonical and absolute, as is root.  If root is
     * a prefix of temp, then the rest of temp is the adjusted path.
     * if not, temp is not in root, so we return NULL.
     */
    adjusted = remove_path_prefix( temp, root );
    free( temp );
  }

  return adjusted;
}

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
#ifdef CHECK_MD5_DEFAULT
  check_md5 = 1;
#else
  check_md5 = 0;
#endif
  pkg = DEFAULT_PKG_STRING;
  root = DEFAULT_ROOT_STRING;
  temp = DEFAULT_TEMP_STRING;
}

int sanity_check_globals( void ) {
  char *old_cwd, *tmp;
  int result, status;

  status = 0;
  old_cwd = get_current_dir();
  if ( old_cwd ) {
    /* We must be able to chdir() to instroot and tempdir */
    result = chdir( root );
    if ( result == 0 ) {
      result = chdir( temp );
      if ( result == 0 ) {
	/*
	 * If instroot is not a prefix of pkgdir, we must be able to
	 * chdir() to pkgdir as well.  If instroot is a prefix of
	 * pkgdir, createdb is allowed to create pkgdir if it needs
	 * to.  Since instroot and pkgdir are canonicalized, path
	 * prefixity is equivalent to string prefixity.
	 */

	tmp = strstr( pkg, root );
	if ( tmp != pkg ) {
	  /* Not a prefix, need to check pkgdir too */

	  result = chdir( pkg );
	  if ( result != 0 ) {
	    fprintf( stderr,
		     "Couldn't chdir to pkgdir %s (and instroot %s isn't a prefix)\n",
		     pkg, root );
	    status = -1;
	  }
	}
      }
      else {
	fprintf( stderr,
		 "Couldn't chdir() to tempdir %s\n",
		 root );
	status = -1;
      }
    }
    else {
      fprintf( stderr,
	       "Couldn't chdir() to instroot %s\n",
	       root );
      status = -1;
    }

    chdir( old_cwd );
    free( old_cwd );
  }
  else {
    fprintf( stderr, "sanity_check_globals(): couldn't get cwd\n" );
    status = -1;
  }

  return status;
}

int get_check_md5( void ) {
  return check_md5;
}

void set_check_md5( int v ) {
  if ( v ) check_md5 = 1;
  else check_md5 = 0;
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
