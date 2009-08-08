include config.mk

.PHONY: all clean dist strip

all:
	$(MAKE) -C src all

clean:
	$(MAKE) -C src clean

strip:
	$(MAKE) -C src strip

DIST_DIR=dist
DIST_NAME=mpkg-0.1
DIST_STAGING=$(DIST_DIR)/$(DIST_NAME)

dist:
	$(MKDIR) -p $(DIST_STAGING) $(DIST_STAGING)/include \
		$(DIST_STAGING)/man $(DIST_STAGING)/src
	$(LN) -f Makefile config.mk $(DIST_STAGING)
	$(LN) -f include/*.h $(DIST_STAGING)/include
	$(LN) -f man/mpkg.1 $(DIST_STAGING)/man
	$(LN) -f src/Makefile src/*.c $(DIST_STAGING)/src
	$(TAR) -C $(DIST_DIR) -cvf - $(DIST_NAME) | $(GZIP) > \
		mpkg-0.1.tar.gz
