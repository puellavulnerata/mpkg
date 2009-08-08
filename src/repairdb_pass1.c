#include <pkg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

static int pass_one_handle_descr_entry( claims_list_map_t *, char *,
					pkg_descr *, time_t,
					pkg_descr_entry * );
static int pass_one_handle_descr( claims_list_map_t *, char *, time_t );

claims_list_map_t * alloc_claims_list_map( void ) {
  claims_list_map_t *m;

  m = malloc( sizeof( *m ) );
  if ( m ) {
    m->num_locations = 0;
    m->num_packages = 0;
    m->num_claims = 0;
    m->t = rbtree_alloc( rbtree_string_comparator, NULL, NULL, NULL, NULL );
    if ( !(m->t) ) {
      free( m );
      m = NULL;
    }
  }

  return m;
}

void * enumerate_claims_list_map( claims_list_map_t *m, void *n,
				  claims_list_t **cl_out ) {
  rbtree_node *n_in, *n_out;
  void *k, *v;

  n_out = NULL;
  if ( m && m->t && cl_out ) {
    n_in = (rbtree_node *)n;
    k = rbtree_enum( m->t, n_in, &v, &n_out );
    if ( k ) *cl_out = v;
    else *cl_out = NULL;
  }

  return n_out;
}

void free_claims_list_map( claims_list_map_t *m ) {
  rbtree_node *n;
  void *k, *v;
  char *location;
  claims_list_t *cl;

  if ( m ) {
    if ( m->t ) {
      do {
	/*
	 * Grab the first node from the tree (yes, we do init n
	 * *inside* the loop; the n in the loop condition is set to
	 * non-NULL if the tree was not empty by rbtree_enum() - we
	 * can't use that n to get the next node because we free that
	 * node in the loop, so we get the first node of the tree
	 * every time)
	 */
	n = NULL;
	k = rbtree_enum( m->t, n, &v, &n );

	/*
	 * If we got one, first we delete it from the tree, and then
	 * we free its key and value.
	 */
	if ( k ) {
	  location = (char *)k;

	  /* Delete this one from the tree */
	  rbtree_delete( m->t, location, NULL );

	  /* It's out of the tree, now free the value if we can */
	  if ( v ) {
	    cl = (claims_list_t *)v;
	    free_claims_list( cl );
	  }

	  /*
	   * Now free the key (after the value because the
	   * claims_list_t refers to it)
	   */
	  free( location );
	}
      } while ( n );

      /* The tree should now have no nodes */

      rbtree_free( m->t );
      m->t = NULL;
    }

    free( m );
  }
}

void free_claims_list( claims_list_t *cl ) {
  claims_list_node_t *cn, *temp;
  int orig_num_claims;

  if ( cl ) {
    orig_num_claims = cl->num_claims;
    cn = cl->head;
    while ( cn ) {
      /* Save the next pointer */
      temp = cn->next;

      /* Detach this node from the list */
      if ( cn->next ) cn->next->prev = cn->prev;
      else cl->tail = cn->prev;
      if ( cn->prev ) cn->prev->next = cn->next;
      else cl->head = cn->next;
      cn->next = cn->prev = NULL;
      --(cl->num_claims);

      /* Null out the location, since it's list-wide and we're detached now */
      cn->c.location = NULL;

      /*
       * Free the contents of the claim; we don't free the location,
       * since it points to the same string for every claim in the
       * list, which we also have from the list header.  Thus, we need
       * only worry about the symlink target, if one is present, and
       * the package name.
       */
      if ( cn->c.pkg_name ) {
	free( cn->c.pkg_name );
	cn->c.pkg_name = NULL;
      }

      if ( cn->c.claim_type == CLAIM_SYMLINK ) {
	if ( cn->c.u.s.target ) {
	  free( cn->c.u.s.target );
	  cn->c.u.s.target = NULL;
	}
      }

      /* Free the node itself */
      free( cn );      

      /* Advance to the next node using the saved pointer */
      cn = temp;
    }

    /*
     * We need not free the location, since it's a pointer to the key
     * as used in the rbtree and will be freed separately.  Just null
     * it out.
     */
    cl->location = NULL;

    /* Check the num_claims and throw a warning if it's inconsistent */
    if ( cl->num_claims != 0 ) {
      fprintf( stderr,
	       "free_claims_list(): detected inconsistency in num_claims;" );
      fprintf( stderr, " is %d, originally %d\n",
	       cl->num_claims, orig_num_claims );
    }

    /* Free the list header */
    free( cl );
  }
}

