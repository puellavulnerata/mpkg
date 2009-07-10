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
  int result;

  if ( ret == NULL ) return NULL;

  ret->query = query_bdb;
  ret->insert = insert_into_bdb;
  ret->delete = delete_from_bdb;
  ret->close = close_bdb;
  ret->enumerate = enumerate_bdb;
  ret->entry_count = entry_count_bdb;
  ret->format = DBFMT_BDB;
  ret->filename = copy_string( filename );
  if ( !(ret->filename) ) {
    free( ret );
    return NULL;
  }

  if( ( result = db_create( &bdb, NULL, 0 ) ) != 0 )
  {
    /* Might want to use DB->err, but it's simpler to fprintf. */
    fprintf( stderr, "bdb db_create failed (%d)\n", result );
    free( ret->filename );
    free( ret );
    return NULL;
  }
  ret->private = (void *)bdb;

  if( ( result = bdb->open( bdb, NULL, filename, 
    NULL, DB_BTREE, DB_CREATE | DB_EXCL, 0644 ) ) != 0 )
  {
    fprintf( stderr, "bdb database create failed (%d)\n", result );
    bdb->close( bdb, 0 );
    free( ret->filename );
    free( ret );
    return NULL;
  }
  bdb->set_flags( bdb, DB_RECNUM );

  return ret;
}

pkg_db * open_pkg_db_bdb( char *filename ) {
  pkg_db *ret = malloc( sizeof ( pkg_db ) );
  DB *bdb;
  int result;

  if ( ret == NULL ) return NULL;

  ret->query = query_bdb;
  ret->insert = insert_into_bdb;
  ret->delete = delete_from_bdb;
  ret->close = close_bdb;
  ret->enumerate = enumerate_bdb;
  ret->entry_count = entry_count_bdb;
  ret->format = DBFMT_BDB;
  ret->filename = copy_string( filename );
  if ( !(ret->filename) ) {
    free( ret );
    return NULL;
  }

  if( ( result = db_create( &bdb, NULL, 0 ) ) != 0 )
  {
    /* Might want to use DB->err, but it's simpler to fprintf.*/
    /* fprintf( stderr, "bdb db_create failed (%d)\n", result ); */
    free( ret->filename );
    free( ret );
    return NULL;
  }
  ret->private = (void *)bdb;

  if( ( result = bdb->open( bdb, NULL, filename, 
    NULL, DB_BTREE, 0, 0 ) ) != 0)
  {
    /* fprintf( stderr, "bdb database open failed\n" ); */
    bdb->close( bdb, 0 );
    free( ret->filename );
    free( ret );
    return NULL;
  }
  bdb->set_flags( bdb, DB_RECNUM );

  return ret;
}

static int close_bdb( void *db ) {
  int ret;
  DB *bdb = (DB *)db;
  ret = bdb->close( bdb, 0 ); /* frees db->private too */
  return ret;
}

static int delete_from_bdb( void *db, char *key ) {
  DB *bdb = (DB *)db;
  DBT bdb_key;
  int result;

  memset( &bdb_key, 0, sizeof( bdb_key ) );
  bdb_key.data = key;
  bdb_key.size = SIZEOF_STR( key );

  result = bdb->del( bdb, NULL, &bdb_key, 0 );
  
  return result;
}

static char * query_bdb( void *db, char *key ) {
  DB *bdb = (DB *)db;
  DBT bdb_key, bdb_value;
  int result;

  memset( &bdb_key, 0, sizeof( bdb_key ) );
  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  memset( &bdb_value, 0, sizeof( bdb_value ) );
  bdb_value.flags = DB_DBT_MALLOC;

  result = bdb->get( bdb, NULL, &bdb_key, &bdb_value, 0 );

  if ( result == 0 && bdb_value.data ) return bdb_value.data;
  else {
    if ( bdb_value.data ) free( bdb_value.data );
    return NULL;
  }
} 

static int insert_into_bdb( void *db, char *key, char *value ) {
  DB *bdb = (DB *)db;
  DBT bdb_key, bdb_value;
  int result;

  memset( &bdb_key, 0, sizeof( bdb_key ) );
  memset( &bdb_value, 0, sizeof( bdb_value ) );

  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb_value.data = value;
  bdb_value.size = SIZEOF_STR( value );

  result = bdb->put( bdb, NULL, &bdb_key, &bdb_value, 0 );

  return result;
}

static unsigned long entry_count_bdb( void *db ) {
  DB *bdb;
  DB_BTREE_STAT *stats;
  int result;
  unsigned long count;

  bdb = (DB *)db;
  count = 0;
  stats = NULL;

  if ( bdb ) {
    result = bdb->stat( bdb, NULL, &stats, 0 );
    if ( result == 0 && stats) count = stats->bt_nkeys;
  }

  if ( stats ) free( stats );
  return count;
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
      memset( &bdb_key, 0, sizeof( bdb_key ) );
      bdb_key.flags = DB_DBT_MALLOC;
      memset( &bdb_value, 0, sizeof( bdb_value ) );
      bdb_value.flags = DB_DBT_MALLOC;

      result = cursor->c_get( cursor, &bdb_key, &bdb_value, DB_NEXT );
      if ( result == 0 && bdb_key.data && bdb_value.data ) {
	*n_out = cursor;
	*k_out = bdb_key.data;
	*v_out = bdb_value.data;
      }
      else {
	cursor->c_close( cursor );
	if ( bdb_key.data ) free( bdb_key.data );
	if ( bdb_value.data ) free( bdb_value.data );
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
