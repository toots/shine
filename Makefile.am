.PHONY : android

EXTRA_DIST = ChangeLog README.md README.old libshine.sym js

noinst_HEADERS = src/bin/main.h src/bin/wave.h \
                 src/lib/bitstream.h src/lib/huffman.h src/lib/l3bitstream.h \
                 src/lib/l3loop.h src/lib/l3mdct.h src/lib/l3subband.h \
                 src/lib/mult_mips_gcc.h src/lib/mult_noarch_gcc.h \
                 src/lib/mult_sarm_gcc.h src/lib/reservoir.h src/lib/tables.h \
                 src/lib/types.h

AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS  = -I m4

CFLAGS = @CFLAGS@ -funroll-loops -fno-exceptions -Wall -O2 -fsigned-char

lib_LTLIBRARIES      = libshine.la
libshine_la_SOURCES  = src/lib/bitstream.c src/lib/huffman.c \
                       src/lib/l3bitstream.c src/lib/l3loop.c src/lib/l3mdct.c \
                       src/lib/l3subband.c src/lib/layer3.c src/lib/reservoir.c \
                       src/lib/tables.c

libshine_la_LDFLAGS  = -lm -no-undefined -version-info 4:0:1 -export-symbols $(srcdir)/libshine.sym
libshine_ladir       = ${prefix}/include/shine
libshine_la_HEADERS  = src/lib/layer3.h

bin_PROGRAMS     = shineenc
shineenc_SOURCES = src/bin/main.c src/bin/wave.c
shineenc_LDADD   = libshine.la
shineenc_CFLAGS  = -I$(srcdir)/src/lib

# One program per concern. A new test needs its program here and its
# _SOURCES line below.
TESTS          = tests/framing tests/flush tests/wave tests/cli
check_PROGRAMS = $(TESTS)
TEST_HELPERS   = tests/helpers.c tests/helpers.h
LDADD          = libshine.la
AM_CPPFLAGS    = -I$(srcdir)/src/lib

tests_framing_SOURCES = tests/framing.c $(TEST_HELPERS)
tests_flush_SOURCES   = tests/flush.c $(TEST_HELPERS)
tests_wave_SOURCES    = tests/wave.c $(TEST_HELPERS)
tests_cli_SOURCES     = tests/cli.c $(TEST_HELPERS)

AM_TESTS_ENVIRONMENT = SHINEENC='$(abs_top_builddir)/shineenc'; export SHINEENC;

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = shine.pc

android:
	[ -d android/include ] || mkdir android/include
	[ -d android/lib ] || mkdir android/lib
	[ -d android/bin ] || mkdir android/bin
	cp src/lib/layer3.h android/include
	NDK_PROJECT_PATH=$(shell pwd) ndk-build APP_BUILD_SCRIPT=$(shell pwd)/android/Library.mk
	mv libs/* android/lib
	NDK_PROJECT_PATH=$(shell pwd) ndk-build APP_BUILD_SCRIPT=$(shell pwd)/android/Binary.mk
	mv libs/* android/bin

clean-local:
	-rm -rf obj libs android/lib android/bin android/include