claims_list_t * get_claims_list_by_location( claims_list_map_t *m,
					     char *location ) {
  claims_list_t *cl;
  char *k;
  void *v;
  int result;

  cl = NULL;
  if ( m && m->t && location ) {
    result = rbtree_query( m->t, location, &v );
    if ( result == RBTREE_SUCCESS ) cl = (claims_list_t *)v;
    else if ( result == RBTREE_NOT_FOUND ) {
      /*
       * We allocate a new one and insert it into the tree, or return
       * NULL if we fail.
       */
      k = copy_string( location );
      if ( k ) {
	cl = malloc( sizeof( *cl ) );
	if ( cl ) {
	  cl->location = k;
	  cl->num_claims = 0;
	  cl->head = cl->tail = NULL;
	  result = rbtree_insert( m->t, k, cl );
	  if ( result != RBTREE_SUCCESS ) {
	    free_claims_list( cl );
	    free( k );
	    cl = NULL;
	  }
	}
	else free( k );
      }
    }
  }

  return cl;
}

static int pass_one_handle_descr_entry( claims_list_map_t *m, char *p,
					pkg_descr *descr, time_t descr_mtime,
					pkg_descr_entry *e ) {
  int status;
  claims_list_t *cl;
  claims_list_node_t *claim;
  /* Canonicalized path */
  char *path;

  status = REPAIRDB_SUCCESS;
  if ( m && p && descr && e ) {
    if ( e->type == ENTRY_DIRECTORY ||
	 e->type == ENTRY_FILE ||
	 e->type == ENTRY_SYMLINK ) {
      path = canonicalize_and_copy( e->filename );
      if ( path ) {
	claim = NULL;
	cl = NULL;

	claim = malloc( sizeof( *claim ) );
	if ( claim ) {
	  claim->next = NULL;
	  claim->prev = NULL;
	  claim->c.pkg_descr_mtime = descr_mtime;
	  claim->c.pkgtime = descr->hdr.pkg_time;
	  claim->c.pkg_name = copy_string( descr->hdr.pkg_name );
	  if ( claim->c.pkg_name ) {
	    if ( e->type == ENTRY_FILE ) {
	      claim->c.claim_type = CLAIM_FILE;
	      memcpy( claim->c.u.f.hash, e->u.f.hash,
		      sizeof( claim->c.u.f.hash ) );
	    }
	    else if ( e->type == ENTRY_DIRECTORY ) {
	      claim->c.claim_type = CLAIM_DIRECTORY;
	    }
	    else {
	      /* e->type == ENTRY_SYMLINK */
	      claim->c.claim_type = CLAIM_SYMLINK;
	      claim->c.u.s.target = copy_string( e->u.s.target );
	      if ( !(claim->c.u.s.target) ) {
		/* Free it all if we fail to allocate */
		free( claim->c.pkg_name );
		free( claim );
		claim = NULL;
	      }
	    }
	  }
	  else {
	    free( claim );
	    claim = NULL;
	  }
	}

	if ( claim ) cl = get_claims_list_by_location( m, path );

	if ( claim && cl ) {
	  /* Set the claim location */
	  claim->c.location = cl->location;
	  /* Attach this claim to the list */
	  claim->prev = NULL;
	  claim->next = cl->head;
	  if ( cl->head ) cl->head->prev = claim;
	  else cl->tail = claim;
	  cl->head = claim;
	  /*
	   * Increment the locations counter if this is the first claim
	   * for this location.
	   */
	  if ( cl->num_claims == 0 ) ++(m->num_locations);
	  /* Increment the claims counters */
	  ++(cl->num_claims);
	  ++(m->num_claims);
	}
	else {
	  /*
	   * We don't need to free cl here; it's just empty and it'll
	   * get freed with the rest of the claims list map.
	   */
	  if ( claim ) {
	    if ( claim->c.claim_type == CLAIM_SYMLINK ) {
	      if ( claim->c.u.s.target ) {
		free( claim->c.u.s.target );
		claim->c.u.s.target = NULL;
	      }
	    }
	    free( claim );
	  }
	  fprintf( stderr,
		   "Failed to allocate memory handling entry %s in %s\n",
		   path, p );
	  status = REPAIRDB_FATAL_ERROR;
	}

	free( path );
      }
      else {
	fprintf( stderr, "Failed to canonicalize pathname %s in %s\n",
		 e->filename, p );
	status = REPAIRDB_FATAL_ERROR;
      }
    }
    else {
      fprintf( stderr, "Saw unknown entry type for %s in %s\n",
	       e->filename, p );
      status = REPAIRDB_ERROR;
    }
  }
  else status = REPAIRDB_FATAL_ERROR;

  return status;
}

