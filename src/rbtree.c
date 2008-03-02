#include <stdlib.h>
#include <string.h>

#include <pkg.h>

static void rbtree_dump_node( rbtree_node *, int,
			      void (*)( void * ), void (*)( void * ) );
static void rbtree_dump_print_spaces( int );
static void rbtree_free_subtree( rbtree *, rbtree_node * );
static rbtree_node * rbtree_get_aunt( rbtree_node * );
static rbtree_node * rbtree_get_first( rbtree_node * );
static rbtree_node * rbtree_get_grandparent( rbtree_node * );
static rbtree_node * rbtree_get_next( rbtree_node * );
static rbtree_node * rbtree_get_parent( rbtree_node * );
static rbtree_node * rbtree_get_sister( rbtree_node * );
static int rbtree_insert_node( rbtree *, rbtree_node *,
			       rbtree_node **,
			       void *, void * );
static void rbtree_insert_post( rbtree *, rbtree_node * );
static int rbtree_query_node( rbtree_node *,
			      int (*)( void *, void * ),
			      void *, void ** );
static void rbtree_rotate_left( rbtree *, rbtree_node * );
static void rbtree_rotate_right( rbtree *, rbtree_node * );
static int rbtree_validate_internal( rbtree_node *, int, int *,
				     int (*)( void *, void * ) );

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

static void rbtree_dump_node( rbtree_node *n, int depth,
			      void (*key_printer)( void * ),
			      void (*val_printer)( void * ) ) {
  rbtree_dump_print_spaces( 2 * depth );
  printf( "node n at %p, depth %d:\n", n, depth );
  rbtree_dump_print_spaces( 2 * depth + 1 );
  printf( "n->key = %p", n->key );
  if ( key_printer ) {
    printf( " \"" );
    key_printer( n->key );
    printf( "\"" );
  }
  printf( ", n->value = %p", n->value );
  if ( val_printer ) {
    printf( " \"" );
    val_printer( n->value );
    printf( "\"" );    
  }
  printf( "\n" );
  rbtree_dump_print_spaces( 2 * depth + 1 );
  printf( "n->up = %p, n->left = %p, n->right = %p\n",
	  n->up, n->left, n->right );
  rbtree_dump_print_spaces( 2 * depth + 1 );
  if ( n->color == RED )
    printf( "n->color = RED\n" );
  else if ( n->color == BLACK )
    printf( "n->color = BLACK\n" );
  else
    printf( "n->color = UNKNOWN (bad!)\n" );
  if ( n->left )
    rbtree_dump_node( n->left, depth + 1, key_printer, val_printer );
  if ( n->right )
    rbtree_dump_node( n->right, depth + 1, key_printer, val_printer );
}

static void rbtree_dump_print_spaces( int n ) {
  int i;

  for ( i = 0; i < n; ++i ) printf( " " );
}

