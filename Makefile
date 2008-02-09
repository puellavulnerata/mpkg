include config.mk

.PHONY: all clean

all:
	$(MAKE) -C src all

clean:
	$(MAKE) -C src clean
