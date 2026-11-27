/* The AVX2 set: the bodies built on vectors of 4 lanes, whose
 * 32x32->64-bit products are vpmuldq, the instruction the baseline
 * instruction set lacks. Its functions are built for AVX2 one by one, so
 * that the rest of the library runs on any x86-64. */
#include "kernels_body.h"

#if defined(__x86_64__) && (defined(__clang__) || __GNUC__ >= 9)
#include <immintrin.h>

#define TARGET_AVX2 __attribute__((target("avx2")))

typedef int32_t lanes4_t __attribute__((vector_size(16)));
typedef int64_t wide4_t __attribute__((vector_size(32)));

/* vpmuldq: GCC multiplies widened vectors as full 64-bit values. */
KERNEL_INLINE TARGET_AVX2 wide4_t multiply_avx2(lanes4_t x, int32_t f) {
  return (wide4_t)_mm256_mul_epi32(_mm256_cvtepi32_epi64((__m128i)x),
                                   _mm256_set1_epi64x(f));
}

/* AVX2 has no 64-bit arithmetic shift: the low 32 bits of x >> shift are
 * those of a logical shift for shift < 32, the high ones of an arithmetic
 * 32-bit shift otherwise. */
KERNEL_INLINE TARGET_AVX2 lanes4_t shift_narrow_avx2(wide4_t x, int shift) {
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

KERNEL_INLINE TARGET_AVX2 void transpose_avx2(lanes4_t v[4]) {
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

KERNEL_INLINE TARGET_AVX2 void
fold_window_avx2(const int32_t *window, int negate, fold_window_avx2_t *taps) {
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
KERNEL_INLINE TARGET_AVX2 void fold_sums_avx2(const int32_t *x,
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
KERNEL_INLINE TARGET_AVX2 __m256i fold_pair_avx2(__m256i ahead,
                                                 __m256i behind) {
  const __m256i half = _mm256_set1_epi64x((int64_t)1 << 31);

  return _mm256_add_epi64(
      _mm256_add_epi64(ahead, _mm256_permute4x64_epi64(behind, 0x1b)), half);
}

/* filter_fold_run on 8 taps. Lane l of the run behind pairs with tap 7 - l:
 * its odd taps meet the even ones ahead. */
KERNEL_INLINE TARGET_AVX2 void fold_run_avx2(const int32_t *x, int first,
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

KERNEL_INLINE TARGET_AVX2 void fold_avx2(const int32_t *x,
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

typedef uint32_t lines8_t __attribute__((vector_size(32)));
typedef uint16_t narrow8_t __attribute__((vector_size(16)));

/* 1 in each lane where the comparison c holds, else 0. */
#define COUNT(c) (-(lines8_t)(c))
/* a in each lane where c holds, else b. */
#define SELECT(c, a, b) (((a) & (lines8_t)(c)) | ((b) & ~(lines8_t)(c)))

/* The first `count` of 8 32-bit values from `in`, the others 0: a run
 * shorter than 8 lines goes through the same operations, its missing lanes
 * left out of the results. */
KERNEL_BODY lines8_t load_run_avx2(const void *in, int count) {
  lines8_t run = {0};

  memcpy(&run, in, (size_t)count * sizeof(uint32_t));
  return run;
}

KERNEL_BODY void ix_max_nonzero_run_avx2(const int *ix, int count,
                                         lines8_t *max, lines8_t *nonzero) {
  const lines8_t run = load_run_avx2(ix, count);

  *max = SELECT(run > *max, run, *max);
  *nonzero += COUNT(run != 0);
}

TARGET_AVX2 static unsigned ix_max_nonzero_avx2(const int *ix, int n,
                                                int *nonzero) {
  lines8_t max = {0}, nonzero_lanes = {0};
  uint32_t largest = 0, count = 0;
  int i = 0;

  for (; i + 8 <= n; i += 8)
    ix_max_nonzero_run_avx2(&ix[i], 8, &max, &nonzero_lanes);
  if (i < n)
    ix_max_nonzero_run_avx2(&ix[i], n - i, &max, &nonzero_lanes);
  for (int l = 0; l < 8; l++) {
    largest = max[l] > largest ? max[l] : largest;
    count += nonzero_lanes[l];
  }
  *nonzero = (int)count;
  return largest;
}

/* The 8 int16_t from in, widened. */
KERNEL_BODY __m256i load_samples_avx2(const int16_t *in) {
  return _mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)in));
}

/* h = in * 65536 + high * 256 + low = (in * 256 + high) * 256 + low, where
 * in * 256 + high is below 2^25: 64-bit lanes only for the last sum. */
KERNEL_BODY void attack_filter_run_avx2(const int16_t *in,
                                        const __m256i high_taps[5],
                                        const __m256i low_taps[5],
                                        __m256i h[2]) {
  __m256i high = _mm256_setzero_si256(), low = high, scaled;

  for (int j = 0; j < 5; j++) {
    const __m256i pair = _mm256_add_epi32(load_samples_avx2(&in[-9 + 2 * j]),
                                          load_samples_avx2(&in[9 - 2 * j]));

    high = _mm256_add_epi32(high, _mm256_mullo_epi32(pair, high_taps[j]));
    low = _mm256_add_epi32(low, _mm256_mullo_epi32(pair, low_taps[j]));
  }
  scaled = _mm256_add_epi32(_mm256_slli_epi32(load_samples_avx2(in), 8), high);
  h[0] = _mm256_add_epi64(
      _mm256_slli_epi64(_mm256_cvtepi32_epi64(_mm256_castsi256_si128(scaled)),
                        8),
      _mm256_cvtepi32_epi64(_mm256_castsi256_si128(low)));
  h[1] = _mm256_add_epi64(
      _mm256_slli_epi64(
          _mm256_cvtepi32_epi64(_mm256_extracti128_si256(scaled, 1)), 8),
      _mm256_cvtepi32_epi64(_mm256_extracti128_si256(low, 1)));
}

KERNEL_BODY __m256i max_epi64_avx2(__m256i a, __m256i b) {
  return _mm256_blendv_epi8(b, a, _mm256_cmpgt_epi64(a, b));
}

KERNEL_BODY int64_t largest_lane_avx2(__m256i v) {
  int64_t lanes[4], largest;

  _mm256_storeu_si256((__m256i *)lanes, v);
  largest = lanes[0];
  for (int l = 1; l < 4; l++)
    largest = lanes[l] > largest ? lanes[l] : largest;
  return largest;
}

/* The largest magnitude of a run is the largest of its highest value and
 * the opposite of its lowest. */
TARGET_AVX2 static void attack_peaks_avx2(const int16_t *in,
                                          const int32_t taps[5], int64_t h[576],
                                          int64_t peaks[9]) {
  __m256i high_taps[5], low_taps[5];

  for (int j = 0; j < 5; j++) {
    high_taps[j] = _mm256_set1_epi32(taps[j] / 256);
    low_taps[j] = _mm256_set1_epi32(taps[j] % 256);
  }
  for (int run = 0; run < 9; run++) {
    __m256i highest = _mm256_setzero_si256(), negated_lowest = highest;

    for (int n = 64 * run; n < 64 * run + 64; n += 8) {
      __m256i out[2];

      attack_filter_run_avx2(&in[n], high_taps, low_taps, out);
      for (int half = 0; half < 2; half++) {
        _mm256_storeu_si256((__m256i *)&h[n + 4 * half], out[half]);
        highest = max_epi64_avx2(highest, out[half]);
        negated_lowest =
            max_epi64_avx2(negated_lowest,
                           _mm256_sub_epi64(_mm256_setzero_si256(), out[half]));
      }
    }
    peaks[run] = largest_lane_avx2(max_epi64_avx2(highest, negated_lowest));
  }
}

static const shine_kernels_t kernels_avx2 = {
    .filter = filter_avx2,
    .mdct = mdct_avx2,
    .ix_max_nonzero = ix_max_nonzero_avx2,
    .attack_peaks = attack_peaks_avx2};

const shine_kernels_t *shine_kernels_avx2(void) {
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx2") ? &kernels_avx2 : NULL;
}
#else
const shine_kernels_t *shine_kernels_avx2(void) { return NULL; }
#endif
