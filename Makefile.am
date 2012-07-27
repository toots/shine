AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS  = -I m4

CFLAGS = @CFLAGS@ -fno-exceptions -Wall -O2 -fsigned-char

lib_LTLIBRARIES      = libshine.la
libshine_la_SOURCES  = src/lib/bitstream.c src/lib/formatbits.c src/lib/huffman.c \
                       src/lib/l3bitstream.c src/lib/l3loop.c src/lib/l3mdct.c \
                       src/lib/l3subband.c src/lib/layer3.c src/lib/reservoir.c \
                       src/lib/tables.c

libshine_la_LDFLAGS  = -version-info 1:0:0
libshine_ladir       = ${prefix}/include/shine
libshine_la_HEADERS  = src/lib/layer3.h

bin_PROGRAMS     = shineenc
shineenc_SOURCES = src/bin/main.c src/bin/wave.c
shineenc_LDADD   = libshine.la
shineenc_CFLAGS  = -Isrc/lib
shineenc_LDFLAGS = -lm

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = shine.pc
