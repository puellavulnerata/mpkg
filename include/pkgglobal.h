#ifndef __PKGGLOBAL_H__
#define __PKGGLOBAL_H__

#define DEFAULT_PKG_STRING "/var/mpkg"
#define DEFAULT_ROOT_STRING "/"
#define DEFAULT_TEMP_STRING "/tmp"

void free_pkg_globals( void );
void init_pkg_globals( void );
int sanity_check_globals( void );

char * adjust_path_against_root( const char * );

int get_check_md5( void );
void set_check_md5( int );

const char * get_pkg( void );
void set_pkg( const char * );

const char * get_root( void );
void set_root( const char * );

const char * get_temp( void );
void set_temp( const char * );

#endif /* __PKGGLOBAL_H__ */
