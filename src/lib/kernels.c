#include "kernels.h"
#include "tables.h"
#include <string.h>

/* Each kernel body is inlined into one wrapper per instruction set, which
 * the compiler then vectorizes for that set. */
#if defined(__GNUC__) || defined(__clang__)
#define KERNEL_BODY static inline __attribute__((always_inline))
#else
#define KERNEL_BODY static inline
#endif

/* 2**31 sin(pi / 3) */
#define SIN_PI_3 1859775393

/* cos and sin of pi / 4 and 3 pi / 4, in Q31. */
static const int32_t eighth_turn[2] = {1518500250, 1518500250};
static const int32_t three_eighths_turn[2] = {-1518500250, 1518500250};

/* value / 2**32, rounded. */
KERNEL_BODY int32_t round_half(int64_t value) {
  return (int32_t)((value + ((int64_t)1 << 31)) >> 32);
}

/* 64-bit windowed sums. As `off` is a multiple of 32, each run of 32 samples
 * is contiguous in the circular buffer. */
KERNEL_BODY void filter_window(const int32_t x[512], int off, int64_t y[64]) {
  for (int i = 0; i < 64; i++)
    y[i] = 0;
  for (int j = 0; j < 8; j++)
    for (int half = 0; half < 2; half++) {
      const int32_t *in = &x[(off + 64 * j + 32 * half) & 511];
      const int32_t *window = &shine_enwindow[64 * j + 32 * half];
      int64_t *out = &y[32 * half];

      for (int i = 0; i < 32; i++)
        out[i] += (int64_t)in[i] * window[i];
    }
}

/* The matrixing cos((2i + 1) (16 - j) pi / 64) is even about j = 16 and odd
 * about j = 48: a[n] collects the terms in cos((2i + 1) n pi / 64). */
KERNEL_BODY void filter_fold(const int64_t y[64], int32_t *a, int stride) {
  a[0] = round_half(y[16]);
  for (int n = 1; n <= 16; n++)
    a[n * stride] = round_half(y[16 + n] + y[16 - n]);
  for (int n = 17; n < 32; n++)
    a[n * stride] = round_half(y[16 + n] - y[80 - n]);
}

KERNEL_BODY void filter_fold_body(const int32_t x[512], int off, int step,
                                  int32_t folded[32][SHINE_FOLDED_STEPS]) {
  int64_t y[64];

  filter_window(x, off, y);
  filter_fold(y, &folded[0][step], SHINE_FOLDED_STEPS);
}

#define LANES 1
#define LANE_T int32_t
#define WIDE_T int64_t
#define WIDEN(x) ((int64_t)(x))
#define NARROW(x) ((int32_t)(x))
#define LANE(v, l) (v)
#define FN(name) name##_x1
#include "kernels_lanes.h"

#ifdef SHINE_KERNELS_NEON
typedef int32_t lanes4_t __attribute__((vector_size(16)));
typedef int64_t wide4_t __attribute__((vector_size(32)));

#define LANES 4
#define LANE_T lanes4_t
#define WIDE_T wide4_t
#define WIDEN(x) __builtin_convertvector(x, wide4_t)
#define NARROW(x) __builtin_convertvector(x, lanes4_t)
#define LANE(v, l) (v)[l]
#define FN(name) name##_x4
#include "kernels_lanes.h"
#endif

static void filter_fold_plain(const int32_t x[512], int off, int step,
                              int32_t folded[32][SHINE_FOLDED_STEPS]) {
  filter_fold_body(x, off, step, folded);
}

static void filter_dct3_plain(const int32_t folded[32][SHINE_FOLDED_STEPS],
                              int32_t s[18][32]) {
  filter_dct3_steps_x1(folded, s);
}

static void mdct_plain(const int32_t previous[18][32],
                       const int32_t current[18][32], const int32_t window[36],
                       int32_t long_out[32][18], int32_t (*short_out)[192]) {
  mdct_bands_x1(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_plain = {filter_fold_plain,
                                             filter_dct3_plain, mdct_plain};

#ifdef SHINE_KERNELS_AVX2
#define TARGET_AVX2 __attribute__((target("avx2")))

TARGET_AVX2 static void
filter_fold_avx2(const int32_t x[512], int off, int step,
                 int32_t folded[32][SHINE_FOLDED_STEPS]) {
  filter_fold_body(x, off, step, folded);
}

TARGET_AVX2 static void
filter_dct3_avx2(const int32_t folded[32][SHINE_FOLDED_STEPS],
                 int32_t s[18][32]) {
  filter_dct3_steps_x1(folded, s);
}

TARGET_AVX2 static void mdct_avx2(const int32_t previous[18][32],
                                  const int32_t current[18][32],
                                  const int32_t window[36],
                                  int32_t long_out[32][18],
                                  int32_t (*short_out)[192]) {
  mdct_bands_x1(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_avx2 = {filter_fold_avx2, filter_dct3_avx2,
                                            mdct_avx2};
#endif

#ifdef SHINE_KERNELS_NEON
static void filter_dct3_neon(const int32_t folded[32][SHINE_FOLDED_STEPS],
                             int32_t s[18][32]) {
  filter_dct3_steps_x4(folded, s);
}

static void mdct_neon(const int32_t previous[18][32],
                      const int32_t current[18][32], const int32_t window[36],
                      int32_t long_out[32][18], int32_t (*short_out)[192]) {
  mdct_bands_x4(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_neon = {filter_fold_plain, filter_dct3_neon,
                                            mdct_neon};
#endif

void shine_kernels_select(shine_kernels_t *kernels) {
  *kernels = shine_kernels_plain;
#ifdef SHINE_KERNELS_AVX2
  __builtin_cpu_init();
  if (__builtin_cpu_supports("avx2"))
    *kernels = shine_kernels_avx2;
#endif
#ifdef SHINE_KERNELS_NEON
  *kernels = shine_kernels_neon;
#endif
}
