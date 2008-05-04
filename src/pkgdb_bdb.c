#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <db.h>

#include <pkg.h>

#define SIZEOF_STR( str ) ( strlen( str) + 1 ) * sizeof( char );

static int close_bdb( void * );
static int delete_from_bdb( void *, char * );
static char * query_bdb( void *, char * );
static int insert_into_bdb( void *, char *, char * );

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
    fprintf( stderr, "bdb database create failed\n" );
    free( ret );
    return NULL;
  }
  bdb = ret->private;

  if( bdb->open( ret->private, NULL, filename, 
    NULL, DB_BTREE, DB_CREATE, 0 ) != 0)
  {
    fprintf( stderr, "bdb database open failed\n" );
    free( ret );
    return NULL;
  }

  return ret;
}

int close_bdb( void *db ) {
  int ret;
  DB *bdb = ((pkg_db *) db)->private;
  ret = bdb->close( bdb, 0 ); // frees db->private too
  free( db );
  return ret;
}

int delete_from_bdb( void *db, char *key ) {
  DB *bdb = ((pkg_db *) db)->private;
  DBT bdb_key;
  
  bdb_key.data = key;
  bdb_key.size = SIZEOF_STR( key );

  return bdb->del( bdb, NULL, &bdb_key, 0 );
}

char * query_bdb( void *db, char *key ) {
  DB *bdb = ((pkg_db *) db)->private;
  DBT bdb_key, bdb_value;
  char *ret;

  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb->get( bdb, NULL, &bdb_key, &bdb_value, 0 );

  ret = malloc( ( bdb_value.size + 1 ) * sizeof( char ) );
  if ( !ret ) return NULL;
  strcpy( ret, bdb_value.data );
  
  return ret;
} 

int insert_into_bdb( void *db, char *key, char *value ) {
  DB *bdb = ((pkg_db *) db)->private;
  DBT bdb_key, bdb_value;
  
  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb_value.data = value;
  bdb_value.size = SIZEOF_STR( value );

  return bdb->put( bdb, NULL, &bdb_key, &bdb_value, 0 );
}
