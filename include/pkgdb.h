#ifndef __PKG_DB_H__
#define __PKG_DB_H__

#define PKGDB_TEXT_FILE_NAME "pkg-managed-files"
#ifdef DB_BDB
#define PKGDB_BDB_FILE_NAME "pkg-managed-files.bdb"
#endif /* DB_BDB */

typedef enum {
  DBFMT_TEXT,
#ifdef DB_BDB
  DBFMT_BDB,
#endif /* DB_BDB */
  DBFMT_UNKNOWN
} dbfmt_t;

typedef struct {
  void *private;
  char * (*query)( void *, char * );
  int (*insert)( void *, char *, char * );
  int (*delete)( void *, char * );
  unsigned long (*entry_count)( void * );
  int (*enumerate)( void *, void *, char **, char **, void ** );
  int (*close)( void * );
  dbfmt_t format;
  char *filename;
} pkg_db;

int close_pkg_db( pkg_db * );
int delete_from_pkg_db( pkg_db *, char * );
int enumerate_pkg_db( pkg_db *, void *, char **, char **, void ** );
unsigned long get_entry_count_for_pkg_db( pkg_db * );
int insert_into_pkg_db( pkg_db *, char *, char * );
pkg_db * open_pkg_db( void );
char * query_pkg_db( pkg_db *, char * );

/*
 * Format-specific constructors
 */ 

pkg_db * create_pkg_db_text_file( char * );
pkg_db * open_pkg_db_text_file( char * );

#ifdef DB_BDB

pkg_db * create_pkg_db_bdb( char * );
pkg_db * open_pkg_db_bdb( char * );

#endif /* DB_BDB */

#endif /* __PKG_DB_H__ */
