#include <stdlib.h>
#include <string.h>

#include <pkg.h>

#undef RBTREE_DEBUG

#define rberr(...) dbg_printf( __FILE__, __LINE__, __VA_ARGS__ )

#ifdef RBTREE_DEBUG
#define rbdbg(...) dbg_printf( __FILE__, __LINE__, __VA_ARGS__ )
#else
#define rbdbg(...)
#endif

static void rbtree_clear_key_and_value( rbtree *, rbtree_node * );
static int rbtree_delete_and_fixup( rbtree *, rbtree_node * );
static int rbtree_delete_node( rbtree *, rbtree_node *, void *, void ** );
static int rbtree_delete_rebalance( rbtree *, rbtree_node *, rbtree_node * );
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
			       void *, void *,
			       int );
static void rbtree_insert_post( rbtree *, rbtree_node * );
static int rbtree_query_node( rbtree_node *,
			      int (*)( void *, void * ),
			      void *, void ** );
static int rbtree_replace_node( rbtree *, rbtree_node *, rbtree_node * );
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

static void rbtree_clear_key_and_value( rbtree *t, rbtree_node *n ) {
  if ( t && n ) {
    if ( n->key ) {
      if ( t->copy_key && t->free_key ) t->free_key( n->key );
      n->key = NULL;
    }
    if ( n->value ) {
      if ( t->copy_val && t->free_val ) t->free_val( n->value );
      n->value = NULL;
    }    
  }
}

static int rbtree_delete_and_fixup( rbtree *t, rbtree_node *n ) {
  /*
   * This function is called by rbtree_delete_node().  The node n has
   * zero or one children, and its key and value have already been
   * cleared.  We have to delete it and fix up the tree invariants.
   *
   * We have the following cases to consider:
   *
   * Case 1: n is the root node.  It must necessarily be black.  If a
   * child exists, it may be either red or black.  If it is red, we
   * can make it black and make it the new root without violating any
   * invariants, since all paths to leaves pass through it.
   *
   * Case 2: n is red.  Its child, if one exists, must necessarily be
   * black.  We can delete it and replace it with its child, if any,
   * without breaking any invariants.
   *
   * Case 3: n is black, and its child exists and is red.  We can
   * remove n and replace it with its child, and turn the child black
   * to keep all the needed invariants.
   *
   * Remaining cases: n is black, not the root, and has no children or
   * a black child.  This is the complex case with rotations and
   * recursion.  Since n is not the root, it has a parent p.  Since n
   * is black, it must have a sister s, or property 3 would fail.  In
   * all cases, we start by deleting n and replacing it with its child
   * (or just deleting it if no child exists), and then call
   * rbtree_delete_rebalance() to rebalance it.
   */

  rbtree_node **parent_ptr;
  rbtree_node *child, *parent, *sister;
  int status;

  status = RBTREE_SUCCESS;
  if ( t && n ) {
    /* n has at most one child; we keep it here */
    if ( n->left ) child = n->left;
    else if ( n->right ) child = n->right;
    else child = NULL;

    if ( n->up == NULL ) {
      /* Case 1: n is the root */

      rbdbg( "rbtree_delete_and_fixup( %p ): case 1, n is the root\n", n );

      if ( t->root == n ) {
	if ( child ) {
	  child->up = NULL;
	  child->color = BLACK;
	  t->root = child;
	}
	else t->root = NULL;
	free( n );
      }
      else status = RBTREE_ERROR;
    }
    else {
      /* n is not the root */

      parent = n->up;
      if ( n->up->left == n ) parent_ptr = &(n->up->left);
      else if ( n->up->right == n ) parent_ptr = &(n->up->right);
      else status = RBTREE_ERROR;

      if ( status == RBTREE_SUCCESS ) {
	if ( n->color == RED ) {

	  rbdbg( "rbtree_delete_and_fixup( %p ): case 2, n is not the root and is red\n",
		 n );

	  /* Case 2: n is red */
	  if ( child ) {
	    *parent_ptr = child;
	    child->up = parent;
	  }
	  else *parent_ptr = NULL;
	  free( n );
	}
	else if ( n->color == BLACK ) {
	  if ( child && child->color == RED ) {

	    rbdbg( "rbtree_delete_and_fixup( %p ): case 3, n is not the root and is black and has a red child\n",
		   n );

	    /* Case 3: n is black, and its child exists and is red */
	    *parent_ptr = child;
	    child->up = parent;
	    child->color = BLACK;
	    free( n );
	  }
	  else {

	    rbdbg( "rbtree_delete_and_fixup( %p ): n is not the root and is black and has a black child, we call rbtree_delete_rebalance()\n", n );
	    
	    *parent_ptr = child;
	    if ( child ) {
	      child->up = parent;
	      child->color = BLACK;
	    }
	    free( n );

	    rbdbg( "rbtree_delete_and_fixup( %p ): deleted n and put its child in its place, about to call rbtree_delete_rebalance()\n",
		   n );

	    status = rbtree_delete_rebalance( t, parent, child );
	  }
	}
	else status = RBTREE_ERROR;
      }
    }
  }
  else status = RBTREE_ERROR;
  return status;
}

