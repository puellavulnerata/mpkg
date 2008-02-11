#ifndef __RBTREE_H__
#define __RBTREE_H__

typedef struct rbtree_node_struct {
  void *key, *value;
  enum {
    RED,
    BLACK
  } color;
  struct rbtree_node_struct *left, *right;
} rbtree_node;

typedef struct {
  rbtree_node *root;
  int (*comparator)( void *, void * );
} rbtree;

rbtree * rbtree_alloc( int (*)( void *, void * ) );
int rbtree_delete( rbtree *, void * );
void * rbtree_enum( rbtree *, void *, void ** );
void rbtree_free( rbtree * );
int rbtree_insert( rbtree *, void *, void * );
int rbtree_query( rbtree *, void *, void ** );
unsigned long rbtree_size( rbtree * );
int rbtree_string_comparator( void *, void * );

#endif
