#include <pkg.h>

#include <sys/types.h>
#include <time.h>

#include <stdio.h>
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
  /* TODO */
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
    }    
  }

  return pkg;
}