static int rbtree_delete_node( rbtree *t, rbtree_node *n,
			       void *k, void **vout ) {
  int status, result;
  rbtree_node *ntmp;
  void *v;

  status = RBTREE_SUCCESS;
  if ( t ) {
    if ( n ) {
      result = t->comparator( k, n->key );
      if ( result < 0 ) {
	/*
	 * The node, if it exists, will be in the left subtree
	 */

	result = rbtree_delete_node( t, n->left, k, vout );
	if ( result != RBTREE_SUCCESS ) status = result;
      }
      else if ( result == 0 ) {
	/*
	 * This node matches.  If it has two children, then we can
	 * pull a value out of one side into this node, and reduce the
	 * problem to deleting a node with zero or one children.  Once
	 * we have such a node, we can call rbtree_delete_and_fixup()
	 * on it.
	 */

	if ( vout ) {
	  if ( n->value ) {
	    if ( t->copy_val && t->free_val ) {
	      v = t->copy_val( n->value );
	      if ( v ) *vout = v;
	      else status = RBTREE_ERROR;
	    }
	    else *vout = n->value;
	  }
	  else *vout = NULL;
	}
	if ( status == RBTREE_SUCCESS ) {
	  /*
	   * If we have a left and right subtree, we have to pick a
	   * node from one of the subtrees to move the key and value
	   * from to this node, and then rbtree_delete_and_fixup() on
	   * that node.  Otherwise, we can directly
	   * rbtree_delete_and_fixup() on this node.
	   */

	  if ( n->left && n->right ) {
	    /*
	     * We could pick either the last node of the left subtree,
	     * or the first node of the right subtree here.  Since we
	     * already have rbtree_get_first(), we'll go with the
	     * latter.
	     */

	    ntmp = rbtree_get_first( n->right );
	    if ( ntmp ) {
	      /*
	       * Clear out n, move the key and value from ntmp to it,
	       * and then call rbtree_delete_and_fixup() on ntmp.
	       */

	      rbtree_clear_key_and_value( t, n );
	      n->key = ntmp->key;
	      n->value = ntmp->value;
	      ntmp->key = NULL;
	      ntmp->value = NULL;
	      result = rbtree_delete_and_fixup( t, ntmp );
	      if ( result != RBTREE_SUCCESS ) status = result;
	      if ( status == RBTREE_SUCCESS ) --(t->count);
	    }
	    else {
	      /*
	       * This shouldn't happen, return an error and give up.
	       */

	      status = RBTREE_ERROR;
	    }
	  }
	  else {
	    /*
	     * Clear out n and call rbtree_delete_and_fixup() on it.
	     */

	    rbtree_clear_key_and_value( t, n );
	    result = rbtree_delete_and_fixup( t, n );
	    if ( result != RBTREE_SUCCESS ) status = result;
	    if ( status == RBTREE_SUCCESS ) --(t->count);
	  }
	}
      }
      else {
	/*
	 * The node, if it exists, will be in the right subtree
	 */

	result = rbtree_delete_node( t, n->right, k, vout );
	if ( result != RBTREE_SUCCESS ) status = result;
      }
    }
    else {
      /*
       * Nothing in this subtree, so return not found
       */

      status = RBTREE_NOT_FOUND;
    }
  }
  else status = RBTREE_ERROR;
  return status;
}

