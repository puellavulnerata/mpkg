# Change these to change mpkg configuration

CONFIG_BZIP2=1
CONFIG_GZIP=1
CONFIG_PKGFMT_V1=1
CONFIG_PKGFMT_V2=1
CONFIG_BDB=1
CONFIG_MD5_DEFAULT=1
CONFIG_MTRACE=0

# Set these to the appropriate commands for your platform, or override them
# on the command line.

CC=cc
GZIP=gzip -9
INSTALL=install
LN=ln
MKDIR=mkdir
RM=rm
STRIP=strip
TAR=tar

# Installation-related defaults

DESTDIR=
PREFIX=/usr
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/share/man

# Adjust this appropriately too

CFLAGS=-O2 -g -Werror

CFLAGS+=-DBUILD_DATE=\"$(shell date +%Y-%m-%d)\"

ifeq ($(CONFIG_BZIP2),1)
	CFLAGS+=-DCOMPRESSION_BZIP2
endif

ifeq ($(CONFIG_GZIP),1)
	CFLAGS+=-DCOMPRESSION_GZIP
endif

ifeq ($(CONFIG_PKGFMT_V1),1)
	CFLAGS+=-DPKGFMT_V1
endif

ifeq ($(CONFIG_PKGFMT_V2),1)
	CFLAGS+=-DPKGFMT_V2
endif

ifeq ($(CONFIG_BDB),1)
	CFLAGS+=-DDB_BDB
endif

ifeq ($(CONFIG_MD5_DEFAULT),1)
	CFLAGS+=-DCHECK_MD5_DEFAULT
endif

ifeq ($(CONFIG_MTRACE),1)
	CFLAGS+=-DUSE_MTRACE
endif

# Put any LDFLAGS you need here

LDFLAGS=

ifeq ($(CONFIG_BDB),1)
	LDFLAGS+=-ldb
endif

ifeq ($(CONFIG_BZIP2),1)
	LDFLAGS+=-lbz2
endif

ifeq ($(CONFIG_GZIP),1)
	LDFLAGS+=-lz
endif
