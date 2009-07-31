include ../config.mk

INCLUDE_FLAGS=-I../include
LIBS=

OBJS=\
	convert.o convertdb.o create.o createdb.o dumpdb.o emit.o install.o \
	md5.o pkg.o pkgdb.o pkgdb_text_file.o pkgdescr.o pkgglobal.o \
	pkgpath.o pkgutil.o rbtree.o remove.o repairdb.o repairdb_pass1.o \
	repairdb_pass2.o streams.o streams_none.o tar.o unpack.o

ifdef CONFIG_BDB
	OBJS+=pkgdb_bdb.o
endif

ifdef CONFIG_BZIP2
	OBJS+=streams_bzip2.o
endif

ifdef CONFIG_GZIP
	OBJS+=streams_gzip.o
endif

.PHONY: all clean

all: mpkg

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDE_FLAGS)

mpkg: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f mpkg $(OBJS)
