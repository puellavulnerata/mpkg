include ../config.mk

INCLUDE_FLAGS=-I../include
LIBS=

OBJS=\
	md5.o pkg.o pkgdb.o pkgdb_bdb.o pkgdb_text_file.o pkgdescr.o \
	pkgpath.o pkgutil.o rbtree.o streams_bzip2.o streams_gzip.o \
	streams_none.o streams.o tar.o unpack.o

.PHONY: all clean

all: mpkg

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDE_FLAGS)

mpkg: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f mpkg $(OBJS)
