#ifndef __PKGGLOBAL_H__
#define __PKGGLOBAL_H__

#define DEFAULT_TEMP_STRING "/tmp"

void init_pkg_globals( void );

const char * get_temp( void );
void set_temp( const char * );

#endif /* __PKGGLOBAL_H__ */
