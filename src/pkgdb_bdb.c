#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <db.h>

#include <pkg.h>

#define SIZEOF_STR( str ) ( strlen( str) + 1 ) * sizeof( char );

static int close_bdb( void * );
static int delete_from_bdb( void *, char * );
static unsigned long entry_count_bdb( void * );
static int enumerate_bdb( void *, void *, char **, char **, void ** );
static char * query_bdb( void *, char * );
static int insert_into_bdb( void *, char *, char * );

pkg_db * create_pkg_db_bdb( char *filename ) {
  pkg_db *ret = malloc( sizeof ( pkg_db ) );
  DB *bdb;

  if ( ret == NULL ) return NULL;

  ret->query = query_bdb;
  ret->insert = insert_into_bdb;
  ret->delete = delete_from_bdb;
  ret->close = close_bdb;

  if( db_create( ret->private, NULL, 0 ) != 0 )
  {
    // Might want to use DB->err, but it's simpler to fprintf.
    fprintf( stderr, "bdb db_create failed\n" );
    free( ret );
    return NULL;
  }
  bdb = ret->private;

  if( bdb->open( ret->private, NULL, filename, 
    NULL, DB_BTREE, DB_CREATE | DB_EXCL, 0 ) != 0)
  {
    fprintf( stderr, "bdb database create failed\n" );
    free( ret );
    return NULL;
  }
  bdb->set_flags( bdb, DB_RECNUM );

  return ret;
}

pkg_db * open_pkg_db_bdb( char *filename ) {
  pkg_db *ret = malloc( sizeof ( pkg_db ) );
  DB *bdb;

  if ( ret == NULL ) return NULL;

  ret->query = query_bdb;
  ret->insert = insert_into_bdb;
  ret->delete = delete_from_bdb;
  ret->close = close_bdb;

  if( db_create( ret->private, NULL, 0 ) != 0 )
  {
    // Might want to use DB->err, but it's simpler to fprintf.
    fprintf( stderr, "bdb db_create failed\n" );
    free( ret );
    return NULL;
  }
  bdb = ret->private;

  if( bdb->open( ret->private, NULL, filename, 
    NULL, DB_BTREE, 0, 0 ) != 0)
  {
    fprintf( stderr, "bdb database open failed\n" );
    free( ret );
    return NULL;
  }

  return ret;
}

static int close_bdb( void *db ) {
  int ret;
  DB *bdb = (DB *)db;
  ret = bdb->close( bdb, 0 ); // frees db->private too
  free( db );
  return ret;
}

static int delete_from_bdb( void *db, char *key ) {
  DB *bdb = (DB *)db;
  DBT bdb_key;
  
  bdb_key.data = key;
  bdb_key.size = SIZEOF_STR( key );

  return bdb->del( bdb, NULL, &bdb_key, 0 );
}

static char * query_bdb( void *db, char *key ) {
  DB *bdb = (DB *)db;
  DBT bdb_key, bdb_value;
  char *ret;

  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb->get( bdb, NULL, &bdb_key, &bdb_value, 0 );

  ret = malloc( ( bdb_value.size + 1 ) * sizeof( char ) );
  if ( !ret ) return NULL;
  memcpy( ret, bdb_value.data, bdb_value.size );
  ret[bdb_value.size] = '\0';
  
  return ret;
} 

static int insert_into_bdb( void *db, char *key, char *value ) {
  DB *bdb = (DB *)db;
  DBT bdb_key, bdb_value;
  
  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb_value.data = value;
  bdb_value.size = SIZEOF_STR( value );

  return bdb->put( bdb, NULL, &bdb_key, &bdb_value, 0 );
}

static unsigned long entry_count_bdb( void *db ) {
  DB *bdb;
  DB_BTREE_STAT stats;
  int result;

  bdb = (DB *)db;
  if ( bdb ) {
    result = bdb->stat( bdb, NULL, &stats, DB_FAST_STAT );
    if ( result == 0 ) return stats.bt_nkeys;
    else return 0;
  }
  else return 0;
}

static int enumerate_bdb( void *db, void *n_in, char **k_out,
			  char **v_out, void **n_out ) {
  DB *bdb;
  DBC *cursor;
  DBT bdb_key, bdb_value;
  int status, result;
  char *ktmp, *vtmp;

  status = 0;
  if ( db && k_out && v_out && n_out ) {
    bdb = (DB *)db;
    cursor = (DBC *)n_in;

    if ( !cursor ) {
      result = bdb->cursor( bdb, NULL, &cursor, 0 );
      if ( result != 0 || cursor == NULL ) status = -1;
    }
    if ( status == 0 ) {
      result = cursor->c_get( cursor, &bdb_key, &bdb_value, DB_NEXT );
      if ( result == 0 ) {
	ktmp = malloc( sizeof( char ) * ( bdb_key.size + 1 ) );
	if ( ktmp ) {
	  vtmp = malloc( sizeof( char ) * ( bdb_value.size + 1 ) );
	  if ( vtmp ) {
	    memcpy( ktmp, bdb_key.data, bdb_key.size );
	    ktmp[bdb_key.size] = '\0';
	    memcpy( vtmp, bdb_value.data, bdb_value.size );
	    vtmp[bdb_value.size] = '\0';
	    *n_out = cursor;
	    *k_out = ktmp;
	    *v_out = vtmp;
	  }
	  else {
	    free( ktmp );
	    cursor->c_close( cursor );
	    status = -1;
	  }
	}
	else {
	  cursor->c_close( cursor );
	  status = -1;
	}
      }
      else {
	cursor->c_close( cursor );
	*n_out = NULL;
	*k_out = NULL;
	*v_out = NULL;
	status = ( result == DB_NOTFOUND ) ? 0 : -1;
      }
    }
  }
  else status = -1;
  return status;
}