void rbtree_dump( rbtree *t,
		  void (*key_printer)( void * ),
		  void (*val_printer)( void * ) ) {
  if ( t->root ) {
    rbtree_dump_node( t->root, 0, key_printer, val_printer );
  }
  else printf( "The tree is empty\n" );
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
  else {
    if ( nout ) *nout = NULL;
    if ( vout ) *vout = NULL;
    return NULL;
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

static rbtree_node * rbtree_get_aunt( rbtree_node *n ) {
  rbtree_node *g, *p;

  if ( n ) {
    if ( n->up ) {
      p = n->up;
      if ( n->up->up ) {
	g = n->up->up;
	if ( g->right == p ) return g->left;
	else if ( g->left == p ) return g->right;
	else {
	  fprintf( stderr, "Inconsistency seen in rbtree_get_aunt( %p )\n",
		   n );
	  return NULL;
	}
      }
      else return NULL;
    }
    else return NULL;
  }
  else return NULL;
}

static rbtree_node * rbtree_get_first( rbtree_node *n ) {
  if ( n ) {
    if ( n->left ) return rbtree_get_first( n->left );
    else return n;
  }
  else return NULL;
}

static rbtree_node * rbtree_get_grandparent( rbtree_node *n ) {
  if ( n ) {
    if ( n->up ) return n->up->up;
    else return NULL;
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
    do {
      temp = n->up;
      if ( temp ) {
	/* If we were the left subtree, this is the next node */
	if ( temp->left == n ) return temp;
	else n = temp;
      }
    } while ( temp );
    return NULL;
  }
  else return NULL;
}

static rbtree_node * rbtree_get_parent( rbtree_node *n ) {
  if ( n ) return n->up;
  else return NULL;
}

static rbtree_node * rbtree_get_sister( rbtree_node *n ) {
  if ( n ) {
    if ( n->up ) {
      if ( n->up->left == n ) return n->up->right;
      else if ( n->up->right == n ) return n->up->left;
      else {
	fprintf( stderr, "Inconsistency seen in rbtree_get_sister( %p )\n",
		 n );
	return NULL;
      }
    }
    else return NULL;
  }
  else return NULL;
}

static int rbtree_insert_node( rbtree *t, rbtree_node *parent,
			       rbtree_node **n,
			       void *k, void *v ) {
  rbtree_node *tmp;
  int c, result;

  if ( t && n ) {
    if ( *n == NULL ) {
      /* insert it here */
      tmp = malloc( sizeof( *tmp ) );
      if ( tmp ) {
	tmp->key = k;
	tmp->value = v;
	tmp->color = RED;
	tmp->left = NULL;
	tmp->right = NULL;
	tmp->up = parent;
	*n = tmp;
	rbtree_insert_post( t, *n );
      }
      else return RBTREE_ERROR;
    }
    else {
      c = t->comparator( k, (*n)->key );
      if ( c < 0 )
	/* Insert it in the left subtree */
	result = rbtree_insert_node( t, *n, &((*n)->left), k, v );
      else if ( c == 0 ) {
	/* Replace this value and key */
	if ( t->free_key ) t->free_key( (*n)->key );
	(*n)->key = k;
	if ( t->free_val ) t->free_val( (*n)->value );
	(*n)->value = v;
	result = RBTREE_SUCCESS;
      }
      else
	/* Insert it in the right subtree */
	result = rbtree_insert_node( t, *n, &((*n)->right), k, v );
    }
  }
  else return RBTREE_ERROR;
}

static void rbtree_insert_post( rbtree *t, rbtree_node *n ) {
  /*
   * Post-process rbtree insert to make sure all the properties still hold
   */
  rbtree_node *aunt, *grandparent, *parent;

  if ( n->up ) {
    /*
     * If the node's parent's color is black, we're okay (the node
     * just inserted is red)
     */

    if ( n->up->color != BLACK ) {
      /*
       * The new node and its parent are both red, so we need to fix
       * this.  If the parent and aunt are both red, we can change
       * them to black and the grandparent to red, so we'll have to
       * recursively fixup the grandparent.  If the aunt is black,
       * then it's rotation time.
       */

      aunt = rbtree_get_aunt( n );
      grandparent = rbtree_get_grandparent( n );
      if ( grandparent ) {
	if ( aunt && aunt->color == RED ) {
	  n->up->color = BLACK;
	  aunt->color = BLACK;
	  grandparent->color = RED;
	  /* fprintf( stderr, "rbtree_insert_post(): case 3 (%p)\n", n ); */
	  rbtree_insert_post( t, grandparent );
	}
	else {
	  /*
	   * Rotation:
	   *
	   * There are four possible cases here, depending on whether
	   * n is the left or right child of its parent, and whether
	   * the parent is the left or right child of its grandparent.
	   */

	  if ( n->up->left == n ) {
	    if ( n->up == grandparent->left ) {
	      /* fprintf( stderr, "rbtree_insert_post(): case 6 (%p)\n", n ); */
	      n->up->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_right( t, grandparent );
	    }
	    else if ( n->up == grandparent->right ) {
	      /* A right rotation will fix things up */
	
	      /* fprintf( stderr, "rbtree_insert_post(): case 4 (%p)\n", n ); */
	      rbtree_rotate_right( t, n->up );
	      n->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_left( t, grandparent );
	    }
	    else {
	      fprintf( stderr, "Inconsistency seen in rbtree_insert_post( %p ): parent not child of grandparent\n",
		       n );
	    }
	  }
	  else if ( n->up->right == n ) {
	    if ( n->up == grandparent->left ) {
	      /* A left rotation will fix things up */

	      /* fprintf( stderr, "rbtree_insert_post(): case 5 (%p)\n", n ); */
	      rbtree_rotate_left( t, n->up );
	      n->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_right( t, grandparent );
	    }
	    else if ( n->up == grandparent->right ) {
	      n->up->color = BLACK;
	      grandparent->color = RED;

	      /* fprintf( stderr, "rbtree_insert_post(): case 7 (%p)\n", n ); */
	      rbtree_rotate_left( t, grandparent );
	    }
	    else {
	      fprintf( stderr, "Inconsistency seen in rbtree_insert_post( %p ): parent not child of grandparent\n",
		       n );
	    }
	  }
	  else {
	    fprintf( stderr, "Inconsistency seen in rbtree_insert_post( %p ): node not child of parent.\n",
		     n );
	  }
	}
      }
      else {
	/*
	 * If this happens, then the parent was the root, so it should
	 * have been black.  We have a broken tree.
	 */

	fprintf( stderr,
		 "Inconsistency seen in rbtree_insert_post( %p ): missing grandparent\n",
		 n );
      }
    }
    /*
    else {
      fprintf( stderr, "rbtree_insert_post(): case 2 (%p)\n", n );
    }
    */
  }
  else {
    /*
     * This is the root, make sure it's black and we're done
     */

    /* fprintf( stderr, "rbtree_insert_post(): case 1 (%p)\n", n ); */
    n->color = BLACK;
  }
}

int rbtree_insert( rbtree *t, void *key, void *val ) {
  int result, status;
  void *k, *v;

  status = RBTREE_SUCCESS;
  if ( t && key ) {
    if ( t->copy_key && t->free_key ) k = t->copy_key( key );
    else k = key;
    if ( ( key && k ) || ( !key && !k ) ) {
      if ( t->copy_val && t->free_val ) v = t->copy_val( val );
      else v = val;
      if ( ( val && v ) || ( !val && !v ) ) {
	result = rbtree_insert_node( t, NULL, &(t->root), k, v );
	if ( result != RBTREE_SUCCESS ) {
	  if ( k != key && t->free_key ) t->free_key( k );
	  if ( v != val && t->free_val ) t->free_val( v );
	}
      }
      else {
	if ( k != key && t->free_key ) t->free_key( k );
	status = RBTREE_ERROR;
      }
    }
    else status = RBTREE_ERROR;
  }
  else status = RBTREE_ERROR;
  return status;
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

static void rbtree_rotate_left( rbtree *t, rbtree_node *n ) {
  rbtree_node *child, *parent;

  if ( t && n ) {
    if ( n->right ) {
      parent = n->up;
      child = n->right;
      n->right = child->left;
      if ( n->right ) n->right->up = n;
      n->up = child;
      child->left = n;
      if ( child->left ) child->left->up = child;
      child->up = parent;
      if ( parent ) {
	if ( parent->left == n ) parent->left = child;
	else if ( parent->right == n ) parent->right = child;
	else {
	  fprintf( stderr,
		   "Inconsistency seen in rbtree_rotate_left( %p )\n", n );
	}
      }
      else t->root = child;
    }
    else {
      fprintf( stderr, "rbtree_rotate_left(): no right child (%p)\n", n );
    }
  }
}

static void rbtree_rotate_right( rbtree *t, rbtree_node *n ) {
  rbtree_node *child, *parent;

  if ( t && n ) {
    if ( n->left ) {
      parent = n->up;
      child = n->left;
      n->left = child->right;
      if ( n->left ) n->left->up = n;
      n->up = child;
      child->right = n;
      if ( child->right ) child->right->up = child;
      child->up = parent;
      if ( parent ) {
	if ( parent->left == n ) parent->left = child;
	else if ( parent->right == n ) parent->right = child;
	else {
	  fprintf( stderr, "Inconsistency seen in rbtree_rotate_right( %p )\n", n );
	}
      }
      else t->root = child;
    }
    else {
      fprintf( stderr, "rbtree_rotate_right(): no left child (%p)\n", n );
    }
  }
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

void rbtree_string_printer( void *s ) {
  if ( s ) printf( "%s", (char *)s );
  else printf( "(null)" );
}

static int rbtree_validate_internal( rbtree_node *n, int depth,
				     int *black_nodes_to_leaves,
				     int (*comparator)( void *, void * ) ) {
  int left_black_nodes_to_leaves, right_black_nodes_to_leaves;
  int child_black_nodes_to_leaves;
  int result;

  if ( n ) {
    if ( depth == 0 ) {
      /* The root node must be black */
      if ( n->color != BLACK ) {
	fprintf( stderr, "Validation failed (%p): the root node must be black.\n",
		 n );
	return 0;
      }
      /* The root node has no parent */
      if ( n->up ) {
	fprintf( stderr, "Validation failed (%p): the root node must not have a parent.\n",
		 n );
	return 0;
      }
    }
    else {
      if ( n->up ) {
	/* The node must be a child of its parent */
	if ( !( n->up->left == n || n->up->right == n ) ) {
	  fprintf( stderr, "Validation failed (%p): a node must be a child of its parent.\n",
		   n );
	  return 0;
	}
      }
      else {
	/* The node must have a parent */

	fprintf( stderr, "Validation failed (%p): a non-root node must have a parent.\n",
		 n );
	return 0;
      }
    }
    /* The color must be red or black */
    if ( !( n->color == RED || n->color == BLACK ) ) {
      fprintf( stderr, "Validation failed (%p): a node must be either red or black.\n",
	       n );

      return 0;
    }
    /* The children of a red node must be black */
    if ( n->color == RED ) {
      if ( n->left ) {
	if ( n->left->color != BLACK ) {
	  fprintf( stderr, "Validation failed (%p): the left child of a red node must be black.\n",
		   n );
	  return 0;
	}
      }
      if ( n->right ) {
	if ( n->right->color != BLACK ) {
	  fprintf( stderr, "Validation failed (%p): the right child of a red node must be black.\n",
		   n );
	  return 0;
	}
      }
    }
    /* The left child of a node must be less than that node */
    if ( n->left ) {
      result = comparator( n->left->key, n->key );
      if ( result >= 0 ) {
	fprintf( stderr, "Validation failed (%p): the left child of a node must be less than that node.\n",
		 n );
	return 0;
      }
    }
    /* The right child of a node must be greater than that node */
    if ( n->right ) {
      result = comparator( n->key, n->right->key );
      if ( result >= 0 ) {
	fprintf( stderr, "Validation failed (%p): the right child of a node must be greater than that node.\n",
		 n );
	return 0;
      }
    }
    /* The left and right subtrees must be valid */
    if ( n->left ) {
      result = rbtree_validate_internal( n->left, depth + 1,
					 &left_black_nodes_to_leaves,
					 comparator );
      if ( result == 0 ) {
	fprintf( stderr, "Validation failed (%p): the left subtree of a node must be valid.\n",
		 n );
	return 0;
      }
    }
    else left_black_nodes_to_leaves = 0;
    if ( n->right ) {
      result = rbtree_validate_internal( n->right, depth + 1,
					 &right_black_nodes_to_leaves,
					 comparator );
      if ( result == 0 ) {
	fprintf( stderr, "Validation failed (%p): the right subtree of a node must be valid.\n",
		 n );
	return 0;
      }
    }
    else right_black_nodes_to_leaves = 0;
    /*
     * There must be the same number of black nodes to the leaves in
     * both subtrees.
     */
    if ( left_black_nodes_to_leaves != right_black_nodes_to_leaves ) {
      fprintf( stderr, "Validation failed (%p): the left and right subtrees of a node must be have the same number of black nodes to the leaves (%d, %d).\n",
	       n, left_black_nodes_to_leaves, right_black_nodes_to_leaves );
      return 0;
    }
    child_black_nodes_to_leaves = left_black_nodes_to_leaves;
    if ( black_nodes_to_leaves ) {
      *black_nodes_to_leaves = child_black_nodes_to_leaves +
	( ( n->color == BLACK ) ? 1 : 0 );
    }
    return 1;
  }
  else {
    /* A NULL node is not valid */
    fprintf( stderr, "Validation failed (%p): a node must not be NULL.\n",
	     n );
    return 0;
  }
}

int rbtree_validate( rbtree *t ) {
  int black_nodes_to_leaves, result;

  if ( t ) {
    /* We must have a comparator */
    if ( !(t->comparator) ) {
      fprintf( stderr, "Validation failed: a tree must have a comparator\n" );
      return 0;
    }
    if ( t->root ) {
      result = rbtree_validate_internal( t->root, 0, &black_nodes_to_leaves,
				       t->comparator );
      if ( result == 1 ) {
	/* fprintf( stderr, "Validation succeeded (%d)\n",
	            black_nodes_to_leaves ); */
      }
      return result;
    }
    /* It's an empty tree; that's okay */
    else {
      /* fprintf( stderr, "Validation succeeded: empty tree\n" ); */
      return 1;
    }
  }
  else {
    fprintf( stderr, "Validation failed: a tree must not be NULL\n" );
    return 0;
  }
}
