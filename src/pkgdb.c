#include <stdlib.h>

#include <pkg.h>

int close_pkg_db( pkg_db *db ) {
  int status, result;

  status = 0;
  if ( db ) {
    result = db->close( db->private );
    if ( result != 0 ) status = result;
    free( db );
  }
  else status = -1;
  return status;
}

int delete_from_pkg_db( pkg_db *db, char *key ) {
  int status, result;

  status = 0;
  if ( db && key ) {
    result = db->delete( db->private, key );
    if ( result != 0 ) status = result;
  }
  else status = -1;
  return status;
}

int insert_into_pkg_db( pkg_db *db, char *key, char *value ) {
  int status, result;

  status = 0;
  if ( db && key && value ) {
    result = db->insert( db->private, key, value );
    if ( result != 0 ) status = result;
  }
  else status = -1;
  return status;
}

char * query_pkg_db( pkg_db *db, char *key ) {
  char *result;

  if ( db && key ) {
    result = db->query( db->private, key );
    return result;
  }
  else return NULL;
}
