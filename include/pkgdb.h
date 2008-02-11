#ifndef __PKG_DB_H__
#define __PKG_DB_H__

typedef struct {
  void *private;
  char * (*query)( void *, char * );
  int (*insert)( void *, char *, char * );
  int (*delete)( void *, char * );
  int (*close)( void * );
} pkg_db;

int close_pkg_db( pkg_db * );
int delete_from_pkg_db( pkg_db *, char * );
int insert_into_pkg_db( pkg_db *, char *, char * );
char * query_pkg_db( pkg_db *, char * );

#endif
