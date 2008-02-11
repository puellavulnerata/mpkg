#include <stdlib.h>
#include <string.h>

#include <pkg.h>

static void rbtree_free_subtree( rbtree *, rbtree_node * );
static rbtree_node * rbtree_get_first( rbtree_node * );
static rbtree_node * rbtree_get_next( rbtree_node * );
static int rbtree_query_node( rbtree_node *,
			      int (*)( void *, void * ),
			      void *, void ** );

rbtree * rbtree_alloc( int (*comparator)( void *, void * ),
		       void * (*copy_key)( void * ),
		       void (*free_key)( void * ), 
		       void * (*copy_val)( void * ),
		       void (*free_val)( void * ) ) {
  rbtree *t;

  if ( comparator ) {
    t = malloc( sizeof( *t ) );
    if ( t ) {
      t->root = NULL;
      t->comparator = comparator;
      t->copy_key = copy_key;
      t->free_key = free_key;
      t->copy_val = copy_val;
      t->free_val = free_val;
      t->count = 0;
    }
    return t;
  }
  else return NULL;
}

void * rbtree_enum( rbtree *t, rbtree_node *in,
		    void **vout, rbtree_node **nout ) {
  /*
   * This is just an inorder enumeration of the tree; if in is NULL,
   * we start from the beginning.  Otherwise, we get the next node
   * after in.  If out is not NULL, we write a pointer to the node to
   * it.  We return the key of that node.
   */
  rbtree_node *n;

  if ( in ) n = rbtree_get_next( in );
  else n = rbtree_get_first( t->root );
  if ( n ) {
    if ( nout ) *nout = n;
    if ( vout ) *vout = n->value;
    return n->key;
  }
}

void rbtree_free_subtree( rbtree *t, rbtree_node *n ) {
  if ( t && n ) {
    if ( n->left ) rbtree_free_subtree( t, n->left );
    if ( n->right ) rbtree_free_subtree( t, n->right );
    if ( t->free_val ) t->free_val( n->value );
    if ( t->free_key ) t->free_key( n->key );
    free( n );
  }
}

void rbtree_free( rbtree *t ) {
  if ( t ) {
    rbtree_free_subtree( t, t->root );
    free( t );
  }
}

static rbtree_node * rbtree_get_first( rbtree_node *n ) {
  if ( n ) {
    if ( n->left ) return rbtree_get_first( n->left );
    else return n;
  }
  else return NULL;
}

static rbtree_node * rbtree_get_next( rbtree_node *n ) {
  rbtree_node *temp;

  if ( n ) {
    /* 
     * If we have a right subtree, then the next node will be the
     * first in it.
     */
    if ( n->right ) return rbtree_get_first( n->right );
    /*
     * Else we're the last of this subtree, so move up until we find
     * something we're not the last node of.
     */
    while ( temp = n->up ) {
      /* If we were the left subtree, this is the next node */
      if ( temp->left == n ) return temp;
      else n = temp;
    }
  }
  else return NULL;
}

static int rbtree_query_node( rbtree_node *t,
			      int (*comparator)( void *, void * ),
			      void *key, void **val_out ) {
  int cmp;

  if ( val_out ) {
    if ( t ) {
      cmp = comparator( key, t->key );
      if ( cmp == 0 ) {
	*val_out = t->value;
	return RBTREE_SUCCESS;
      }
      else if ( cmp < 0 )
	return rbtree_query_node( t->left, comparator, key, val_out );
      else
     	return rbtree_query_node( t->right, comparator, key, val_out );
    }
    else return RBTREE_NOT_FOUND;
  }
  else return RBTREE_ERROR;
}

int rbtree_query( rbtree *t, void *key, void **val_out ) {
  int result;
  void *temp;

  if ( t && val_out ) {
    result = rbtree_query_node( t->root, t->comparator, key, &temp );
    if ( result == RBTREE_SUCCESS ) *val_out = temp;
    return result;
  }
  else return RBTREE_ERROR;
}

unsigned long rbtree_size( rbtree *t ) {
  if ( t ) return t->count;
  else return 0;
}

int rbtree_string_comparator( void *x, void *y ) {
  char *sx, *sy;
  int result;

  if ( x && y ) {
    sx = (char *)x;
    sy = (char *)y;
    result = strcmp( sx, sy );
    if ( result < 0 ) return -1;
    else if ( result == 0 ) return 0;
    else return 1;
  }
  else {
    if ( !x && y ) return 1;
    else if ( x && !y ) return -1;
    else return 0;
  }
}

void * rbtree_string_copier( void *key ) {
  char *s, *t;

  if ( key ) {
    s = (char *)key;
    t = copy_string( s );
    return t;
  }
  else return NULL;
}

void rbtree_string_free( void *key ) {
  free( key );
}
