#ifndef KERNELS_H
#define KERNELS_H

#include <stdint.h>

/* The filter bank's inner loops, chosen once per encoder. On x86-64 with
 * GCC or clang they are also built for AVX2, which the compiler vectorizes:
 * the baseline instruction set has no signed 32x32->64-bit vector multiply.
 * Every build gives the same output: each product rounds the same way, and
 * 32-bit sums wrap the same in any order. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define SHINE_KERNELS_AVX2 1
#endif

typedef struct {
  /* The 32 subband samples of the analysis filter from the window buffer x
   * (HAN_SIZE samples), its newest 32 at offset `off`, a multiple of 32. */
  void (*filter)(const int32_t x[512], int off, int32_t s[32]);
  /* The long MDCT of 36 subband samples with the window of `table`, and the
   * three short ones when `short_out` is not NULL. */
  void (*mdct)(const int32_t in[36], const int32_t table[18][36],
               int32_t long_out[18], int32_t short_out[3][6]);
} shine_kernels_t;

extern const shine_kernels_t shine_kernels_plain;
#ifdef SHINE_KERNELS_AVX2
extern const shine_kernels_t shine_kernels_avx2;
#endif

/* The fastest kernels the processor runs. */
void shine_kernels_select(shine_kernels_t *kernels);

#endif
