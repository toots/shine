AUTOMAKE_OPTIONS = foreign

lib_LTLIBRARIES      = libshine.la
libshine_la_SOURCES  = src/lib/bitstream.c src/lib/formatbits.c src/lib/huffman.c \
											 src/lib/l3bitstream.c src/lib/l3loop.c src/lib/l3mdct.c \
											 src/lib/l3subband.c src/lib/layer3.c src/lib/reservoir.c \
											 src/lib/tables.c
libshine_la_LDFLAGS  = -version-info 1:0:0
libshine_ladir       = shine
libshine_la_HEADERS  = src/lib/bitstream.h src/lib/formatbits.h src/lib/g_includes.h \
											 src/lib/huffman.h src/lib/l3bitstream.h src/lib/l3loop.h \
											 src/lib/l3mdct.h src/lib/l3subband.h src/lib/layer3.h \
											 src/lib/reservoir.h src/lib/tables.h src/lib/types.h src/lib/wave.h

bin_PROGRAMS     = shineenc
shineenc_SOURCES = src/bin/main.c src/bin/wave.c
shineenc_LDADD   = libshine.la
shineenc_CFLAGS  = -Isrc/lib
