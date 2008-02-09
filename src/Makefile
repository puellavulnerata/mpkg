include ../config.mk

INCLUDE_FLAGS=-I../include
LIBS=

OBJS=pkg.o pkgdescr.o

.PHONY: all clean

all: pkg

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDE_FLAGS)

pkg: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f pkg $(OBJS)
