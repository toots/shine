/* The transforms of kernels.c on LANES independent inputs at once, one per
 * lane of LANE_T. kernels.c includes this once per lane type, with:
 *   LANES       inputs transformed together
 *   LANE_T      one 32-bit value per lane, WIDE_T one 64-bit value per lane
 *   WIDEN(x)    LANE_T to WIDE_T, NARROW(x) the low 32 bits of WIDE_T
 *   LANE(v, l)  lane l of the LANE_T v
 *   FN(name)    the name of a function or type for this lane type
 * Every lane goes through the same integer operations in the same order
 * whatever the lane type, so its result does not depend on LANES. */

typedef struct {
  LANE_T re, im;
} FN(complex_t);

KERNEL_BODY LANE_T FN(load)(const int32_t *in) {
  LANE_T lanes = {0};

  for (int l = 0; l < LANES; l++)
    LANE(lanes, l) = in[l];
  return lanes;
}

/* Lane l of lines[k] to out[l * stride + k], for the first `count` lanes.
 * Runs of LANES values are stored at once: a load of the same run is then
 * served by one pending store. */
KERNEL_BODY void FN(store_lines)(const LANE_T *lines, int n, int32_t *out,
                                 int stride, int count) {
  int k = 0;

  for (; k + LANES <= n; k += LANES)
    for (int l = 0; l < count; l++) {
      LANE_T run = {0};

      for (int i = 0; i < LANES; i++)
        LANE(run, i) = LANE(lines[k + i], l);
      memcpy(&out[l * stride + k], &run, sizeof(run));
    }
  for (; k < n; k++)
    for (int l = 0; l < count; l++)
      out[l * stride + k] = LANE(lines[k], l);
}

KERNEL_BODY LANE_T FN(round_shift)(WIDE_T value, int shift) {
  return NARROW((value + ((int64_t)1 << (shift - 1))) >> shift);
}

KERNEL_BODY FN(complex_t) FN(add)(FN(complex_t) a, FN(complex_t) b) {
  FN(complex_t) sum = {a.re + b.re, a.im + b.im};
  return sum;
}

KERNEL_BODY FN(complex_t) FN(sub)(FN(complex_t) a, FN(complex_t) b) {
  FN(complex_t) difference = {a.re - b.re, a.im - b.im};
  return difference;
}

KERNEL_BODY FN(complex_t) FN(times_minus_i)(FN(complex_t) a) {
  FN(complex_t) product = {a.im, -a.re};
  return product;
}

/* (re + i im) (cos - i sin) / 2**shift, from the Q31 (cos, sin). */
KERNEL_BODY FN(complex_t)
    FN(rotate)(LANE_T re, LANE_T im, const int32_t turn[2], int shift) {
  FN(complex_t)
  rotated = {FN(round_shift)(WIDEN(re) * turn[0] + WIDEN(im) * turn[1], shift),
             FN(round_shift)(WIDEN(im) * turn[0] - WIDEN(re) * turn[1], shift)};
  return rotated;
}

KERNEL_BODY void FN(dft3)(FN(complex_t) * x0, FN(complex_t) * x1,
                          FN(complex_t) * x2) {
  FN(complex_t) sum = FN(add)(*x1, *x2), difference = FN(sub)(*x1, *x2);
  FN(complex_t)
  middle = {x0->re - FN(round_shift)(WIDEN(sum.re), 1),
            x0->im - FN(round_shift)(WIDEN(sum.im), 1)};
  FN(complex_t)
  side = {FN(round_shift)(WIDEN(difference.re) * SIN_PI_3, 31),
          FN(round_shift)(WIDEN(difference.im) * SIN_PI_3, 31)};

  *x0 = FN(add)(*x0, sum);
  x1->re = middle.re + side.im;
  x1->im = middle.im - side.re;
  x2->re = middle.re - side.im;
  x2->im = middle.im + side.re;
}

/* Three DFTs of size 3, twiddles, then three more. */
KERNEL_BODY void FN(dft9)(const FN(complex_t) in[9], FN(complex_t) out[9]) {
  FN(complex_t) x[9];

  for (int n = 0; n < 9; n++)
    x[n] = in[n];
  for (int n = 0; n < 3; n++)
    FN(dft3)(&x[n], &x[n + 3], &x[n + 6]);
  x[4] = FN(rotate)(x[4].re, x[4].im, shine_dft9_twiddle[0], 31);
  x[5] = FN(rotate)(x[5].re, x[5].im, shine_dft9_twiddle[1], 31);
  x[7] = FN(rotate)(x[7].re, x[7].im, shine_dft9_twiddle[1], 31);
  x[8] = FN(rotate)(x[8].re, x[8].im, shine_dft9_twiddle[2], 31);
  for (int k = 0; k < 3; k++) {
    FN(dft3)(&x[3 * k], &x[3 * k + 1], &x[3 * k + 2]);
    for (int j = 0; j < 3; j++)
      out[k + 3 * j] = x[3 * k + j];
  }
}

