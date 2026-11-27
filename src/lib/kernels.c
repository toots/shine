#include "kernels.h"
#include "tables.h"
#include <stddef.h>

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

typedef struct {
  int32_t re, im;
} complex_t;

KERNEL_BODY int32_t round_shift(int64_t value, int shift) {
  return (int32_t)((value + ((int64_t)1 << (shift - 1))) >> shift);
}

KERNEL_BODY complex_t add(complex_t a, complex_t b) {
  complex_t sum = {a.re + b.re, a.im + b.im};
  return sum;
}

KERNEL_BODY complex_t sub(complex_t a, complex_t b) {
  complex_t difference = {a.re - b.re, a.im - b.im};
  return difference;
}

KERNEL_BODY complex_t times_minus_i(complex_t a) {
  complex_t product = {a.im, -a.re};
  return product;
}

/* (re + i im) (cos - i sin) / 2**shift, from the Q31 (cos, sin). */
KERNEL_BODY complex_t rotate(int32_t re, int32_t im, const int32_t turn[2],
                             int shift) {
  complex_t rotated = {
      round_shift((int64_t)re * turn[0] + (int64_t)im * turn[1], shift),
      round_shift((int64_t)im * turn[0] - (int64_t)re * turn[1], shift)};
  return rotated;
}

KERNEL_BODY void dft3(complex_t *x0, complex_t *x1, complex_t *x2) {
  complex_t sum = add(*x1, *x2), difference = sub(*x1, *x2);
  complex_t middle = {x0->re - round_shift(sum.re, 1),
                      x0->im - round_shift(sum.im, 1)};
  complex_t side = {round_shift((int64_t)difference.re * SIN_PI_3, 31),
                    round_shift((int64_t)difference.im * SIN_PI_3, 31)};

  *x0 = add(*x0, sum);
  x1->re = middle.re + side.im;
  x1->im = middle.im - side.re;
  x2->re = middle.re - side.im;
  x2->im = middle.im + side.re;
}

/* Three DFTs of size 3, twiddles, then three more. */
KERNEL_BODY void dft9(const complex_t in[9], complex_t out[9]) {
  complex_t x[9];

  for (int n = 0; n < 9; n++)
    x[n] = in[n];
  for (int n = 0; n < 3; n++)
    dft3(&x[n], &x[n + 3], &x[n + 6]);
  x[4] = rotate(x[4].re, x[4].im, shine_dft9_twiddle[0], 31);
  x[5] = rotate(x[5].re, x[5].im, shine_dft9_twiddle[1], 31);
  x[7] = rotate(x[7].re, x[7].im, shine_dft9_twiddle[1], 31);
  x[8] = rotate(x[8].re, x[8].im, shine_dft9_twiddle[2], 31);
  for (int k = 0; k < 3; k++) {
    dft3(&x[3 * k], &x[3 * k + 1], &x[3 * k + 2]);
    for (int j = 0; j < 3; j++)
      out[k + 3 * j] = x[3 * k + j];
  }
}

KERNEL_BODY void dft4(const complex_t *in, int stride, complex_t out[4]) {
  complex_t even0 = add(in[0], in[2 * stride]);
  complex_t even1 = sub(in[0], in[2 * stride]);
  complex_t odd0 = add(in[stride], in[3 * stride]);
  complex_t odd1 = times_minus_i(sub(in[stride], in[3 * stride]));

  out[0] = add(even0, odd0);
  out[1] = add(even1, odd1);
  out[2] = sub(even0, odd0);
  out[3] = sub(even1, odd1);
}

KERNEL_BODY void dft8(const complex_t in[8], complex_t out[8]) {
  complex_t even[4], odd[4];

  dft4(in, 2, even);
  dft4(in + 1, 2, odd);
  odd[1] = rotate(odd[1].re, odd[1].im, eighth_turn, 31);
  odd[2] = times_minus_i(odd[2]);
  odd[3] = rotate(odd[3].re, odd[3].im, three_eighths_turn, 31);
  for (int k = 0; k < 4; k++) {
    out[k] = add(even[k], odd[k]);
    out[k + 4] = sub(even[k], odd[k]);
  }
}

KERNEL_BODY void dft(int size, const complex_t *in, complex_t *out) {
  if (size == 3) {
    for (int n = 0; n < 3; n++)
      out[n] = in[n];
    dft3(&out[0], &out[1], &out[2]);
  } else if (size == 4)
    dft4(in, 1, out);
  else if (size == 8)
    dft8(in, out);
  else
    dft9(in, out);
}

/* The DCT-IV of size n of in[0], in[stride], ..., through a DFT of size n/2
 * between two twiddles, with gains 2**(31 - pre_shift) and
 * 2**(31 - post_shift). Every value of the DFT is a sum of its inputs times
 * factors of modulus at most 1, so their sum of moduli bounds it. */
KERNEL_BODY void dct4(const int32_t *in, int stride, int n,
                      const int32_t (*twiddle)[2], int pre_shift,
                      int post_shift, int32_t *out) {
  complex_t folded[9], spectrum[9];

  for (int j = 0; j < n / 2; j++)
    folded[j] = rotate(in[stride * 2 * j], in[stride * (n - 1 - 2 * j)],
                       twiddle[j], pre_shift);
  dft(n / 2, folded, spectrum);
  for (int k = 0; k < n / 2; k++) {
    complex_t z = spectrum[k];

    out[2 * k] = round_shift((int64_t)z.re * twiddle[k][0] +
                                 (int64_t)z.im * twiddle[k][1],
                             post_shift);
    out[n - 1 - 2 * k] = round_shift((int64_t)z.re * twiddle[k][1] -
                                         (int64_t)z.im * twiddle[k][0],
                                     post_shift);
  }
}

