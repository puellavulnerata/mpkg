include config.mk

.PHONY: all clean strip

all:
	$(MAKE) -C src all

clean:
	$(MAKE) -C src clean

strip:
	$(MAKE) -C src strip