static int rbtree_delete_rebalance( rbtree *t,
				    rbtree_node *p,
				    rbtree_node *n ) {
  /*
   * rbtree_delete_rebalance() is called to rebalance the tree after a
   * node is deleted in the complex case.  p is the parent of the node
   * that was just deleted, and n, if not NULL, was the child put in
   * its place.  If n is not NULL it must be black.  Since the deleted
   * node must have been black, there must have been a sibling or
   * property 3 would have failed.  We find the sibling and call it s.
   *
   * Case 1: s is red.  Since it has a red child, p must be black.  If
   * n is the left child of p, we rotate left at p and make p red and
   * s black.  If n is the right child of p, we rotate right at p and
   * make p red and s black.  Since s was red and p and n were both
   * black, for property 3 to hold s must have two black children, sl
   * and sr.  Now p is red and has two black children, n and either sl
   * or sr.  We have reduced to the case where n and s are black and p
   * is red.
   *
   * Case 2: s is black, p is black and the children of s are black or
   * do not exist.  We make s red, so all paths passing through s have
   * one less black node.  Since a black node was deleted between p
   * and n, those paths also were one black node short.  Thus, paths
   * passing through p now all have the same number of black nodes
   * again, but unless p is the root node they might be unbalanced
   * against the rest of the tree.  Recurse on p and its parent.
   *
   * Case 3: s is black, p is red and both of the children of s either
   * do not exist or are black.  If we make p black and s red, we have
   * not changed the number of black nodes in paths through s, and we
   * have increased by one the number of black nodes on paths through
   * n, compensating for the original deletion.
   *
   * Case 4: n and s are black.  n is the left child of p, and s is
   * the right child of p.  The left child of s exists and is red, and
   * the right child of s does not exist or is black.  We rotate right
   * at s, so that the red left child of s becomes n's new sibling and
   * the black s becomes its new left child.  We make the new sibling
   * black and the old one red, and then we relabel the former left
   * child of s to s and fall through to case 6.  Paths that passed
   * through the left child of s now either pass through it in its new
   * position, or through it then s, but not through the right child
   * of s, if any, and thus have the same number of black nodes as
   * before.  Paths that passed through the right child of s, if any,
   * now pass through the former left child, then s, then the right
   * child, and thus have the same number of black nodes as before.
   *
   * Case 5: n and s are black.  s is the left child of p, and n is
   * the right child of p.  The right child of s exists and is red,
   * and the left child of s does not exist or is black.  We rotate
   * left at s, so that the red right child of s becomes n's new
   * sibling and the black s becomes its new right child.  We make the
   * new sibling black and the old one red, and then we relabel the
   * former right child of s to s and fall through to case 7.  Paths
   * that passed through the right child of s now either pass through
   * it in its new position, or through it then s, but not through the
   * left child of s, if any, and thus have the same number of black
   * nodes as before.  Paths that passed through the left child of s,
   * if any, now pass through the former right child, then s, then the
   * left child, and thus have the same number of black nodes as
   * before.
   * 
   * Case 6: n and s are black.  n is the left child of p, and s is
   * the right child of p.  The right child of s exists and is red.
   * We rotate left at p, so now s is the parent of p and its former
   * right child.  We switch the colors of s and p, and make the red
   * right child of s black.  The subtree still has a red node at its
   * root, and now an additional black node sits between it and n,
   * compensating for the one lost in deletion.  The paths not passing
   * through n either pass through p to its new other child (the
   * former left child of s) instead of s, and hence have the same
   * number of black nodes (p is black now, as s was), or they pass
   * through red s and then the now-black right child of s instead of
   * red p, black s and the red right child of s, and hence have the
   * same number of black nodes.
   *
   * Case 7: n and s are black.  n is the right child of p, and s is
   * the left child of p.  The left child of s exists and is red.  We
   * rotate right at p, so now s is the parent of p and its former
   * left child.  We switch the colors of s and p, and make the red
   * left child of s black.  The subtree still has a red node at its
   * root, and now an additional black node sits between it and n,
   * compensating for the one lost in deletion.  The paths not passing
   * through n either pass through p to its new other child (the
   * former right child of s) instead of s, and hence have the same
   * number of black nodes (p is black now, as s was), or they pass
   * through red s and then the now-black left child of s instead of
   * red p, black s and the red left child of s, and hence have the
   * same number of black nodes.
   *
   */

  int status;
  rbtree_node *s;

  status = RBTREE_SUCCESS;
  if ( t && p ) {
    if ( !n || n->color == BLACK ) {
      if ( p->left == n ) s = p->right;
      else if ( p->right == n ) s = p->left;
      else {
	s = NULL;
	status = RBTREE_ERROR;
      }

      if ( s && status == RBTREE_SUCCESS ) {
	/* Case 1: s is red */

	if ( s->color == RED ) {

	  rbdbg( "rbtree_delete_rebalance( %p, %p ): case 1; s is red\n",
		 p, n );

	  if ( p->left == n ) {
	    rbtree_rotate_left( t, p );
	    p->color = RED;
	    s->color = BLACK;
	    s = p->right;
	  }
	  else if ( p->right == n ) {
	    rbtree_rotate_right( t, p );
	    p->color = RED;
	    s->color = BLACK;
	    s = p->left;
	  }
	  else status = RBTREE_ERROR;
	}

	/* We fall through to the next case */

	if ( s && s->color == BLACK ) {
	  if ( ( !(s->left)  || s->left->color == BLACK ) &&
	       ( !(s->right) || s->right->color == BLACK ) ) {
	    if ( p->color == BLACK ) {
	      /*
	       * p, n and s are all black, and the children of s are
	       * black of do not exist.  This is case 2 above.  We
	       * make s red, and if p is the root, we're done.
	       * Otherwise, recurse.
	       */

	      rbdbg( "rbtree_delete_rebalance( %p, %p ): case 2; p, n and s (%p) are all black\n",
		     p, n, s );

	      s->color = RED;
	      if ( p->up ) {

		rbdbg( "rbtree_delete_rebalance( %p, %p ): case 2 about to recurse\n",
		       p, n );

		status = rbtree_delete_rebalance( t, p->up, p );
	      }
	      else {
		rbdbg( "rbtree_delete_rebalance( %p, %p ): case 2 at the root, we're done\n",
		       p, n );
	      }
	    }
	    else if ( p->color == RED ) {
	      /* Case 3; we make p black and s red and we're done */

	      rbdbg( "rbtree_delete_rebalance( %p, %p ): case 3, p red, n and s black, both children of s black\n",
		     p, n );
	      
	      p->color = BLACK;
	      s->color = RED;
	    }
	    else status = RBTREE_ERROR;
	  }
	  else {
	    /*
	     * n and s are both black, and s has at least one red child
	     */
	    
	    if ( p->left == n ) {
	      if ( p->right == s ) {
		if ( ( s->left && s->left->color == RED ) &&
		     ( !(s->right) || s->right->color == BLACK ) ) {
		  /* This is case 4 as described above. */

		  rbdbg( "rbtree_delete_rebalance( %p, %p ): case 4 (s is %p)\n",
			 p, n, s );

		  s->left->color = BLACK;
		  s->color = RED;

		  rbtree_rotate_right( t, s );

		  /*
		   * The old left child of s is the new sister of n,
		   * and ended up the parent of s after the rotation.
		   */

		  s = s->up;
		}

		/*
		 * This must be true, since at least one of them is
		 * red or we would have hit case 3, and if it was
		 * the left one we just moved it above.
		 */

		if ( s->right && s->right->color == RED ) {
		  /* This is case 6 as described above. */

		  rbdbg( "rbtree_delete_rebalance( %p, %p ): case 6\n", p, n );

		  s->right->color = BLACK;
		  s->color = p->color;
		  p->color = BLACK;

		  rbtree_rotate_left( t, p );
		}
		else status = RBTREE_ERROR;
	      }
	      else status = RBTREE_ERROR;
	    }
	    else if ( p->right == n ) {
	      if ( p->left == s ) {
		if ( ( s->right && s->right->color == RED ) &&
		     ( !(s->left) || s->left->color == BLACK ) ) {
		  /* This is case 5 as described above. */

		  rbdbg( "rbtree_delete_rebalance( %p, %p ): case 5 (s is %p)\n",
			 p, n, s );

		  s->right->color = BLACK;
		  s->color = RED;

		  rbtree_rotate_left( t, s );

		  /*
		   * The old right child of s is the new sister of n,
		   * and ended up the parent of s after the rotation.
		   */
		  s = s->up;
		}

		/*
		 * This must be true, since at least one of them is
		 * red or we would have hit case 3, and if it was
		 * the right one we just moved it above.
		 */

		if ( s->left && s->left->color == RED ) {
		  /* This is case 7 as described above. */

		  rbdbg( "rbtree_delete_rebalance( %p, %p ): case 7\n", p, n );

		  s->left->color = BLACK;
		  s->color = p->color;
		  p->color = BLACK;

		  rbtree_rotate_right( t, p );
		}
		else status = RBTREE_ERROR;
	      }
	      else status = RBTREE_ERROR;
	    }
	    else status = RBTREE_ERROR;
	  }
	}
	else status = RBTREE_ERROR;
      }
      else status = RBTREE_ERROR;
    }
    else status = RBTREE_ERROR;
  }
  else status = RBTREE_ERROR;
  return status;
}

