#ifndef __PKGDESCR_H__
#define __PKGDESCR_H__

#include <sys/types.h>
#include <time.h>

#include <md5.h>

#define HASH_LEN MD5_RESULT_LEN

typedef struct {
  char *pkg_name;
  time_t pkg_time;
} pkg_descr_hdr;

typedef struct {
  enum {
    ENTRY_FILE,
    ENTRY_DIRECTORY,
    ENTRY_SYMLINK,
    ENTRY_LAST
  } type;
  char *filename;
  char *owner, *group;
  union {
    struct {
      mode_t mode;
      uint8_t hash[HASH_LEN];
    } f;
    struct {
      mode_t mode;
    } d;
    struct {
      char *target;
    } s;
  } u;
} pkg_descr_entry;

typedef struct {
  pkg_descr_hdr hdr;
  int num_entries, num_entries_alloced;
  pkg_descr_entry *entries;
} pkg_descr;

void free_pkg_descr( pkg_descr * );
pkg_descr * read_pkg_descr_from_file( char * );
int write_pkg_descr_to_file( pkg_descr *, char * );

#endif
