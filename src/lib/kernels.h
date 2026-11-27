#ifndef KERNELS_H
#define KERNELS_H

#include <stdint.h>

/* The filter bank's inner loops, chosen once per encoder. On x86-64 with
 * GCC or clang they are also built for AVX2, which the compiler vectorizes:
 * the baseline instruction set has no signed 32x32->64-bit vector multiply.
 * Every build gives the same output: the sums of products are exact in 64
 * bits, in any order, and each is rounded once. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define SHINE_KERNELS_AVX2 1
#endif

/* On arm64 with GCC or clang they are also built on vectors of 4 lanes, each
 * lane one filter step or one subband. */
#if defined(__aarch64__) && (defined(__clang__) || __GNUC__ >= 9)
#define SHINE_KERNELS_NEON 1
#endif

/* The filter steps of a granule, padded to a multiple of the lanes. */
#define SHINE_FOLDED_STEPS 20

typedef struct {
  /* Step `step` of a granule: the window buffer x (HAN_SIZE samples), its
   * newest 32 at offset `off`, a multiple of 32, windowed and folded to the
   * 32 inputs folded[.][step] of the filter's cosine transform. */
  void (*filter_fold)(const int32_t x[512], int off, int step,
                      int32_t folded[32][SHINE_FOLDED_STEPS]);
  /* The 32 subband samples of each of the 18 steps of a granule. Every
   * entry of `folded` is read. */
  void (*filter_dct3)(const int32_t folded[32][SHINE_FOLDED_STEPS],
                      int32_t s[18][32]);
  /* The long MDCT of each subband of a granule under `window`, from its 18
   * previous and 18 current samples, and the three short ones, 6 lines per
   * subband and window, when `short_out` is not NULL. */
  void (*mdct)(const int32_t previous[18][32], const int32_t current[18][32],
               const int32_t window[36], int32_t long_out[32][18],
               int32_t (*short_out)[192]);
} shine_kernels_t;

extern const shine_kernels_t shine_kernels_plain;
#ifdef SHINE_KERNELS_AVX2
extern const shine_kernels_t shine_kernels_avx2;
#endif
#ifdef SHINE_KERNELS_NEON
extern const shine_kernels_t shine_kernels_neon;
#endif

/* The fastest kernels the processor runs. */
void shine_kernels_select(shine_kernels_t *kernels);

#endif