int rbtree_delete( rbtree *t, void *k, void **vout ) {
  int result;

  if ( t )
    return rbtree_delete_node( t, t->root, k, vout );
  else return RBTREE_ERROR;
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

int rbtree_insert_no_overwrite( rbtree *t, void *key, void *val ) {
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
	result = rbtree_insert_node( t, NULL, &(t->root), k, v, 0 );
	if ( result != RBTREE_SUCCESS ) {
	  if ( k != key && t->free_key ) t->free_key( k );
	  if ( v != val && t->free_val ) t->free_val( v );
	  status = result;
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

static int rbtree_insert_node( rbtree *t, rbtree_node *parent,
			       rbtree_node **n,
			       void *k, void *v,
			       int overwrite ) {
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
	++(t->count);
	result = RBTREE_SUCCESS;
      }
      else result = RBTREE_ERROR;
    }
    else {
      c = t->comparator( k, (*n)->key );
      if ( c < 0 )
	/* Insert it in the left subtree */
	result = rbtree_insert_node( t, *n, &((*n)->left), k, v,
				     overwrite );
      else if ( c == 0 ) {
	/* Replace this value and key */
	if ( overwrite ) {
	  if ( t->free_key ) t->free_key( (*n)->key );
	  (*n)->key = k;
	  if ( t->free_val ) t->free_val( (*n)->value );
	  (*n)->value = v;
	  result = RBTREE_SUCCESS;
	  /* We don't change the count to replace an existing node */
	}
	else result = RBTREE_NO_OVERWRITE;
      }
      else
	/* Insert it in the right subtree */
	result = rbtree_insert_node( t, *n, &((*n)->right), k, v,
				     overwrite );
    }
  }
  else result = RBTREE_ERROR;
  return result;
}

