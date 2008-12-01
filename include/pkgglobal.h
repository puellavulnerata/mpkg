#ifndef __PKGGLOBAL_H__
#define __PKGGLOBAL_H__

#define DEFAULT_PKG_STRING "/var/pkg"
#define DEFAULT_ROOT_STRING "/"
#define DEFAULT_TEMP_STRING "/tmp"

void free_pkg_globals( void );
void init_pkg_globals( void );

const char * get_pkg( void );
void set_pkg( const char * );

const char * get_root( void );
void set_root( const char * );

const char * get_temp( void );
void set_temp( const char * );

#endif /* __PKGGLOBAL_H__ */
