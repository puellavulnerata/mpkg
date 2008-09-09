#ifndef __PKG_UTIL_H__
#define __PKG_UTIL_H__

#include <stdio.h>

char * copy_string( char * );
void dbg_printf( char const *, int, char const *, ... );
char * get_temp_dir( void );
char * hash_to_string( unsigned char *, unsigned long );
int is_whitespace( char * );
int parse_strings_from_line( char *, char *** );
char * read_line_from_file( FILE * );
int recrm( const char * );
int strlistlen( char ** );

#endif
