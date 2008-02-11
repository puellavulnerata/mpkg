#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <db.h>

#include <pkg.h>

pkg_db * open_pkg_db_bdb( char *filename ) {
  pkg_db *ret = malloc(sizeof struct pkg_db);

  if( ret == NULL ) 
    perror( "" );

  ret->query = query_bdb;
  ret->insert = insert_into_bdb;
  ret->delete = delete_from_bdb;
  ret->close = close_bdb;

  if( db_create( db->private, NULL, 0 ) != 0 )
  {
    // Might want to use DB->err, but it's simpler to fprintf.
    fprintf( stderr, "bdb database create failed\n" );
    free( ret );
    return NULL;
  }

  return db->open( db->private, NULL, filename, NULL, DB_BTREE, DB_CREATE, 0 );
}

int close_bdb( pkg_db *db ) {
  int ret;
  DB *bdb = db->private;
  ret = bdb->close( bdb, 0 ); // frees db->private too
  free( db );
  return ret;
}

int delete_from_bdb( pkg_db *db, char *key ) {
  DB *bdb = db->private;
  DBT bdb_key;
  
  bdb_key.data = key;
  bdb_key.size = SIZEOF_STR( key );

  return bdb->del( bdb, NULL, &bdb_key, 0 );
}

char * query_bdb( pkg_db *db, char *key ) {
  DB *bdb = db->private;
  DBT bdb_key, bdb_value;
  char *ret;

  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb->get( bdb, NULL, &bdb_key, &bdb_value, 0 );

  ret = malloc( SIZEOF_STR(bdb_value.data) );
  strcpy( ret, bdb_data.data );
  
  return ret;
} 

int insert_into_bdb( pkg_db *db, char *key, char *value ) {
  DB *bdb = db->private;
  DBT bdb_key, bdb_value;
  
  bdb_key.data = key; 
  bdb_key.size = SIZEOF_STR( key );

  bdb_value.data = value;
  bdb_value.size = SIZEOF_STR( value );

  return bdb->put( bdb, NULL, &key, &value, 0 );
}
