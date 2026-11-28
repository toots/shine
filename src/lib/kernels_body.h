/* What every kernel file shares: the scalar parts of the kernels, written
 * once and built for the target of the file or function that inlines them.
 * The bodies on the lane type of an instruction set extension are built
 * for it (KERNEL_TARGET), as they pass its vectors by value. */
#ifndef KERNELS_BODY_H
#define KERNELS_BODY_H

#include "kernels.h"
#include "tables.h"
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define KERNEL_INLINE static inline __attribute__((always_inline))
#else
#define KERNEL_INLINE static inline
#endif
#define KERNEL_TARGET
#define KERNEL_BODY KERNEL_INLINE KERNEL_TARGET

/* 2**31 sin(pi / 3) */
#define SIN_PI_3 1859775393

/* cos and sin of pi / 4 and 3 pi / 4, in Q31. */
static const int32_t eighth_turn[2] = {1518500250, 1518500250};
static const int32_t three_eighths_turn[2] = {-1518500250, 1518500250};

/* a[0..32] of each step, on the vector lanes. */
#define FOLDED_WIDTH 33

/* value / 2**32, rounded. */
KERNEL_INLINE int32_t round_half(int64_t value) {
  return (int32_t)((value + ((int64_t)1 << 31)) >> 32);
}

/* 64-bit windowed sums. */
KERNEL_INLINE void filter_window(const int32_t x[512], int64_t y[64]) {
  for (int i = 0; i < 64; i++)
    y[i] = 0;
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 64; i++)
      y[i] += (int64_t)x[64 * j + i] * shine_enwindow[64 * j + i];
}

/* The matrixing cos((2i + 1) (16 - j) pi / 64) is even about j = 16 and odd
 * about j = 48: a[n] collects the terms in cos((2i + 1) n pi / 64). */
KERNEL_INLINE void filter_fold(const int64_t y[64], int32_t a[32]) {
  a[0] = round_half(y[16]);
  for (int n = 1; n <= 16; n++)
    a[n] = round_half(y[16 + n] + y[16 - n]);
  for (int n = 17; n < 32; n++)
    a[n] = round_half(y[16 + n] - y[80 - n]);
}

/* The references of the kernels a set may leave NULL, and their calls
 * through a set. */
KERNEL_INLINE unsigned ix_max_nonzero(const int *ix, int n, int *nonzero) {
  unsigned max = 0;
  int count = 0;

  for (int i = 0; i < n; i++) {
    max = (unsigned)ix[i] > max ? (unsigned)ix[i] : max;
    count += ix[i] != 0;
  }
  *nonzero = count;
  return max;
}

/* Each tap splits into a multiple of 256 and a remainder, so that the
 * products of either part add up in 32 bits, which vectorises. */
KERNEL_INLINE void attack_filter(const int16_t *in, const int32_t taps[5],
                                 int64_t h[576]) {
  for (int i = 0; i < 576; i++) {
    int32_t high = 0, low = 0;

    for (int j = 0; j < 5; j++) {
      const int32_t pair = in[i - 9 + 2 * j] + in[i + 9 - 2 * j];

      high += taps[j] / 256 * pair;
      low += taps[j] % 256 * pair;
    }
    h[i] = (int64_t)in[i] * 65536 + (int64_t)high * 256 + low;
  }
}

KERNEL_INLINE void run_peaks(const int64_t h[576], int64_t peaks[9]) {
  for (int run = 0; run < 9; run++) {
    int64_t peak = 0;

    for (int n = 64 * run; n < 64 * run + 64; n++) {
      const int64_t magnitude = h[n] < 0 ? -h[n] : h[n];

      peak = magnitude > peak ? magnitude : peak;
    }
    peaks[run] = peak;
  }
}

KERNEL_INLINE void attack_peaks(const int16_t *in, const int32_t taps[5],
                                int64_t h[576], int64_t peaks[9]) {
  attack_filter(in, taps, h);
  run_peaks(h, peaks);
}

KERNEL_INLINE void line_energies(const uint32_t *magnitude, int lines,
                                 int shift, int shift_t, int rescale,
                                 uint64_t *energy, uint64_t *energy_t) {
  for (int k = 0; k < lines; k++) {
    uint64_t square = (uint64_t)magnitude[k] * magnitude[k];

    energy_t[k] = (square >> shift_t) + (energy[k] >> rescale);
    energy[k] = square >> shift;
  }
}

KERNEL_INLINE unsigned call_ix_max_nonzero(const shine_kernels_t *kernels,
                                           const int *ix, int n, int *nonzero) {
  return kernels->ix_max_nonzero ? kernels->ix_max_nonzero(ix, n, nonzero)
                                 : ix_max_nonzero(ix, n, nonzero);
}

KERNEL_INLINE void call_attack_peaks(const shine_kernels_t *kernels,
                                     const int16_t *in, const int32_t taps[5],
                                     int64_t h[576], int64_t peaks[9]) {
  if (kernels->attack_peaks)
    kernels->attack_peaks(in, taps, h, peaks);
  else
    attack_peaks(in, taps, h, peaks);
}

KERNEL_INLINE void call_line_energies(const shine_kernels_t *kernels,
                                      const uint32_t *magnitude, int lines,
                                      int shift, int shift_t, int rescale,
                                      uint64_t *energy, uint64_t *energy_t) {
  if (kernels->line_energies)
    kernels->line_energies(magnitude, lines, shift, shift_t, rescale, energy,
                           energy_t);
  else
    line_energies(magnitude, lines, shift, shift_t, rescale, energy, energy_t);
}

#endif
