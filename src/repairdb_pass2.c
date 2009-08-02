#include <pkg.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char * resolve_claim_check_content( claims_list_t * );
static char * resolve_claim( claims_list_t * );

#define STR_BUF_LEN 80

rbtree * repairdb_pass_two( claims_list_map_t *m, char content_checking ) {
  rbtree *t;
  claims_list_t *l;
  void *n;
  char *pkg;
  int result;
  unsigned long count;
  char error;
  char buf[STR_BUF_LEN];
  int i, prev_chars_displayed;

  t = NULL;
  if ( m ) {
    t = rbtree_alloc( rbtree_string_comparator,
		      rbtree_string_copier, rbtree_string_free,
		      rbtree_string_copier, rbtree_string_free );
    if ( t ) {
      prev_chars_displayed = 0;
      count = 0;
      error = 0;
      n = NULL;
      printf( "Resolving claims: " );
      do {
	l = NULL;
	n = enumerate_claims_list_map( m, n, &l );
	if ( l ) {
	  if ( prev_chars_displayed > 0 ) {
	    for ( i = 0; i < prev_chars_displayed; ++i ) putchar( '\b' );   
	  }
	  snprintf( buf, STR_BUF_LEN, "%lu / %lu", count, m->num_locations );
	  prev_chars_displayed = strlen( buf );
	  printf( "%s", buf );

	  if ( content_checking ) {
	    pkg = resolve_claim_check_content( l );
	  }
	  else {
	    pkg = resolve_claim( l );
	  }

	  if ( pkg ) {
	    result = rbtree_insert( t, l->location, pkg );
	    if ( result != RBTREE_SUCCESS ) {
	      /*
	       * Get to the next line, since we aren't displaying the
	       * counter any more.
	       */
	      printf( "\n" );
	      fprintf( stderr, "Error in pass two: couldn't insert claim" );
	      fprintf( stderr, " by %s for %s into rbtree\n",
		       pkg, l->location );
	    }
	  }
	  /* else no claim was upheld, so drop it */

	  if ( !error ) ++count;
	}
      } while ( n && !error );

      if ( error ) {
	rbtree_free( t );
	t = NULL;
      }
      /* Stop displaying the counter and get to the next line */
      else printf( "\n" );
    }
  }

  return t;
}

static char * resolve_claim_check_content( claims_list_t *l ) {
  /*
   * This is the content-aware resolver.  We scan over the list
   * looking for claims which match the file on disk.  If we find more
   * than one, we break ties with the mtime of the
   * package-description.
   */
  char *pkg;
  claims_list_node_t *n;
  time_t most_recent;
  /* Full path to file */
  char *full_path;
  /*
   * Keep track of the stat() results and hash for the file.  Compute
   * these on demand, so we can avoid computing the expensive MD5 if
   * there are no file claims, but only compute them at most once.
   */
  int have_stat, no_stat;
  struct stat st;
  int have_hash, no_hash;
  uint8_t hash[HASH_LEN];
  char *target;
  int match, result;

  pkg = NULL;
  if ( l ) {
    /*
     * Initialize all these to indicate they are not available yet,
     * since we compute them on demand in the loop.
     */
    full_path = NULL;
    have_hash = 0;
    have_stat = 0;
    target = NULL;
    n = l->head;
    while ( n ) {
      /*
       * If we have an existing matching claim, don't bother to check
       * unless this one is more recent.
       */
      if ( !pkg || ( pkg && n->c.pkg_descr_mtime > most_recent ) ) {
	/* Test if this claim matches */

	match = 0;

	if ( !full_path ) {
	  full_path = concatenate_paths( get_root(), l->location );
	  if ( !full_path ) {
	    fprintf( stderr,
		     "Warning: couldn't allocate memory resolving claims " );
	    fprintf( stderr, "for location %s, dropping out early.\n",
		     l->location );
	    break;
	  }
	}

	if ( !have_stat ) {
	  result = lstat( full_path, &st );
	  have_stat = 1;
	  if ( result == 0 ) no_stat = 0;
	  else no_stat = 1;
	}

	switch ( n->c.claim_type ) {
	case CLAIM_DIRECTORY:
	  /*
	   * Directories are simplest; it matches the claim if it's a
	   * directory at all.
	   */
	  if ( !no_stat && S_ISDIR( st.st_mode ) ) match = 1;
	  break;
	case CLAIM_FILE:
	  /*
	   * For files, it matches if the in-filesystem location is a
	   * file too, and the mtimes or MD5s, depending on
	   * get_check_md5(), match.
	   */
	  if ( !no_stat && S_ISREG( st.st_mode ) ) {
	    if ( get_check_md5() ) {
	      /* If we're checking MD5s */
	      if ( !have_hash ) {
		/* If we don't have a hash for this file, compute one */
		result = get_file_hash( full_path, hash );
		have_hash = 1;
		if ( result == 0 ) no_hash = 0;
		else no_hash = 1;
	      }

	      if ( have_hash && !no_hash ) {
		if ( memcmp( n->c.u.f.hash, hash, sizeof( hash ) ) == 0 ) {
		  match = 1;
		}
	      }
	    }
	    else {
	      if ( st.st_mtime == n->c.pkgtime ) match = 1;
	    }
	  }
	  break;
	case CLAIM_SYMLINK:
	  /*
	   * For symlinks, it matches if the in-filesystem location is
	   * a symlink too and the targets match.
	   */
	  if ( !no_stat && S_ISLNK( st.st_mode ) ) {
	    if ( !target ) {
	      result = read_symlink_target( full_path, &target );
	      if ( result != READ_SYMLINK_SUCCESS ) target = NULL;
	    }

	    if ( target ) {
	      if ( strcmp( n->c.u.s.target, target ) == 0 ) match = 1;
	    }
	  }
	  break;
	default:
	  /* Weird claim, can't match anything */
	  break;
	}

	if ( match ) {
	  pkg = n->c.pkg_name;
	  most_recent = n->c.pkg_descr_mtime;
	}
      }

      n = n->next;
    }

    if ( full_path ) free( full_path );
    if ( target ) free( target );
  }

  return pkg;
}

static char * resolve_claim( claims_list_t *l ) {
  /*
   * The non-contentful resolver is simple: we just scan over the list
   * and pick the claim with the most recent package-description
   * mtime.
   */
  char *pkg = NULL;
  claims_list_node_t *n;
  time_t most_recent;

  if ( l ) {
    n = l->head;
    while ( n ) {
      if ( pkg ) {
	/* This one gets it if it's more recent */
	if ( n->c.pkg_descr_mtime > most_recent ) {
	  pkg = n->c.pkg_name;
	  most_recent = n->c.pkg_descr_mtime;	  
	}
      }
      else {
	/* No existing claim, so this one gets it */
	pkg = n->c.pkg_name;
	most_recent = n->c.pkg_descr_mtime;
      }

      /* Advance to next one */
      n = n->next;
    }
  }

  return pkg;
}
