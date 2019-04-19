AC_INIT([shine],[3.1.1],[toots@rastageeks.org])
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT
AC_CONFIG_MACRO_DIR([m4])
AC_C_BIGENDIAN([CFLAGS="$CFLAGS -DSHINE_BIG_ENDIAN"])
AC_CHECK_HEADER([byteswap.h],[CFLAGS="$CFLAGS -DSHINE_HAVE_BSWAP_H"])
AC_OUTPUT([Makefile shine.pc])
