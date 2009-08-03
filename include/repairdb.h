#ifndef __REPAIRDB_H__
#define __REPAIRDB_H__

#include <md5.h>
#include <pkgdb.h>
#include <pkgdescr.h>
#include <rbtree.h>

#include <sys/types.h>
#include <time.h>

#define REPAIRDB_SUCCESS (0)
#define REPAIRDB_ERROR (-1)
#define REPAIRDB_FATAL_ERROR (-2)

#define HASH_LEN MD5_RESULT_LEN

typedef struct {
  /* Filesystem location claimed */
  char *location;
  /* Name of claiming package */
  char *pkg_name;
  /*
   * The mtime of the claiming package's description.  We know when
   * that package was installed, so we can resolve conflicts in favor
   * of the most recent package.
   */
  time_t pkg_descr_mtime;
  /* Type of object claimed */
  enum {
    CLAIM_DIRECTORY,
    CLAIM_FILE,
    CLAIM_SYMLINK
  } claim_type;
  /* Expected mtime of installed files, from the package-description header */
  time_t pkgtime;
  /* Type-specific fields */
  union {
    struct {
      /* MD5 of files */
      uint8_t hash[HASH_LEN];
    } f;
    struct {
      /* Target of symlinks */
      char *target;
    } s;
  } u;
} claim;

typedef struct claims_list_node_struct claims_list_node_t;
struct claims_list_node_struct {
  claim c;
  claims_list_node_t *next, *prev;
};

typedef struct {
  char *location;
  int num_claims;
  claims_list_node_t *head, *tail;
} claims_list_t;

typedef struct {
  /*
   * The rbtree has strings as keys, and claims_list_t * as values.
   * It does *not* use the copy/free functions; this allows us to keep
   * only a single copy of each location string, and both the keys of
   * the rbtree and the location fields of the claims_list_t
   * structures pointed to by the values point to it.  Thus, when we
   * free the tree, we need to enumerate them by hand and free
   * appropriately.  Use the function free_claims_list_map().
   */

  int num_locations, num_packages, num_claims;
  rbtree *t;
} claims_list_map_t;

claims_list_map_t * alloc_claims_list_map( void );
void * enumerate_claims_list_map( claims_list_map_t *, void *,
				  claims_list_t ** );
void free_claims_list_map( claims_list_map_t * );
void free_claims_list( claims_list_t * );
claims_list_t * get_claims_list_by_location( claims_list_map_t *, char * );
void repairdb_help( void );
void repairdb_main( int, char ** );
claims_list_map_t * repairdb_pass_one( void );
rbtree * repairdb_pass_two( claims_list_map_t *, char );
int repairdb_pass_three( pkg_db *, rbtree * );

#endif /* __REPAIRDB_H__ */
