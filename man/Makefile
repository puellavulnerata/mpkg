include ../config.mk

.PHONY: all clean install

all:
# Nothing to do for all

clean:
# Nothing to do for clean

install: mpkg.1
	$(INSTALL) -m 0755 -d $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m 0644 mpkg.1 $(DESTDIR)$(MANDIR)/man1
