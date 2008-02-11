#define SIZEOF_STR( str ) ( strlen( str) + 1 ) * sizeof( char );

pkg_db * open_pkg_db_bdb( char * );
int close_bdb( pkg_db * );
int delete_from_bdb( pkg_db *, char * );
char * query_bdb( pkg_db *, char * );
int insert_into_bdb( pkg_db *, char *, char * );
