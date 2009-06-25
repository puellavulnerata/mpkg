#ifndef __PKG_TYPES_H__
#define __PKG_TYPES_H__

typedef enum {
  NONE,
#ifdef COMPRESSION_GZIP
  GZIP,
#endif /* COMPRESSION_GZIP */
#ifdef COMPRESSION_BZIP2
  BZIP2,
#endif /* COMPRESSION_BZIP2 */
  DEFAULT_COMPRESSION
} pkg_compression_t;

typedef enum {
#ifdef PKGFMT_V1
  V1,
#endif /* PKGFMT_V1 */
#ifdef PKGFMT_V2
  V2,
#endif /* PKGFMT_V2 */
  DEFAULT_VERSION
} pkg_version_t;

#endif /* __PKG_TYPES_H__ */