/* out[k] = sum of in[stride * m] * table[k][m] / 2**32, rounded once. */
KERNEL_BODY void cosine_product(const int32_t *in, int stride, int n,
                                const int32_t *table, int32_t *out) {
  for (int k = 0; k < n; k++) {
    int64_t sum = 0;

    for (int m = 0; m < n; m++)
      sum += (int64_t)in[stride * m] * table[n * k + m];
    out[k] = round_shift(sum, 32);
  }
}

/* The DCT-III of size 2n from the DCT-III of its even inputs and the DCT-IV
 * of its odd ones. */
KERNEL_BODY void dct3_merge(const int32_t *even, const int32_t *odd, int n,
                            int32_t *out) {
  for (int i = 0; i < n; i++) {
    out[i] = even[i] + odd[i];
    out[2 * n - 1 - i] = even[i] - odd[i];
  }
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
KERNEL_BODY void filter_fold(const int64_t y[64], int32_t a[32]) {
  a[0] = round_shift(y[16], 32);
  for (int n = 1; n <= 16; n++)
    a[n] = round_shift(y[16 + n] + y[16 - n], 32);
  for (int n = 17; n < 32; n++)
    a[n] = round_shift(y[16 + n] - y[80 - n], 32);
}

/* s = DCT-III(a) / 2, split into DCT-IVs down to size 2. For full scale
 * input, the window bounds |a| by 0.043 * 2**31, so each value is below
 * 16 * 0.043 * 2**31. */
KERNEL_BODY void filter_dct3_32(const int32_t a[32], int32_t s[32]) {
  int32_t even2[2], odd2[2], even4[4], odd4[4], even8[8], odd8[8];
  int32_t even16[16], odd16[16];

  cosine_product(a, 16, 2, &shine_dct3_2[0][0], even2);
  cosine_product(a + 8, 16, 2, &shine_dct4_2[0][0], odd2);
  dct3_merge(even2, odd2, 2, even4);
  cosine_product(a + 4, 8, 4, &shine_dct4_4[0][0], odd4);
  dct3_merge(even4, odd4, 4, even8);
  dct4(a + 2, 4, 8, shine_dct4_twiddle_8, 31, 32, odd8);
  dct3_merge(even8, odd8, 8, even16);
  dct4(a + 1, 2, 16, shine_dct4_twiddle_16, 31, 32, odd16);
  dct3_merge(even16, odd16, 16, s);
}

KERNEL_BODY void filter_body(const int32_t x[512], int off, int32_t s[32]) {
  int64_t y[64];
  int32_t a[32];

  filter_window(x, off, y);
  filter_fold(y, a);
  filter_dct3_32(a, s);
}

/* The windowed MDCT input of size n folded to the DCT-IV input of size n/2,
 * halved. */
KERNEL_BODY void mdct_fold(const int32_t *in, const int32_t *window, int n,
                           int32_t *u) {
  const int quarter = n / 4, mirror = 3 * n / 4 - 1;

  for (int k = 0; k < quarter; k++)
    u[k] = round_shift(-(int64_t)in[mirror - k] * window[mirror - k] -
                           (int64_t)in[mirror + 1 + k] * window[mirror + 1 + k],
                       32);
  for (int k = quarter; k < n / 2; k++)
    u[k] = round_shift((int64_t)in[k - quarter] * window[k - quarter] -
                           (int64_t)in[mirror - k] * window[mirror - k],
                       32);
}

/* The outputs are scaled by 1/4 (MDCT_HEADROOM_STEPS). The long DFT runs at
 * the scale of its output, the short one at 4 times. For subband samples,
 * below 0.424 * 2**31, the moduli of a DFT's inputs, which bound its values,
 * then sum to less than 0.77 * 2**31 with any window. */
KERNEL_BODY void mdct_body(const int32_t in[36], const int32_t window[36],
                           int32_t long_out[18], int32_t short_out[3][6]) {
  int32_t u[18];

  mdct_fold(in, window, 36, u);
  dct4(u, 1, 18, shine_dct4_twiddle_18, 33, 31, long_out);
  if (!short_out)
    return;
  /* The three short windows cover samples 6..17, 12..23 and 18..29. */
  for (int w = 0; w < 3; w++) {
    mdct_fold(in + 6 + 6 * w, shine_mdct_window_short, 12, u);
    dct4(u, 1, 6, shine_dct4_twiddle_6, 31, 33, short_out[w]);
  }
}

static void filter_plain(const int32_t x[512], int off, int32_t s[32]) {
  filter_body(x, off, s);
}

static void mdct_plain(const int32_t in[36], const int32_t window[36],
                       int32_t long_out[18], int32_t short_out[3][6]) {
  mdct_body(in, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_plain = {filter_plain, mdct_plain};

#ifdef SHINE_KERNELS_AVX2
__attribute__((target("avx2"))) static void
filter_avx2(const int32_t x[512], int off, int32_t s[32]) {
  filter_body(x, off, s);
}

__attribute__((target("avx2"))) static void mdct_avx2(const int32_t in[36],
                                                      const int32_t window[36],
                                                      int32_t long_out[18],
                                                      int32_t short_out[3][6]) {
  mdct_body(in, window, long_out, short_out);
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
