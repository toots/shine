/* resample.c: doc/psy/resampling.md §2 to §4. */

#include "resample.h"
#include "layer3.h"
#include "tables.h"

#include <stdlib.h>
#include <string.h>

/* §3: coefficients in Q15. The absolute sum of each half of a phase stays
 * below 2, so that its sum of products with 16-bit samples fits 32 bits. */
#define COEFFICIENT_BITS 15
/* Most input samples per call. */
#define MAX_INPUT (2 * GRANULE_SIZE)
/* The lowest rate is a sixth of the highest. */
#define MAX_TAPS (2 * 6 * RESAMPLE_ZEROS + 2)

struct shine_resampler {
  int channels;
  int up, down;          /* output and input rates, reduced */
  int taps;              /* per phase */
  int step;              /* down / up */
  int remainder;         /* down % up */
  int phase;             /* of the next output sample, 0 to up - 1 */
  int fill;              /* samples held per channel */
  int16_t *coefficients; /* [up][taps] */
  int16_t *history[2];   /* [taps + step + MAX_INPUT] */
};

/* §1: RESAMPLE_BAND_FRACTION = 0.92, RESAMPLE_MIN_RATIO = 0.8. */
int shine_resample_rate(int samplerate, int bitrate, int cutoff) {
  int best = samplerate;

  if (!cutoff)
    return samplerate;
  for (int i = 0; i < 9; i++) {
    int rate = shine_samplerates[i];

    /* RESAMPLE_MIN_RATE: MPEG-2.5 rates only when the input is one. */
    if (rate < best && rate >= 16000 && 5 * rate <= 4 * samplerate &&
        50LL * cutoff <= 23LL * rate && shine_check_config(rate, bitrate) >= 0)
      best = rate;
  }
  return best;
}

static int greatest_common_divisor(int a, int b) {
  while (b) {
    int rest = a % b;
    a = b;
    b = rest;
  }
  return a;
}

/* §2: the prototype at `numerator / denominator` points from its centre, in
 * Q30, by cubic interpolation of the table. */
static int64_t prototype(int64_t numerator, int denominator) {
  int64_t point = numerator / denominator;
  int64_t t = ((numerator % denominator) << 15) / denominator;
  const int32_t *y;
  int64_t c1, c2, c3;

  if (point >= RESAMPLE_ZEROS * RESAMPLE_POINTS)
    return 0;
  /* y[-1] to y[2] are the table at point - 1 to point + 2. */
  y = shine_resample_prototype + point + 1;
  c1 = 6LL * y[1] - 2LL * y[-1] - 3LL * y[0] - y[2];
  c2 = 3LL * (y[-1] + (int64_t)y[1]) - 6LL * y[0];
  c3 = (int64_t)y[2] - y[-1] + 3LL * ((int64_t)y[0] - y[1]);
  return y[0] + (t * (c1 + t * (c2 + t * c3 / 32768) / 32768) / 32768) / 6;
}

/* §3: the coefficients of one phase, summing to exactly 1. Returns whether
 * each half has the headroom of a 32-bit sum. */
static int phase_coefficients(const shine_resampler_t *resampler, int phase,
                              int16_t *coefficients) {
  const int first =
      -(int)((int64_t)RESAMPLE_ZEROS * resampler->down / resampler->up);
  int64_t raw[MAX_TAPS], sum = 0;
  int total = 0, largest = 0;

  for (int j = 0; j < resampler->taps; j++) {
    int64_t offset = (int64_t)(first + j) * resampler->up - phase;

    raw[j] = prototype((offset < 0 ? -offset : offset) * RESAMPLE_POINTS,
                       resampler->down);
    sum += raw[j];
  }
  for (int j = 0; j < resampler->taps; j++) {
    int64_t scaled = raw[j] * (2 << COEFFICIENT_BITS);

    /* Rounded to nearest, halves away from zero. */
    scaled = (scaled + (scaled < 0 ? -sum : sum)) / (2 * sum);
    coefficients[j] = (int16_t)scaled;
    total += (int)scaled;
    if (raw[j] > raw[largest])
      largest = j;
  }
  coefficients[largest] += (1 << COEFFICIENT_BITS) - total;

  for (int half = 0; half < 2; half++) {
    int absolute = 0;

    for (int j = 0; j < resampler->taps / 2; j++)
      absolute += abs(coefficients[half * resampler->taps / 2 + j]);
    if (absolute >= 2 << COEFFICIENT_BITS)
      return 0;
  }
  return 1;
}

