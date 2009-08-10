# Change these to change mpkg configuration

CONFIG_BZIP2=1
CONFIG_GZIP=1
CONFIG_PKGFMT_V1=1
CONFIG_PKGFMT_V2=1
CONFIG_BDB=1
CONFIG_MD5_DEFAULT=1
CONFIG_MTRACE=0

# Try BDB_INCLUDE=-I/usr/local/include/db4 and BDB_LIBS=-L/usr/local/lib
# for OpenBSD

BDB_INCLUDE=
BDB_LIBS=

# Try BZIP2_INCLUDE=-I/usr/local/include and BDB_LIBS=-L/usr/local/lib
# for OpenBSD

BZIP2_INCLUDE=
BZIP2_LIBS=

GZIP_INCLUDE=
GZIP_LIBS=

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
	CFLAGS+=$(BZIP2_INCLUDE)
endif

ifeq ($(CONFIG_GZIP),1)
	CFLAGS+=-DCOMPRESSION_GZIP
	CFLAGS+=$(GZIP_INCLUDE)
endif

ifeq ($(CONFIG_PKGFMT_V1),1)
	CFLAGS+=-DPKGFMT_V1
endif

ifeq ($(CONFIG_PKGFMT_V2),1)
	CFLAGS+=-DPKGFMT_V2
endif

ifeq ($(CONFIG_BDB),1)
	CFLAGS+=-DDB_BDB
	CFLAGS+=$(BDB_INCLUDE)
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
	LDFLAGS+=$(BDB_LIBS)
	LDFLAGS+=-ldb
endif

ifeq ($(CONFIG_BZIP2),1)
	LDFLAGS+=$(BZIP2_LIBS)
	LDFLAGS+=-lbz2
endif

ifeq ($(CONFIG_GZIP),1)
	LDFLAGS+=$(GZIP_LIBS)
	LDFLAGS+=-lz
endif
