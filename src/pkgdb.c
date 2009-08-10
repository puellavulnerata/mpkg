#include <pkg.h>

#include <stdlib.h>
#include <string.h>

int close_pkg_db( pkg_db *db ) {
  int status, result;

  status = 0;
  if ( db ) {
    result = db->close( db->private );
    if ( result != 0 ) status = result;
    if ( db->filename ) {
      free( db->filename );
      db->filename = NULL;
    }
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

int enumerate_pkg_db( pkg_db *db, void *n_in,
		      char **k_out, char **v_out,
		      void **n_out ) {
  if ( db && k_out && v_out && n_out )
    return db->enumerate( db->private, n_in,
			  k_out, v_out, n_out );
  else return -1;
}

unsigned long get_entry_count_for_pkg_db( pkg_db *db ) {
  if ( db ) return db->entry_count( db->private );
  else return 0;
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

pkg_db * open_pkg_db( void ) {
  pkg_db *db;
  const char *pkg_dir;
  char *temp;
  int temp_len;

  db = NULL;
  pkg_dir = get_pkg();

#ifdef DB_BDB
  if ( !db ) {
    temp_len = strlen( pkg_dir ) + strlen( PKGDB_BDB_FILE_NAME ) + 2;
    temp = malloc( sizeof( *temp ) * temp_len );
    if ( temp ) {
      snprintf( temp, temp_len, "%s/%s", pkg_dir, PKGDB_BDB_FILE_NAME );
      db = open_pkg_db_bdb( temp );
      free( temp );
    }
  }
#endif

  if ( !db ) {
    temp_len = strlen( pkg_dir ) + strlen( PKGDB_TEXT_FILE_NAME ) + 2;
    temp = malloc( sizeof( *temp ) * temp_len );
    if ( temp ) {
      snprintf( temp, temp_len, "%s/%s", pkg_dir, PKGDB_TEXT_FILE_NAME );
      db = open_pkg_db_text_file( temp );
      free( temp );
    }
  }

  return db;
}

char * query_pkg_db( pkg_db *db, char *key ) {
  char *result;

  if ( db && key ) {
    result = db->query( db->private, key );
    return result;
  }
  else return NULL;
}
