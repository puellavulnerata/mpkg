#ifndef __PKG_H__
#define __PKG_H__

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
      unsigned char hash[16];
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
  int num_entries;
  pkg_descr_entry *entries;
} pkg_descr;

pkg_descr * read_pkg_descr_from_file( char *filename );
void free_pkg_descr( pkg_descr * );

#endif
