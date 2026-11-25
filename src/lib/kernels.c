#include "kernels.h"
#include "fixed_point.h"
#include "tables.h"
#include <stddef.h>

/* Each kernel body is inlined into one wrapper per instruction set, which
 * the compiler then vectorizes for that set. */
#if defined(__GNUC__) || defined(__clang__)
#define KERNEL_BODY static inline __attribute__((always_inline))
#else
#define KERNEL_BODY static inline
#endif

KERNEL_BODY void filter_body(const int32_t x[512], int off, int32_t s[32]) {
  int32_t y[64] = {0};

  /* Windowing. `off` is a multiple of 32, so each run of 32 samples is
   * contiguous in the circular buffer. */
  for (int j = 0; j < 8; j++)
    for (int half = 0; half < 2; half++) {
      const int32_t *in = &x[(off + 64 * j + 32 * half) & 511];
      const int32_t *window = &shine_enwindow[64 * j + 32 * half];
      int32_t *out = &y[32 * half];

      for (int i = 0; i < 32; i++)
        out[i] += mul(in[i], window[i]);
    }

  /* Matrixing. */
  for (int i = 0; i < 32; i++) {
    int32_t sum = 0;

    for (int j = 0; j < 64; j++)
      sum += mul(shine_subband_filter[i][j], y[j]);
    s[i] = sum;
  }
}

/* With the headroom of the tables, the sum of the absolute values of the
 * terms of a long coefficient is at most 0.705 * 2**31 for any input. */
KERNEL_BODY void mdct_body(const int32_t in[36], const int32_t table[18][36],
                           int32_t long_out[18], int32_t short_out[3][6]) {
  for (int k = 0; k < 18; k++) {
    int32_t sum = 0;

    for (int j = 0; j < 36; j++)
      sum += mul(in[j], table[k][j]);
    long_out[k] = sum;
  }
  if (!short_out)
    return;
  /* The three short windows cover samples 6..17, 12..23 and 18..29. */
  for (int window = 0; window < 3; window++)
    for (int k = 0; k < 6; k++) {
      int32_t sum = 0;

      for (int j = 0; j < 12; j++)
        sum += mul(in[6 + 6 * window + j], shine_mdct_cosine_short[k][j]);
      short_out[window][k] = sum;
    }
}

static void filter_plain(const int32_t x[512], int off, int32_t s[32]) {
  filter_body(x, off, s);
}

static void mdct_plain(const int32_t in[36], const int32_t table[18][36],
                       int32_t long_out[18], int32_t short_out[3][6]) {
  mdct_body(in, table, long_out, short_out);
}

const shine_kernels_t shine_kernels_plain = {filter_plain, mdct_plain};

#ifdef SHINE_KERNELS_AVX2
__attribute__((target("avx2"))) static void
filter_avx2(const int32_t x[512], int off, int32_t s[32]) {
  filter_body(x, off, s);
}

__attribute__((target("avx2"))) static void
mdct_avx2(const int32_t in[36], const int32_t table[18][36],
          int32_t long_out[18], int32_t short_out[3][6]) {
  mdct_body(in, table, long_out, short_out);
}

const shine_kernels_t shine_kernels_avx2 = {filter_avx2, mdct_avx2};
#endif

void shine_kernels_select(shine_kernels_t *kernels) {
  *kernels = shine_kernels_plain;
#ifdef SHINE_KERNELS_AVX2
  __builtin_cpu_init();
  if (__builtin_cpu_supports("avx2"))
    *kernels = shine_kernels_avx2;
#endif
}
