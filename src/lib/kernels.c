#include "kernels.h"
#include "tables.h"
#include <string.h>
#ifdef SHINE_KERNELS_AVX2
#include <immintrin.h>
#endif

/* Each kernel body is inlined into one wrapper per instruction set, which
 * the compiler then vectorizes for that set. The bodies on the lane type of
 * an instruction set extension are built for it (KERNEL_TARGET): they pass
 * its vectors by value. */
#define KERNEL_TARGET
#if defined(__GNUC__) || defined(__clang__)
#define KERNEL_BODY static inline __attribute__((always_inline)) KERNEL_TARGET
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

#ifdef SHINE_KERNELS_AVX2
#define TARGET_AVX2 __attribute__((target("avx2")))

typedef int32_t lanes4_t __attribute__((vector_size(16)));
typedef int64_t wide4_t __attribute__((vector_size(32)));

/* vpmuldq: GCC multiplies widened vectors as full 64-bit values. */
KERNEL_BODY TARGET_AVX2 wide4_t multiply_avx2(lanes4_t x, int32_t f) {
  return (wide4_t)_mm256_mul_epi32(_mm256_cvtepi32_epi64((__m128i)x),
                                   _mm256_set1_epi64x(f));
}

/* AVX2 has no 64-bit arithmetic shift: the low 32 bits of x >> shift are
 * those of a logical shift for shift < 32, the high ones of an arithmetic
 * 32-bit shift otherwise. */
KERNEL_BODY TARGET_AVX2 lanes4_t shift_narrow_avx2(wide4_t x, int shift) {
  const __m256i low_halves = _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6);
  const __m256i high_halves = _mm256_setr_epi32(1, 3, 5, 7, 1, 3, 5, 7);
  __m256i shifted;

  if (shift < 32)
    shifted = _mm256_permutevar8x32_epi32(
        _mm256_srl_epi64((__m256i)x, _mm_cvtsi32_si128(shift)), low_halves);
  else
    shifted = _mm256_permutevar8x32_epi32(
        _mm256_sra_epi32((__m256i)x, _mm_cvtsi32_si128(shift - 32)),
        high_halves);
  return (lanes4_t)_mm256_castsi256_si128(shifted);
}

KERNEL_BODY TARGET_AVX2 void transpose_avx2(lanes4_t v[4]) {
  __m128i low01 = _mm_unpacklo_epi32((__m128i)v[0], (__m128i)v[1]);
  __m128i low23 = _mm_unpacklo_epi32((__m128i)v[2], (__m128i)v[3]);
  __m128i high01 = _mm_unpackhi_epi32((__m128i)v[0], (__m128i)v[1]);
  __m128i high23 = _mm_unpackhi_epi32((__m128i)v[2], (__m128i)v[3]);

  v[0] = (lanes4_t)_mm_unpacklo_epi64(low01, low23);
  v[1] = (lanes4_t)_mm_unpackhi_epi64(low01, low23);
  v[2] = (lanes4_t)_mm_unpacklo_epi64(high01, high23);
  v[3] = (lanes4_t)_mm_unpackhi_epi64(high01, high23);
}

/* The windows of a run of 8 taps, as vpmuldq multiplies them: the even taps
 * in even[j], the odd ones in odd[j], each in the low half of a 64-bit
 * lane, negated when `negate`. */
typedef struct {
  __m256i even[8], odd[8];
} fold_window_avx2_t;

KERNEL_BODY TARGET_AVX2 void fold_window_avx2(const int32_t *window, int negate,
                                              fold_window_avx2_t *taps) {
  for (int j = 0; j < 8; j++) {
    __m256i run = _mm256_loadu_si256((const __m256i *)&window[64 * j]);

    if (negate)
      run = _mm256_sub_epi32(_mm256_setzero_si256(), run);
    taps->even[j] = run;
    taps->odd[j] = _mm256_srli_epi64(run, 32);
  }
}

/* The windowed sums y[i] of a filter step for 8 consecutive i, from the
 * samples x at the first of them: those of the even i in *even, of the odd
 * ones in *odd. */
