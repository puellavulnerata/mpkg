#ifndef __PKGPATH_H__
#define __PKGPATH_H__

char * canonicalize_and_copy( const char * );
int canonicalize_path( char * );
char * concatenate_paths( const char *, const char * );
char * get_base_path( const char * ); 
char * get_last_component( const char * );
int is_absolute( const char * );
char * remove_path_prefix( const char *, const char * );

#endif
