#ifndef KERNELS_H
#define KERNELS_H

#include <stdint.h>

/* The hot array arithmetic of the encoder, in sets of functions, one set
 * chosen per encoder. The plain set is the reference: portable C, which
 * defines the result. The other sets, each for an instruction set
 * extension, give the same output, bit for bit:
 *   avx2  x86-64 with AVX2, built with GCC 9 or later or clang and chosen
 *         when the processor has AVX2 (kernels_x86.c)
 *   neon  arm64 with vector registers, built with GCC 9 or later or clang
 *         (kernels_arm.c)
 * The filter bank of each set is the same body built on its own vectors
 * (kernels_lanes.h). A set leaves NULL the other kernels that it has no
 * version of, the plain set all of them: their callers then inline the
 * reference (kernels_body.h), as a call through the table would take
 * longer than the loop.
 *
 * The environment variable SHINE_KERNELS, read when an encoder is
 * initialised, forces a set by name. A set that is unknown, or not built
 * for or not run by the processor, makes the initialisation fail. */

/* Samples under the analysis windows of the 18 filter steps of a granule. */
#define SHINE_FILTER_INPUT (512 + 17 * 32)

typedef struct {
  /* The analysis filter over a granule. x holds its 576 samples then the
   * 480 before them, newest first, in Q31: step t is under the window at
   * x + 32 (17 - t). s gets the 32 subband samples of each step. */
  void (*filter)(const int32_t x[SHINE_FILTER_INPUT], int32_t s[18][32]);
  /* The long MDCT of each subband of a granule under `window`, from its 18
   * previous and 18 current samples, and the three short ones, 6 lines per
   * subband and window, when `short_out` is not NULL. */
  void (*mdct)(const int32_t previous[18][32], const int32_t current[18][32],
               const int32_t window[36], int32_t long_out[32][18],
               int32_t (*short_out)[192]);
  /* The largest of n quantised values, and in *nonzero how many are not
   * 0. */
  unsigned (*ix_max_nonzero)(const int *ix, int n, int *nonzero);
} shine_kernels_t;

extern const shine_kernels_t shine_kernels_plain;

/* Each set of an extension, or NULL when the build or the processor does
 * not have it. */
const shine_kernels_t *shine_kernels_avx2(void);
const shine_kernels_t *shine_kernels_neon(void);

/* The name of set `index`, from the most preferred, NULL past the last. */
const char *shine_kernels_name(int index);

/* The set of that name, NULL when it is not available. */
const shine_kernels_t *shine_kernels_named(const char *name);

/* The set SHINE_KERNELS names, otherwise the first available. Returns -1
 * when SHINE_KERNELS names a set that is not available. */
int shine_kernels_select(shine_kernels_t *kernels);

#endif