KERNEL_BODY void FN(dft4)(const FN(complex_t) * in, int stride,
                          FN(complex_t) out[4]) {
  FN(complex_t) even0 = FN(add)(in[0], in[2 * stride]);
  FN(complex_t) even1 = FN(sub)(in[0], in[2 * stride]);
  FN(complex_t) odd0 = FN(add)(in[stride], in[3 * stride]);
  FN(complex_t) odd1 = FN(times_minus_i)(FN(sub)(in[stride], in[3 * stride]));

  out[0] = FN(add)(even0, odd0);
  out[1] = FN(add)(even1, odd1);
  out[2] = FN(sub)(even0, odd0);
  out[3] = FN(sub)(even1, odd1);
}

KERNEL_BODY void FN(dft8)(const FN(complex_t) in[8], FN(complex_t) out[8]) {
  FN(complex_t) even[4], odd[4];

  FN(dft4)(in, 2, even);
  FN(dft4)(in + 1, 2, odd);
  odd[1] = FN(rotate)(odd[1].re, odd[1].im, eighth_turn, 31);
  odd[2] = FN(times_minus_i)(odd[2]);
  odd[3] = FN(rotate)(odd[3].re, odd[3].im, three_eighths_turn, 31);
  for (int k = 0; k < 4; k++) {
    out[k] = FN(add)(even[k], odd[k]);
    out[k + 4] = FN(sub)(even[k], odd[k]);
  }
}

KERNEL_BODY void FN(dft)(int size, const FN(complex_t) * in,
                         FN(complex_t) * out) {
  if (size == 3) {
    for (int n = 0; n < 3; n++)
      out[n] = in[n];
    FN(dft3)(&out[0], &out[1], &out[2]);
  } else if (size == 4)
    FN(dft4)(in, 1, out);
  else if (size == 8)
    FN(dft8)(in, out);
  else
    FN(dft9)(in, out);
}

/* The DCT-IV of size n of in[0], in[stride], ..., through a DFT of size n/2
 * between two twiddles, with gains 2**(31 - pre_shift) and
 * 2**(31 - post_shift). Every value of the DFT is a sum of its inputs times
 * factors of modulus at most 1, so their sum of moduli bounds it. */
KERNEL_BODY void FN(dct4)(const LANE_T *in, int stride, int n,
                          const int32_t (*twiddle)[2], int pre_shift,
                          int post_shift, LANE_T *out) {
  FN(complex_t) folded[9], spectrum[9];

  for (int j = 0; j < n / 2; j++)
    folded[j] = FN(rotate)(in[stride * 2 * j], in[stride * (n - 1 - 2 * j)],
                           twiddle[j], pre_shift);
  FN(dft)(n / 2, folded, spectrum);
  for (int k = 0; k < n / 2; k++) {
    FN(complex_t) z = spectrum[k];

    out[2 * k] = FN(round_shift)(
        WIDEN(z.re) * twiddle[k][0] + WIDEN(z.im) * twiddle[k][1], post_shift);
    out[n - 1 - 2 * k] = FN(round_shift)(
        WIDEN(z.re) * twiddle[k][1] - WIDEN(z.im) * twiddle[k][0], post_shift);
  }
}

/* out[k] = sum of in[stride * m] * table[k][m] / 2**32, rounded once. */
KERNEL_BODY void FN(cosine_product)(const LANE_T *in, int stride, int n,
                                    const int32_t *table, LANE_T *out) {
  for (int k = 0; k < n; k++) {
    WIDE_T sum = WIDEN(in[0]) * table[n * k];

    for (int m = 1; m < n; m++)
      sum += WIDEN(in[stride * m]) * table[n * k + m];
    out[k] = FN(round_shift)(sum, 32);
  }
}

/* The DCT-III of size 2n from the DCT-III of its even inputs and the DCT-IV
 * of its odd ones. */
KERNEL_BODY void FN(dct3_merge)(const LANE_T *even, const LANE_T *odd, int n,
                                LANE_T *out) {
  for (int i = 0; i < n; i++) {
    out[i] = even[i] + odd[i];
    out[2 * n - 1 - i] = even[i] - odd[i];
  }
}

/* s = DCT-III(a) / 2, split into DCT-IVs down to size 2. For full scale
 * input, the window bounds |a| by 0.043 * 2**31, so each value is below
 * 16 * 0.043 * 2**31. */
