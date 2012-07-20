lib_LTLIBRARIES      = libshine.la
libshine_la_SOURCES  = bitstream.c formatbits.c huffman.c l3bitstream.c l3loop.c l3mdct.c l3subband.c layer3.c reservoir.c tables.c 
libshine_la_LDFLAGS  = -version-info 1:0:0
libshine_ladir       = shine
libshine_la_HEADERS  = bitstream.h formatbits.h g_includes.h huffman.h l3bitstream.h l3loop.h l3mdct.h l3subband.h layer3.h mult_noarch_gcc.h mult_sarm_gcc.h reservoir.h tables.h types.h wave.h

bin_PROGRAMS     = shineenc
shineenc_SOURCES = main.c wave.c
shineenc_LDADD   = libshine.la
