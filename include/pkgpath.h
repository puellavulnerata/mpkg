#ifndef __PKGPATH_H__
#define __PKGPATH_H__

char * canonicalize_and_copy( char * );
int canonicalize_path( char * );
char * concatenate_paths( char *, char * );
int is_absolute( char * );

#endif
