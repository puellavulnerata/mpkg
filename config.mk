CC=/usr/bin/gcc-3.4.5 -m32 -mcpu=ultrasparc
CFLAGS=-O2 -g -Werror -DCOMPRESSION_GZIP -DCOMPRESSION_BZIP2 -DPKGFMT_V1 -DPKGFMT_V2
LDFLAGS=-ldb -lz -lbz2