static void rbtree_insert_post( rbtree *t, rbtree_node *n ) {
  /*
   * Post-process rbtree insert to make sure all the properties still
   * hold.
   *
   * The required properties are:
   *
   * Property 1: The root node is black
   * Property 2: Both children of a red node are black
   * Property 3: Every path from the root to a leaf node crosses an
   *             equal number of black nodes
   *
   * The newly inserted node n is always red.  The possible cases we
   * have to handle are:
   *
   * Case 1: The new node is the root.  Make it black and we're done
   * (all paths on both subtrees of the root pass through the root, so
   * we're okay with property 3 here.
   *
   * Case 2: The new node's parent is black.  This threatens none of
   * the required properties, so we don't need to do anything.
   *
   * For the following cases, the parent exists and is red, so it
   * can't be the root, and a grandparent exists (which must be black
   * by property 2, since the parent is red)
   *
   * Case 3: the parent is red, and the grandparent is black, as
   * above.  An aunt exists and is red.  If we make the parent and
   * aunt black, and the grandparent red, the subtree depending from
   * the grandparent is okay now.  Do that and call this function
   * recursively on the newly red grandparent to fix up the rest of
   * the tree.  This recurses at most O(log(n)) times.
   *
   * Case 4: the node is the right child of the parent, and the parent
   * is the left child of the grandparent.  The aunt does not exist or
   * is black.  We perform a right rotation on the parent, and then we
   * obtain a black grandparent, a red parent as its left child, and a
   * red node as the parent's left child.  We haven't added or removed
   * any black nodes on the path to any of the children in the
   * rotation, so property 3 is safe, but we still violate property 2.
   * Now this is just case 6, and we handle it the same way.
   *
   * Case 5: the node is the left child of the parent, and the parent
   * is the right child of the grandparent.  The aunt does not exist
   * or is black.  This is a mirror image of case 4.  We perform a
   * left rotation on the parent, and obtain a black grandparent, a
   * red parent as its right child, and a red node as the parent's
   * right child.  This is just case 7, and we handle it the same way.
   *
   * Case 6: we have a black grandparent, its left child is a red
   * parent, and the parent's left child is a red node.  Turn the
   * parent black and the grandparent red, and perform a right
   * rotation at the grandparent.  The parent is now the grandparent
   * and black, and has the node and the grandparent as red children.
   * Property 2 is now satisfied, and we haven't disrupted property 3.
   *
   * Case 7: we have a black grandparent, its right child is a red
   * parent, and the parent's right child is a red node.  Turn the
   * parent black and the grandparent red, and perform a left rotation
   * at the grandparent.  This is a mirror image of case 6.
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

	  rbdbg( "rbtree_insert_post(): case 3 (%p)\n", n );

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

	      rbdbg( "rbtree_insert_post(): case 6 (%p)\n", n );

	      n->up->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_right( t, grandparent );
	    }
	    else if ( n->up == grandparent->right ) {
	      /* A right rotation will fix things up */

	      rbdbg( "rbtree_insert_post(): case 4 (%p)\n", n );

	      rbtree_rotate_right( t, n->up );
	      n->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_left( t, grandparent );
	    }
	    else {
	      rberr( "Inconsistency seen in rbtree_insert_post( %p ): parent not child of grandparent\n",
		     n );
	    }
	  }
	  else if ( n->up->right == n ) {
	    if ( n->up == grandparent->left ) {
	      /* A left rotation will fix things up */

	      rbdbg( "rbtree_insert_post(): case 5 (%p)\n", n );

	      rbtree_rotate_left( t, n->up );
	      n->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_right( t, grandparent );
	    }
	    else if ( n->up == grandparent->right ) {
	      rbdbg( "rbtree_insert_post(): case 7 (%p)\n", n );

	      n->up->color = BLACK;
	      grandparent->color = RED;
	      rbtree_rotate_left( t, grandparent );
	    }
	    else {
	      rberr( "Inconsistency seen in rbtree_insert_post( %p ): parent not child of grandparent\n",
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
    else {
      rbdbg( "rbtree_insert_post(): case 2 (%p)\n", n );
    }
  }
  else {
    /*
     * This is the root, make sure it's black and we're done
     */

    rbdbg( "rbtree_insert_post(): case 1 (%p)\n", n );

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
	result = rbtree_insert_node( t, NULL, &(t->root), k, v, 1 );
	if ( result != RBTREE_SUCCESS ) {
	  if ( k != key && t->free_key ) t->free_key( k );
	  if ( v != val && t->free_val ) t->free_val( v );
	  status = result;
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

static int rbtree_replace_node( rbtree *t, rbtree_node *m, rbtree_node *n ) {
  /*
   * Substitute n into m's place in the tree.  Make sure in advance
   * that n has the same children as m, has the same color, or the
   * tree will get screwed up.
   */

  rbtree_node **mptr;
  int status;

  status = RBTREE_SUCCESS;
  if ( t && m ) {
    if ( m->up ) {
      if ( m == m->up->left ) mptr = &(m->up->left);
      else if ( m == m->up->right ) mptr = &(m->up->right);
      else status = RBTREE_ERROR;
    }
    else mptr = &(t->root);
    if ( status == RBTREE_SUCCESS ) *mptr = n;
  }
  else status = RBTREE_ERROR;
  return status;
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
	rbdbg( "Validation failed (%p): the root node must be black.\n",
	       n );

	return 0;
      }
      /* The root node has no parent */
      if ( n->up ) {

	rbdbg( "Validation failed (%p): the root node must not have a parent.\n",
	       n );

	return 0;
      }
    }
    else {
      if ( n->up ) {
	/* The node must be a child of its parent */
	if ( !( n->up->left == n || n->up->right == n ) ) {

	  rbdbg( "Validation failed (%p): a node must be a child of its parent.\n",
		 n );

	  return 0;
	}
      }
      else {
	/* The node must have a parent */

	rbdbg( "Validation failed (%p): a non-root node must have a parent.\n",
	       n );

	return 0;
      }
    }
    /* The color must be red or black */
    if ( !( n->color == RED || n->color == BLACK ) ) {

      rbdbg( "Validation failed (%p): a node must be either red or black.\n",
	     n );

      return 0;
    }
    /* The children of a red node must be black */
    if ( n->color == RED ) {
      if ( n->left ) {
	if ( n->left->color != BLACK ) {

	  rbdbg( "Validation failed (%p): the left child of a red node must be black.\n",
		 n );

	  return 0;
	}
      }
      if ( n->right ) {
	if ( n->right->color != BLACK ) {

	  rbdbg( "Validation failed (%p): the right child of a red node must be black.\n",
		 n );

	  return 0;
	}
      }
    }
    /* The left child of a node must be less than that node */
    if ( n->left ) {
      result = comparator( n->left->key, n->key );
      if ( result >= 0 ) {

	rbdbg( "Validation failed (%p): the left child of a node must be less than that node.\n",
	       n );

	return 0;
      }
    }
    /* The right child of a node must be greater than that node */
    if ( n->right ) {
      result = comparator( n->key, n->right->key );
      if ( result >= 0 ) {

	rbdbg( "Validation failed (%p): the right child of a node must be greater than that node.\n",
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

	rbdbg( "Validation failed (%p): the left subtree of a node must be valid.\n",
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

	rbdbg( "Validation failed (%p): the right subtree of a node must be valid.\n",
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

      rbdbg( "Validation failed (%p): the left and right subtrees of a node must be have the same number of black nodes to the leaves (%d, %d).\n",
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

    rbdbg( "Validation failed (%p): a node must not be NULL.\n",
	   n );

    return 0;
  }
}

int rbtree_validate( rbtree *t ) {
  int black_nodes_to_leaves, result;

  if ( t ) {
    /* We must have a comparator */
    if ( !(t->comparator) ) {

      rbdbg( "Validation failed: a tree must have a comparator\n" );

      return 0;
    }
    if ( t->root ) {
      result = rbtree_validate_internal( t->root, 0, &black_nodes_to_leaves,
				       t->comparator );
      if ( result == 1 ) {

	rbdbg( "Validation succeeded (%d)\n",
	       black_nodes_to_leaves );

      }
      return result;
    }
    /* It's an empty tree; that's okay */
    else {

      rbdbg( "Validation succeeded: empty tree\n" );

      return 1;
    }
  }
  else {

    rbdbg( "Validation failed: a tree must not be NULL\n" );

    return 0;
  }
}