shine_resampler_t *shine_resampler_open(int input_rate, int output_rate,
                                        int channels) {
  const int divisor = greatest_common_divisor(input_rate, output_rate);
  shine_resampler_t *resampler = calloc(1, sizeof(*resampler));
  int lead, before, capacity;

  if (!resampler)
    return NULL;
  resampler->channels = channels;
  resampler->up = output_rate / divisor;
  resampler->down = input_rate / divisor;
  resampler->step = resampler->down / resampler->up;
  resampler->remainder = resampler->down % resampler->up;
  lead = RESAMPLE_ZEROS * resampler->down / resampler->up;
  resampler->taps = 2 * lead + 2;
  capacity = resampler->taps + resampler->step + 1 + MAX_INPUT;

  resampler->coefficients =
      malloc((size_t)resampler->up * resampler->taps * sizeof(int16_t));
  for (int ch = 0; ch < channels; ch++)
    resampler->history[ch] = calloc(capacity, sizeof(int16_t));
  if (!resampler->coefficients || !resampler->history[0] ||
      !resampler->history[channels - 1]) {
    shine_resampler_close(resampler);
    return NULL;
  }
  for (int phase = 0; phase < resampler->up; phase++)
    if (!phase_coefficients(resampler, phase,
                            resampler->coefficients +
                                phase * resampler->taps)) {
      shine_resampler_close(resampler);
      return NULL;
    }

  /* §4: output sample k is centred RESAMPLE_ZEROS output periods before
   * input sample k * down / up. The history starts with the silence that
   * precedes the input. */
  before =
      (RESAMPLE_ZEROS * resampler->down + resampler->up - 1) / resampler->up;
  resampler->phase = before * resampler->up - RESAMPLE_ZEROS * resampler->down;
  resampler->fill = before + lead;
  return resampler;
}

void shine_resampler_close(shine_resampler_t *resampler) {
  if (!resampler)
    return;
  free(resampler->coefficients);
  free(resampler->history[0]);
  free(resampler->history[1]);
  free(resampler);
}

int shine_resample_bound(const shine_resampler_t *resampler, int count) {
  return (int)(((int64_t)count * resampler->up + resampler->down - 1) /
               resampler->down) +
         1;
}

static inline int16_t filter(const int16_t *samples,
                             const int16_t *coefficients, int taps) {
  const int half = taps / 2;
  int32_t first = 0, second = 0;
  int64_t sum;

  for (int j = 0; j < half; j++)
    first += samples[j] * coefficients[j];
  for (int j = 0; j < half; j++)
    second += samples[half + j] * coefficients[half + j];
  sum = ((int64_t)first + second + (1 << (COEFFICIENT_BITS - 1))) >>
        COEFFICIENT_BITS;
  return (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
}

int shine_resample(shine_resampler_t *resampler, const int16_t *const *input,
                   int stride, int count, int16_t *const *output) {
  const int taps = resampler->taps;
  const int fill = resampler->fill + count;
  int position = 0, phase = resampler->phase, written = 0;

  for (int ch = 0; ch < resampler->channels; ch++) {
    int16_t *history = resampler->history[ch];

    for (int i = 0; i < count; i++)
      history[resampler->fill + i] = input[ch][i * stride];

    position = 0;
    phase = resampler->phase;
    written = 0;
    while (position + taps <= fill) {
      output[ch][written++] = filter(
          history + position, resampler->coefficients + phase * taps, taps);
      position += resampler->step;
      phase += resampler->remainder;
      if (phase >= resampler->up) {
        phase -= resampler->up;
        position++;
      }
    }
    memmove(history, history + position, (fill - position) * sizeof(*history));
  }
  resampler->fill = fill - position;
  resampler->phase = phase;
  return written;
}
