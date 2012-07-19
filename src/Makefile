#
# Shine_Fixed GENERIC PLATFORM GCC port Makefile
#

XPREFIX=
CXX = $(XPREFIX)g++
CC = $(XPREFIX)gcc
STRIP = $(XPREFIX)strip
AR = $(XPREFIX)ar
CFLAGS = -fno-exceptions -Wall -O2 -fsigned-char -m32
INCLUDE = -I.
LIBS = -lm
LIBDIRS =


APPNAME = shineenc
STNAME = libshine-fxp.la
SONAME = libshine-fxp.so.1
LIBNAME = $(SONAME).0.0

C_SRCS = $(wildcard *.c)
OBJFILES = $(C_SRCS:.c=.o)

#don't need wav or main in library
LIBOBJS = $(shell echo $(OBJFILES) | sed -e "s/main.o//" -e "s/wave.o//")

all: $(APPNAME)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -c $<

default: $(OBJFILES) $(APPNAME) library

library: $(LIBOJBS)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$(SONAME) -o $(LIBNAME) $(LIBOBJS) $(LIBDIRS) -lc -lm
	$(AR) rcs $(STNAME) $(LIBOBJS)

$(APPNAME): $(OBJFILES)
	$(CC) $(CFLAGS) $(INCLUDE) -o $(APPNAME) $(OBJFILES) $(LIBDIRS) $(LIBS)

release:
	$(STRIP) -s $(APPNAME)
	$(STRIP) -s $(LIBNAME)

cleanobj:
	rm -f $(OBJFILES)

clean: cleanobj
	rm -f *.la *.so* $(APPNAME)