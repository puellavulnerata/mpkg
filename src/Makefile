include ../config.mk

INCLUDE_FLAGS=-I../include
LIBS=

OBJS=\
	compression_bzip2.o compression_gzip.o compression_none.o \
	compression.o pkg.o pkgdb.o pkgdb_bdb.o pkgdb_text_file.o pkgdescr.o \
	pkgpath.o pkgutil.o rbtree.o

.PHONY: all clean

all: pkg

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDE_FLAGS)

pkg: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f pkg $(OBJS)
