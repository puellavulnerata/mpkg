#ifndef __EMIT_H__
#define __EMIT_H__

#include <streams.h>
#include <tar.h>

#include <sys/types.h>
#include <time.h>

#define EMIT_SUCCESS 0
#define EMIT_ERROR -1

typedef enum {
  NONE,
#ifdef COMPRESSION_GZIP
  GZIP,
#endif
#ifdef COMPRESSION_BZIP2
  BZIP2,
#endif
  DEFAULT_COMPRESSION
} emit_compression_opt;

typedef enum {
#ifdef PKGFMT_V1
    V1,
#endif
#ifdef PKGFMT_V2
    V2,
#endif
    DEFAULT_VERSION
} emit_version_opt;

typedef struct {
  char *output_file;
  time_t pkg_mtime;
  emit_compression_opt compression;
  emit_version_opt version;
} emit_opts;

typedef struct {
  /* Bare ws for the output file */
  write_stream *out_ws;
  /*
   * Compressed ws wrapped around out_ws for V1, and around content_ws
   * during content emission for V2
   */
  write_stream *comp_ws;
  /*
   * Either out_ws or comp_ws, depending on version and compression.
   * We attach the outer tar_writer to this.
   */
  write_stream *ws;
  /* The outer tar_writer for V2, the only tar_writer for V1 */
  tar_writer *pkg_tw;
#ifdef PKGFMT_V2
  /* tar_file_info for the content tarball's entry in the outer tarball */
  tar_file_info ti_outer;
  /* The bare ws for the package-content.tar.{|gz|bz2} file */
  write_stream *content_out_ws;
  /* We use comp_ws for the compressed ws around it in the V2 case */
  /* content_ws is either comp_ws or content_out_ws, as appropriate */
  write_stream *content_ws;
#endif /* PKGFMT_V2 */
  /*
   * This is the tar_writer to emit files to.  It will be just pkg_tw
   * for V1, and will be the inner tar_writer for V2
   */
  tar_writer *emit_tw;
} emit_pkg_streams;

void finish_pkg_content( emit_opts *, emit_pkg_streams * );
void finish_pkg_streams( emit_opts *, emit_pkg_streams * );
void free_emit_opts( emit_opts * );
emit_compression_opt get_compression( emit_opts * );
emit_version_opt get_version( emit_opts * );
void guess_compression_and_version_from_filename( emit_opts * );
int start_pkg_content( emit_opts *, emit_pkg_streams * );
emit_pkg_streams * start_pkg_streams( emit_opts * );

#endif /* !defined(__EMIT_H__) */
