AC_INIT([shine],[3.1.1],[toots@rastageeks.org])
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT
AC_CONFIG_MACRO_DIR([m4])

dnl WebAssembly build, for the JavaScript package. Independent of CC: it needs
dnl a clang with the wasm32 target, wasm-ld and a wasi-libc sysroot.
AC_ARG_ENABLE([wasm],
  [AS_HELP_STRING([--enable-wasm],
    [enable the wasm and check-wasm targets (default: auto, when WASM_CC can
     target wasm32)])],
  [], [enable_wasm=auto])
AC_ARG_VAR([WASM_CC], [clang used for the WebAssembly build])
AC_ARG_VAR([WASI_SYSROOT], [wasi-libc sysroot for the WebAssembly build])
AC_ARG_VAR([NODE], [node, to test the WebAssembly build])

shine_wasm=no
WASM_FLAGS=
if test "$enable_wasm" != no; then
  AC_PATH_PROGS([WASM_CC], [clang])
  AC_MSG_CHECKING([for a WebAssembly toolchain])
  if test -n "$WASM_CC"; then
    cat > conftest_wasm.c <<EOF
#include <stdlib.h>
#include <math.h>
double probe(int n) { return cos((double)n) + (double)(size_t)malloc(n); }
EOF
    dnl An empty sysroot leaves the choice to the compiler, e.g. Debian's clang.
    for sysroot in "$WASI_SYSROOT" /opt/homebrew/share/wasi-sysroot \
                   /usr/local/share/wasi-sysroot /usr/share/wasi-sysroot ""; do
      for target in wasm32-wasip1 wasm32-wasi; do
        flags="--target=$target -nostartfiles -nodefaultlibs -Wl,--no-entry"
        test -n "$sysroot" && flags="$flags --sysroot=$sysroot"
        if $WASM_CC $flags -Wl,--export=probe conftest_wasm.c -lc \
             -o conftest_wasm.wasm >&AS_MESSAGE_LOG_FD 2>&1; then
          shine_wasm=yes
          WASM_FLAGS=$flags
          break 2
        fi
      done
    done
    rm -f conftest_wasm.c conftest_wasm.wasm
  fi
  AC_MSG_RESULT([$shine_wasm])
  if test "$shine_wasm" = no && test "$enable_wasm" = yes; then
    AC_MSG_ERROR([no WebAssembly toolchain: set WASM_CC to a clang with wasm-ld in its PATH, and WASI_SYSROOT to a wasi-libc sysroot])
  fi
  AC_PATH_PROGS([NODE], [node])
fi
AC_SUBST([WASM_FLAGS])
AM_CONDITIONAL([HAVE_WASM], [test "$shine_wasm" = yes])

AC_OUTPUT([Makefile shine.pc])