KERNEL_BODY void FN(filter_dct3_32)(const LANE_T a[32], LANE_T s[32]) {
  LANE_T even2[2], odd2[2], even4[4], odd4[4], even8[8], odd8[8];
  LANE_T even16[16], odd16[16];

  FN(cosine_product)(a, 16, 2, &shine_dct3_2[0][0], even2);
  FN(cosine_product)(a + 8, 16, 2, &shine_dct4_2[0][0], odd2);
  FN(dct3_merge)(even2, odd2, 2, even4);
  FN(cosine_product)(a + 4, 8, 4, &shine_dct4_4[0][0], odd4);
  FN(dct3_merge)(even4, odd4, 4, even8);
  FN(dct4)(a + 2, 4, 8, shine_dct4_twiddle_8, 31, 32, odd8);
  FN(dct3_merge)(even8, odd8, 8, even16);
  FN(dct4)(a + 1, 2, 16, shine_dct4_twiddle_16, 31, 32, odd16);
  FN(dct3_merge)(even16, odd16, 16, s);
}

/* The first `count` lanes are the filter steps from `step` on. */
KERNEL_BODY void
FN(filter_dct3_batch)(const int32_t folded[32][SHINE_FOLDED_STEPS], int step,
                      int count, int32_t s[18][32]) {
  LANE_T a[32], subbands[32];

  for (int n = 0; n < 32; n++)
    a[n] = FN(load)(&folded[n][step]);
  FN(filter_dct3_32)(a, subbands);
  FN(store_lines)(subbands, 32, s[step], 32, count);
}

KERNEL_BODY void
FN(filter_dct3_steps)(const int32_t folded[32][SHINE_FOLDED_STEPS],
                      int32_t s[18][32]) {
  const int full = 18 - 18 % LANES;

  for (int step = 0; step < full; step += LANES)
    FN(filter_dct3_batch)(folded, step, LANES, s);
  if (full < 18)
    FN(filter_dct3_batch)(folded, full, 18 - full, s);
}

/* The windowed MDCT input of size n folded to the DCT-IV input of size n/2,
 * halved. */
KERNEL_BODY void FN(mdct_fold)(const LANE_T *in, const int32_t *window, int n,
                               LANE_T *u) {
  const int quarter = n / 4, mirror = 3 * n / 4 - 1;

  for (int k = 0; k < quarter; k++)
    u[k] =
        FN(round_shift)(-WIDEN(in[mirror - k]) * window[mirror - k] -
                            WIDEN(in[mirror + 1 + k]) * window[mirror + 1 + k],
                        32);
  for (int k = quarter; k < n / 2; k++)
    u[k] = FN(round_shift)(WIDEN(in[k - quarter]) * window[k - quarter] -
                               WIDEN(in[mirror - k]) * window[mirror - k],
                           32);
}

/* The lanes are consecutive subbands. The outputs are scaled by 1/4
 * (MDCT_HEADROOM_STEPS). The long DFT runs at the scale of its output, the
 * short one at 4 times. For subband samples, below 0.424 * 2**31, the moduli
 * of a DFT's inputs, which bound its values, then sum to less than
 * 0.77 * 2**31 with any window. */
KERNEL_BODY void FN(mdct_bands)(const int32_t previous[18][32],
                                const int32_t current[18][32],
                                const int32_t window[36],
                                int32_t long_out[32][18],
                                int32_t (*short_out)[192]) {
  for (int band = 0; band < 32; band += LANES) {
    LANE_T in[36], u[18], lines[18];

    for (int k = 0; k < 18; k++) {
      in[k] = FN(load)(&previous[k][band]);
      in[k + 18] = FN(load)(&current[k][band]);
    }
    FN(mdct_fold)(in, window, 36, u);
    FN(dct4)(u, 1, 18, shine_dct4_twiddle_18, 33, 31, lines);
    FN(store_lines)(lines, 18, long_out[band], 18, LANES);
    if (!short_out)
      continue;
    /* The three short windows cover samples 6..17, 12..23 and 18..29. */
    for (int w = 0; w < 3; w++) {
      FN(mdct_fold)(in + 6 + 6 * w, shine_mdct_window_short, 12, u);
      FN(dct4)(u, 1, 6, shine_dct4_twiddle_6, 31, 33, lines);
      FN(store_lines)(lines, 6, &short_out[w][6 * band], 6, LANES);
    }
  }
}

#undef LANES
#undef LANE_T
#undef WIDE_T
#undef WIDEN
#undef NARROW
#undef LANE
#undef FN
