#ifndef __TAR_H__
#define __TAR_H__

#include <streams.h>

#define TAR_SUCCESS 0
#define TAR_NO_MORE_FILES -1
#define TAR_UNEXPECTED_EOF -2
#define TAR_IO_ERROR -3
#define TAR_BAD_PARAMS -4
#define TAR_INTERNAL_ERROR -5

#define TAR_BLOCK_SIZE 512

#define TAR_FILE 0
#define TAR_LINK 1
#define TAR_SYMLINK 2
#define TAR_CDEV 3
#define TAR_BDEV 4
#define TAR_DIR 5
#define TAR_FIFO 6
#define TAR_CONTIG_FILE 7

#define TAR_FILENAME_OFFSET 0
#define TAR_MODE_OFFSET 100
#define TAR_OWNER_OFFSET 108
#define TAR_GROUP_OFFSET 116
#define TAR_SIZE_OFFSET 124
#define TAR_MTIME_OFFSET 136
#define TAR_CHECKSUM_OFFSET 148
#define TAR_LINK_IND_OFFSET 156
#define TAR_TARGET_OFFSET 157
#define TAR_USTAR_SIG_OFFSET 257
#define TAR_PREFIX_OFFSET 345

#define TAR_FILENAME_LEN 100
#define TAR_MODE_LEN 8
#define TAR_OWNER_LEN 8
#define TAR_GROUP_LEN 8
#define TAR_SIZE_LEN 12
#define TAR_MTIME_LEN 12
#define TAR_TARGET_LEN 100
#define TAR_PREFIX_LEN 155

typedef struct {
  /* Possibly including USTAR prefix */
  char filename[TAR_FILENAME_LEN + TAR_PREFIX_LEN + 1];
  char target[TAR_TARGET_LEN + 1];
  uid_t owner;
  gid_t group;
  mode_t mode;
  time_t mtime;
} tar_file_info;

typedef struct {
  unsigned long files_seen; /* Including the current one, if any */
  unsigned long long blocks_seen;
  unsigned long zero_blocks_seen;
  enum {
    TAR_READY,
    TAR_IN_FILE,
    TAR_DONE
  } state;
  union {
    struct {
      tar_file_info *f;
      char type;
      char curr_block[TAR_BLOCK_SIZE];
      unsigned long long bytes_seen;
      unsigned long long blocks_seen;
      unsigned long long bytes_total;
    } in_file;
  } u;
  read_stream *rs;
} tar_reader;

typedef struct {
  unsigned long filenum;
  tar_reader *tr;
} tar_read_stream;

void close_tar_reader( tar_reader * );
int get_next_file( tar_reader * );
read_stream * get_reader_for_file( tar_reader * );
tar_reader *start_tar_reader( read_stream * );

#endif /* __TAR_H__ */