KERNEL_BODY TARGET_AVX2 void fold_sums_avx2(const int32_t *x,
                                            const fold_window_avx2_t *taps,
                                            __m256i *even, __m256i *odd) {
  __m256i even_sums = _mm256_setzero_si256(), odd_sums = even_sums;

  for (int j = 0; j < 8; j++) {
    __m256i run = _mm256_loadu_si256((const __m256i *)&x[64 * j]);

    even_sums =
        _mm256_add_epi64(even_sums, _mm256_mul_epi32(run, taps->even[j]));
    odd_sums = _mm256_add_epi64(
        odd_sums, _mm256_mul_epi32(_mm256_srli_epi64(run, 32), taps->odd[j]));
  }
  *even = even_sums;
  *odd = odd_sums;
}

/* (ahead + behind) / 2**32, rounded, in the high half of each 64-bit lane,
 * from the run behind in reverse lane order. */
KERNEL_BODY TARGET_AVX2 __m256i fold_pair_avx2(__m256i ahead, __m256i behind) {
  const __m256i half = _mm256_set1_epi64x((int64_t)1 << 31);

  return _mm256_add_epi64(
      _mm256_add_epi64(ahead, _mm256_permute4x64_epi64(behind, 0x1b)), half);
}

/* filter_fold_run on 8 taps. Lane l of the run behind pairs with tap 7 - l:
 * its odd taps meet the even ones ahead. */
KERNEL_BODY TARGET_AVX2 void fold_run_avx2(const int32_t *x, int first,
                                           int32_t folded[18][FOLDED_WIDTH]) {
  const int ahead = 16 + first, behind = (9 - first) & 63;
  fold_window_avx2_t ahead_taps, behind_taps;

  fold_window_avx2(&shine_enwindow[ahead], 0, &ahead_taps);
  fold_window_avx2(&shine_enwindow[behind], first > 16, &behind_taps);
  for (int step = 0; step < 18; step++) {
    const int32_t *in = &x[32 * (17 - step)];
    __m256i ahead_even, ahead_odd, behind_even, behind_odd, even, odd;

    fold_sums_avx2(&in[ahead], &ahead_taps, &ahead_even, &ahead_odd);
    fold_sums_avx2(&in[behind], &behind_taps, &behind_even, &behind_odd);
    even = fold_pair_avx2(ahead_even, behind_odd);
    odd = fold_pair_avx2(ahead_odd, behind_even);
    _mm256_storeu_si256(
        (__m256i *)&folded[step][first],
        _mm256_blend_epi32(_mm256_srli_epi64(even, 32), odd, 0xaa));
  }
}

KERNEL_BODY TARGET_AVX2 void fold_avx2(const int32_t *x,
                                       int32_t folded[18][FOLDED_WIDTH]) {
  for (int first = 1; first <= 32; first += 8)
    fold_run_avx2(x, first, folded);
}

#define LANES 4
#define LANE_T lanes4_t
#define WIDE_T wide4_t
#define WIDEN(x) __builtin_convertvector(x, wide4_t)
#define NARROW(x) __builtin_convertvector(x, lanes4_t)
#define TRANSPOSE(v) transpose_avx2(v)
#define FILTER_FOLD(x, folded) fold_avx2(x, folded)
#define MULTIPLY(x, f) multiply_avx2(x, f)
#define SHIFT_NARROW(x, shift) shift_narrow_avx2(x, shift)
#define LANE(v, l) (v)[l]
#define FN(name) name##_avx2
#undef KERNEL_TARGET
#define KERNEL_TARGET TARGET_AVX2
#include "kernels_lanes.h"
#undef KERNEL_TARGET
#define KERNEL_TARGET
#endif

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
TARGET_AVX2 static void filter_avx2(const int32_t *x, int32_t s[18][32]) {
  filter_granule_avx2(x, s);
}

TARGET_AVX2 static void mdct_avx2(const int32_t previous[18][32],
                                  const int32_t current[18][32],
                                  const int32_t window[36],
                                  int32_t long_out[32][18],
                                  int32_t (*short_out)[192]) {
  mdct_bands_avx2(previous, current, window, long_out, short_out);
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
