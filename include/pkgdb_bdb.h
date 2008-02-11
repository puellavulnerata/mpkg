#ifndef __PKGDB_BDB_H__
#define __PKGDB_BDB_H__

#define SIZEOF_STR( str ) ( strlen( str) + 1 ) * sizeof( char );

pkg_db * open_pkg_db_bdb( char * );
int close_bdb( void * );
int delete_from_bdb( void *, char * );
char * query_bdb( void *, char * );
int insert_into_bdb( void *, char *, char * );

#endif
