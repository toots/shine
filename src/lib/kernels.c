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

/* 64-bit windowed sums. */
KERNEL_BODY void filter_window(const int32_t x[512], int64_t y[64]) {
  for (int i = 0; i < 64; i++)
    y[i] = 0;
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 64; i++)
      y[i] += (int64_t)x[64 * j + i] * shine_enwindow[64 * j + i];
}

/* The matrixing cos((2i + 1) (16 - j) pi / 64) is even about j = 16 and odd
 * about j = 48: a[n] collects the terms in cos((2i + 1) n pi / 64). */
KERNEL_BODY void filter_fold(const int64_t y[64], int32_t a[32]) {
  a[0] = round_half(y[16]);
  for (int n = 1; n <= 16; n++)
    a[n] = round_half(y[16 + n] + y[16 - n]);
  for (int n = 17; n < 32; n++)
    a[n] = round_half(y[16 + n] - y[80 - n]);
}

/* a[0..32] of each step, on the vector lanes. */
#define FOLDED_WIDTH 33

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

/* One step at a time: the compiler vectorizes the windowed sums where the
 * instruction set has the multiply. */
KERNEL_BODY void filter_steps(const int32_t *x, int32_t s[18][32]) {
  for (int step = 0; step < 18; step++) {
    int64_t y[64];
    int32_t a[32];

    filter_window(&x[32 * (17 - step)], y);
    filter_fold(y, a);
    filter_dct3_32_x1(a, s[step]);
  }
}

static void filter_plain(const int32_t *x, int32_t s[18][32]) {
  filter_steps(x, s);
}

static void mdct_plain(const int32_t previous[18][32],
                       const int32_t current[18][32], const int32_t window[36],
                       int32_t long_out[32][18], int32_t (*short_out)[192]) {
  mdct_bands_x1(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_plain = {filter_plain, mdct_plain};

#ifdef SHINE_KERNELS_AVX2
#define TARGET_AVX2 __attribute__((target("avx2")))

TARGET_AVX2 static void filter_avx2(const int32_t *x, int32_t s[18][32]) {
  filter_steps(x, s);
}

TARGET_AVX2 static void mdct_avx2(const int32_t previous[18][32],
                                  const int32_t current[18][32],
                                  const int32_t window[36],
                                  int32_t long_out[32][18],
                                  int32_t (*short_out)[192]) {
  mdct_bands_x1(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_avx2 = {filter_avx2, mdct_avx2};
#endif

#ifdef SHINE_KERNELS_NEON
static void filter_neon(const int32_t *x, int32_t s[18][32]) {
  filter_granule_x4(x, s);
}

static void mdct_neon(const int32_t previous[18][32],
                      const int32_t current[18][32], const int32_t window[36],
                      int32_t long_out[32][18], int32_t (*short_out)[192]) {
  mdct_bands_x4(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_neon = {filter_neon, mdct_neon};
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
