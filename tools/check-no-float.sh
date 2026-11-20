#!/bin/sh
# Fails if a library uses floating point. Build it for arm64 with
# CFLAGS=-mgeneral-regs-only first: the compiler then turns every floating
# point operation into a call to a soft-float helper (__adddf3, __fixdfsi...),
# so the library's undefined symbols show any use, along with libm calls.
#
# tools/check-no-float.sh .libs/libshine.a

set -e
library=${1:?usage: $0 library.a}
undefined=$(nm -u "$library" | awk '{ print $NF }' | sort -u)

# Guard against an empty listing passing the check.
echo "$undefined" | grep -qE '^_*malloc$' || {
  echo "$library: no undefined symbols found, nothing was checked" >&2
  exit 1
}

soft_float='^_*__[a-z]*(sf|df|tf)[a-z0-9]*$'
libm='^_*(sin|cos|tan|pow|exp|exp2|log|log2|log10|sqrt|modf|floor|ceil|fabs|round|lround|trunc|fmod)f?$'
float=$(echo "$undefined" | grep -E "$soft_float|$libm" || true)

if [ -n "$float" ]; then
  echo "$library uses floating point:" $float >&2
  exit 1
fi
echo "$library: no floating point"
