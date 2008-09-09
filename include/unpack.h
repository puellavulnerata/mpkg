#ifndef __UNPACK_H__
#define __UNPACK_H__

typedef struct {
  pkg_descr *descr;
  char *descr_file;
  char *unpacked_dir;
  enum {
    PKG_VER_1,
    PKG_VER_2
  } version;
} pkg_handle;

void close_pkg( pkg_handle * );
pkg_handle * open_pkg_file( const char * );

#endif