static int pass_one_handle_descr( claims_list_map_t *m, char *p,
				  time_t descr_mtime ) {
  int status, result;
  pkg_descr *descr;
  int i;

  status = REPAIRDB_SUCCESS;
  if ( m && p ) {
    descr = read_pkg_descr_from_file( p );
    if ( descr ) {
      /* Iterate over each entry */
      for ( i = 0; i < descr->num_entries; ++i ) {
	result = pass_one_handle_descr_entry( m, p, descr, descr_mtime,
					      &(descr->entries[i]) );

	if ( result != REPAIRDB_SUCCESS ) {
	  if ( result == REPAIRDB_ERROR ) {
	    fprintf( stderr, "Error handling package-description entry " );
	    fprintf( stderr, "%s in %s in pass one, skipping it.\n",
		     descr->entries[i].filename, p );
	  }
	  else if ( result == REPAIRDB_FATAL_ERROR ) {
	    fprintf( stderr, "Fatal error handling package-description " );
	    fprintf( stderr, "entry %s in %s in pass one, aborting.\n",
		     descr->entries[i].filename, p );
	    status = result;
	    break;
	  }
	  else {
	    fprintf( stderr, "Unknown error handling package-description " );
	    fprintf( stderr, "entry %s in %s in pass one, skipping it.\n",
		     descr->entries[i].filename, p );
	  }
	}
      }

      if ( status == REPAIRDB_SUCCESS ) {
	/* Count this package */
	++(m->num_packages);
      }

      /* Free the descr */
      free_pkg_descr( descr );
    }
    else {
      fprintf( stderr, "Unable to read package-description file %s\n", p );
      status = REPAIRDB_ERROR;
    }
  }
  else status = REPAIRDB_FATAL_ERROR;

  return status;
}

claims_list_map_t * repairdb_pass_one( void ) {
  /*
   * See the comment in perform_repair() of repairdb.c for context and
   * theory of this function.
   */

  /* The structure we're constructing */
  claims_list_map_t *m;
  DIR *d;
  struct dirent *dentry;
  char *path;
  int valid, result;
  struct stat st;
  time_t descr_mtime;

  m = alloc_claims_list_map();
  if ( m ) {
    /* Open the package directory */
    d = opendir( get_pkg() );
    if ( d ) {
      /* Enumerate entries from the directory and handle them */
      
      dentry = NULL;
      do {
	dentry = readdir( d );
	if ( dentry ) {
	  /*
	   * Indicates if it's a potentially valid package-description
	   */
	  valid = 1;
	  path = NULL;
	  descr_mtime = 0;

	  /* Exclude . and .. */
	  if ( valid && strcmp( dentry->d_name, "." ) == 0 ) valid = 0;
	  if ( valid && strcmp( dentry->d_name, ".." ) == 0 ) valid = 0;

	  /* Exclude anything that starts with pkg-managed-files */
	  if ( valid && ( strstr( dentry->d_name, "pkg-managed-files" ) ==
			  dentry->d_name ) ) {
	    valid = 0;
	  }

	  /*
	   * Exclude anything with a . in it (package-descriptions
	   * don't have them)
	   */
	  if ( valid &&
	       strstr( dentry->d_name, "." ) != NULL ) valid = 0;

	  /*
	   * Exclude anything with a ~ in it, so the users can edit
	   * the files by hand in emacs.
	   */
	  if ( valid &&
	       strstr( dentry->d_name, "~" ) != NULL )
	    valid = 0;

	  /* Construct the full path name here */
	  if ( valid ) {
	    path = concatenate_paths( get_pkg(), dentry->d_name );
	    if ( !path ) {
	      fprintf( stderr,
		       "Warning, unable to allocate memory for pkgdir " );
	      fprintf( stderr, "entry %s, skipping it\n", dentry->d_name );
	      valid = 0;
	    }
	  }

	  /* Okay, the name passes all the tests.  See if we can stat it */
	  if ( valid && path ) {
	    result = stat( path, &st );
	    if ( result == 0 ) {
	      if ( S_ISREG( st.st_mode ) ) {
		descr_mtime = st.st_mtime;
	      }
	      else {
		fprintf( stderr, "Warning: pkgdir entry %s is not a",
			 path );
		fprintf( stderr, "regular file or symlink to one, " );
		fprintf( stderr, "skipping it\n" );
		valid = 0;
	      }
	    }
	    else {
	      fprintf( stderr, "Couldn't stat pkgdir entry %s: %s\n",
		       path, strerror( errno ) );
	      valid = 0;
	    }
	  }

	  /* Okay, try to handle it as a package-description */
	  if ( valid && path ) {
	    result = pass_one_handle_descr( m, path, descr_mtime );
	    if ( result != REPAIRDB_SUCCESS ) {
	      if ( result == REPAIRDB_ERROR ) {
		fprintf( stderr, "Error handling package-description %s",
			 path );
		fprintf( stderr, "in pass one, skipping it.\n" );
		valid = 0;
	      }
	      else if ( result == REPAIRDB_FATAL_ERROR ) {
		fprintf( stderr, "Error handling package-description %s",
			 path );
		fprintf( stderr, "in pass one, aborting.\n" );
		valid = 0;
		free_claims_list_map( m );
		m = NULL;
	      }
	      else {
		fprintf( stderr, "Unknown error handling package-" );
		fprintf( stderr, "description %s in pass one, ", path );
		fprintf( stderr, "skipping it.\n" );
		valid = 0;
	      }
	    }
	  }

	  if ( path ) {
	    free( path );
	    path = NULL;
	  }
	}
      } while ( dentry && m );

      closedir( d );
    }
    else {
      fprintf( stderr, "Unable to open package directory %s\n",
	       get_pkg() );
      free_claims_list_map( m );
      m = NULL;
    }
  }

  return m;
}
