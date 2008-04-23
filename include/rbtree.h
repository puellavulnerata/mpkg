#ifndef __RBTREE_H__
#define __RBTREE_H__

#define RBTREE_SUCCESS (0)
#define RBTREE_NOT_FOUND (-1)
#define RBTREE_ERROR (-2)
#define RBTREE_NO_OVERWRITE (-3)

typedef struct rbtree_node_struct {
  void *key, *value;
  enum {
    RED,
    BLACK
  } color;
  struct rbtree_node_struct *left, *right, *up;
} rbtree_node;

typedef struct {
  rbtree_node *root;
  int (*comparator)( void *, void * );
  void * (*copy_key)( void * );
  void (*free_key)( void * );
  void * (*copy_val)( void * );
  void (*free_val)( void * );
  unsigned long count;
} rbtree;

rbtree * rbtree_alloc( int (*)( void *, void * ), /* comparator */
		       void * (*)( void * ), /* copy_key */
		       void (*)( void * ), /* free_key */
		       void * (*)( void * ), /* copy_val */
		       void (*)( void * ) /* free_val */
		       );
int rbtree_delete( rbtree *, void *, void ** );
void rbtree_dump( rbtree *, void (*)( void * ), void (*)( void * ) );
void * rbtree_enum( rbtree *, rbtree_node *, void **, rbtree_node ** );
void rbtree_free( rbtree * );
int rbtree_insert_no_overwrite( rbtree *, void *, void * );
int rbtree_insert( rbtree *, void *, void * );
int rbtree_query( rbtree *, void *, void ** );
unsigned long rbtree_size( rbtree * );
int rbtree_string_comparator( void *, void * );
void * rbtree_string_copier( void * );
void rbtree_string_free( void * );
void rbtree_string_printer( void * );
int rbtree_validate( rbtree * );

#endif
