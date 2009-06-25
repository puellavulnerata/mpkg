#ifndef __UNPACK_H__
#define __UNPACK_H__

#include <pkgtypes.h>

typedef struct {
  pkg_descr *descr;
  char *descr_file;
  char *unpacked_dir;
  pkg_compression_t compression;
  pkg_version_t version;
} pkg_handle;

void close_pkg( pkg_handle * );
pkg_handle * open_pkg_file( const char * );

#endif
