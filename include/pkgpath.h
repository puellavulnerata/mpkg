#ifndef __PKGPATH_H__
#define __PKGPATH_H__

char * canonicalize_and_copy( const char * );
int canonicalize_path( char * );
char * concatenate_paths( const char *, const char * );
int is_absolute( const char * );

#endif
