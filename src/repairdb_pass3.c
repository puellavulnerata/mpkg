#include <stdio.h>
#include <stdlib.h>

#include <pkg.h>

int repairdb_pass_three( pkg_db *db, rbtree *t ) {
  int status, result;
  void *n, *tpkg_v, *location_v, *pkg_v;
  char *location, *pkg, *tpkg;
  rbtree *deletions, *modifications;
  rbtree_node *rn;

  status = REPAIRDB_SUCCESS;
  if ( db && t ) {
    /*
     * This rbtree holds the list of records to be deleted from the
     * database; the keys are locations and the values are always
     * NULL.
     */
    deletions = rbtree_alloc( rbtree_string_comparator,
			      rbtree_string_copier, rbtree_string_free,
			      NULL, NULL );
    /*
     * This rbtree holds the list of records in the database to be
     * modified; the keys are locations and the values are the new
     * package names to assign to these locations.
     */
    modifications = rbtree_alloc( rbtree_string_comparator,
				  rbtree_string_copier, rbtree_string_free,
				  rbtree_string_copier, rbtree_string_free );

    if ( deletions && modifications ) {
      n = NULL;
      do {
	location = NULL;
	pkg = NULL;
	result = enumerate_pkg_db( db, n, &location, &pkg, &n );
	if ( result == 0 ) {
	  if ( n ) {
	    if ( location ) {
	      /*
	       * Query the rbtree from pass two for this location.
	       */
	      tpkg_v = NULL;
	      result = rbtree_query( t, location, &tpkg_v );
	      tpkg = (char *)tpkg_v;
	      if ( result == RBTREE_SUCCESS ) {
		/*
		 * Found it; check if it needs to be modified by comparing
		 * the values.
		 */
		if ( pkg && tpkg ) {
		  /* Compare the values */
		  if ( strcmp( pkg, tpkg ) != 0 ) {
		    /*
		     * They don't match, so we need to modify this DB
		     * record
		     */
		    result = rbtree_insert( modifications, location, tpkg );
		    if ( result != RBTREE_SUCCESS ) {
		      fprintf( stderr,
			       "Error while adding %s (for %s from %s)",
			       location, tpkg, pkg );
		      fprintf( stderr, "to the modification list in pass" );
		      fprintf( stderr, "three\n" );
		      status = REPAIRDB_ERROR;
		    }
		  }
		  /* else no modification needed */

		  /*
		   * Either way, this one does not need to be added, so
		   * we remove it from t.
		   */
		  if ( status == REPAIRDB_SUCCESS ) {
		    result = rbtree_delete( t, location, NULL );
		    if ( result != RBTREE_SUCCESS ) {
		      fprintf( stderr, "Error removing %s from addition list",
			       location );
		      fprintf( stderr, "in pass three\n" );
		      status = REPAIRDB_ERROR;
		    }
		  }
		}
		else {
		  fprintf( stderr, "Saw NULL value where one shouldn't have" );
		  fprintf( stderr, " been during pass three\n" );
		  fprintf( stderr, "location = %s, pkg = %s, tpkg = %s\n",
			   location, ( pkg ? pkg : "null" ),
			   (tpkg ? tpkg : "null" ) );
		  status = REPAIRDB_ERROR;
		}
	      }
	      else if ( result == RBTREE_NOT_FOUND ) {
		/* Not found, add this one to the delete list */
		result = rbtree_insert( deletions, location, NULL );
		if ( result != RBTREE_SUCCESS ) {
		  fprintf( stderr, "Error while adding %s to deletion list",
			   location );
		  fprintf( stderr, "in pass three\n" );
		  status = REPAIRDB_ERROR;
		}
	      }
	      else {
		/* error querying rbtree from pass two */
		fprintf( stderr, "Error querying rbtree from pass two for " );
		fprintf( stderr, "location %s in pass three\n", location );
		status = REPAIRDB_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr,
		       "Error: saw null location while enumerating database" );
	      fprintf( stderr, " for pass three\n" );
	      /* status = REPAIRDB_ERROR; */
	    }
	  }
	  /* else end of db */
	}
	else {
	  fprintf( stderr,
		   "Error %d while enumerating database for pass three\n",
		   result );
	  status = REPAIRDB_ERROR;
	}

	if ( location ) free( location );
	if ( pkg ) free( pkg );
      
      } while ( n && status == REPAIRDB_SUCCESS );

      if ( status == REPAIRDB_SUCCESS ) {
	/*
	 * Now, deletions is the list of records to be deleted from
	 * the database, modifications are records which need their
	 * value changed, and t holds the records which must be added.
	 * Perform those actions.
	 */

	/* Deletions */
	printf( "Performing %lu deletions...", deletions->count );
	rn = NULL;
	do {
	  location_v = rbtree_enum( deletions, rn, NULL, &rn );
	  if ( rn ) {
	    if ( location_v ) {
	      location = (char *)location_v;
	      /* Delete the db record for this location */
	      result = delete_from_pkg_db( db, location );
	      if ( result != 0 ) {
		fprintf( stderr, "Warning: failed to delete location %s",
			 location );
		fprintf( stderr, "from package db in pass three\n" );
		status = REPAIRDB_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr, "Warning: saw null location in deletion list" );
	      fprintf( stderr, "in pass three\n" );
	      status = REPAIRDB_ERROR;
	    }
	  }
	  /* else end of tree */
	} while ( rn );
	printf( "done\n" );
	/* We can free the deletions tree now */
	rbtree_free( deletions );
	deletions = NULL;

	/* Modifications */
	printf( "Peforming %lu modifications...", modifications->count );
	rn = NULL;
	do {
	  location_v = rbtree_enum( modifications, rn, &pkg_v, &rn );
	  if ( rn ) {
	    if ( location_v && pkg_v ) {
	      location = (char *)location_v;
	      pkg = (char *)pkg_v;
	      result = insert_into_pkg_db( db, location, pkg );
	      if ( result != 0 ) {
		fprintf( stderr, "Warning: failed to modify location %s",
			 location );
		fprintf( stderr, " to %s in package db in pass three\n",
			 pkg );
		status = REPAIRDB_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr, "Warning: saw null location or package in" );
	      fprintf( stderr, "modification list in pass three\n" );
	      status = REPAIRDB_ERROR;
	    }
	  }
	  /* else end of tree */
	} while ( rn );
	printf( "done\n" );
	/* We can free the modifications tree now */
	rbtree_free( modifications );
	modifications = NULL;

	/* Additions */
	printf( "Peforming %lu additions...", t->count );
	rn = NULL;
	do {
	  location_v = rbtree_enum( t, rn, &pkg_v, &rn );
	  if ( rn ) {
	    if ( location_v && pkg_v ) {
	      location = (char *)location_v;
	      pkg = (char *)pkg_v;
	      result = insert_into_pkg_db( db, location, pkg );
	      if ( result != 0 ) {
		fprintf( stderr, "Warning: failed to add location %s",
			 location );
		fprintf( stderr, " for %s to package db in pass three\n",
			 pkg );
		status = REPAIRDB_ERROR;
	      }
	    }
	    else {
	      fprintf( stderr, "Warning: saw null location or package in" );
	      fprintf( stderr, "addition list in pass three\n" );
	      status = REPAIRDB_ERROR;
	    }
	  }
	  /* else end of tree */
	} while ( rn );
	printf( "done\n" );
	/*
	 * We don't free t here because the main repairdb routine in
	 * repairdb.c does after this function returns.
	 */
      }
    }

    /* Free the rbtrees if needed */
    if ( deletions ) rbtree_free( deletions );
    if ( modifications ) rbtree_free( modifications );
  }
  else status = REPAIRDB_FATAL_ERROR;

  return status;
}
