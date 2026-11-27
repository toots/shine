/* What every kernel file shares: the scalar parts of the kernels, written
 * once and built for the target of the file or function that inlines them.
 * The bodies on the lane type of an instruction set extension are built
 * for it (KERNEL_TARGET), as they pass its vectors by value. */
#ifndef KERNELS_BODY_H
#define KERNELS_BODY_H

#include "kernels.h"
#include "tables.h"
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define KERNEL_INLINE static inline __attribute__((always_inline))
#else
#define KERNEL_INLINE static inline
#endif
#define KERNEL_TARGET
#define KERNEL_BODY KERNEL_INLINE KERNEL_TARGET

/* 2**31 sin(pi / 3) */
#define SIN_PI_3 1859775393

/* cos and sin of pi / 4 and 3 pi / 4, in Q31. */
static const int32_t eighth_turn[2] = {1518500250, 1518500250};
static const int32_t three_eighths_turn[2] = {-1518500250, 1518500250};

/* a[0..32] of each step, on the vector lanes. */
#define FOLDED_WIDTH 33

/* value / 2**32, rounded. */
KERNEL_INLINE int32_t round_half(int64_t value) {
  return (int32_t)((value + ((int64_t)1 << 31)) >> 32);
}

/* 64-bit windowed sums. */
KERNEL_INLINE void filter_window(const int32_t x[512], int64_t y[64]) {
  for (int i = 0; i < 64; i++)
    y[i] = 0;
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 64; i++)
      y[i] += (int64_t)x[64 * j + i] * shine_enwindow[64 * j + i];
}

/* The matrixing cos((2i + 1) (16 - j) pi / 64) is even about j = 16 and odd
 * about j = 48: a[n] collects the terms in cos((2i + 1) n pi / 64). */
KERNEL_INLINE void filter_fold(const int64_t y[64], int32_t a[32]) {
  a[0] = round_half(y[16]);
  for (int n = 1; n <= 16; n++)
    a[n] = round_half(y[16 + n] + y[16 - n]);
  for (int n = 17; n < 32; n++)
    a[n] = round_half(y[16 + n] - y[80 - n]);
}

/* The references of the kernels a set may leave NULL, and their calls
 * through a set. */
KERNEL_INLINE unsigned ix_max_nonzero(const int *ix, int n, int *nonzero) {
  unsigned max = 0;
  int count = 0;

  for (int i = 0; i < n; i++) {
    max = (unsigned)ix[i] > max ? (unsigned)ix[i] : max;
    count += ix[i] != 0;
  }
  *nonzero = count;
  return max;
}

KERNEL_INLINE unsigned call_ix_max_nonzero(const shine_kernels_t *kernels,
                                           const int *ix, int n, int *nonzero) {
  return kernels->ix_max_nonzero ? kernels->ix_max_nonzero(ix, n, nonzero)
                                 : ix_max_nonzero(ix, n, nonzero);
}

#endif
