/* The filter bank kernels (src/lib/kernels.h) against the transforms they
 * compute, evaluated in double precision: on random input, on full scale
 * input of both signs, on the input that maximises each output, and the
 * MDCTs on the subband samples the filter makes. Every kernel set built for
 * this processor must also give the same output as the others, bit for
 * bit. */
#include "kernels.h"
#include "tables.h"
#include "types.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Largest error allowed, in units of the last bit of the 32-bit outputs. */
#define FILTER_ERROR_BOUND 6.0
#define MDCT_ERROR_BOUND 8.0

#define FULL_SCALE (32767 * 65536)
#define NEGATIVE_FULL_SCALE INT32_MIN

/* Stream samples feeding the 36 subband samples of one MDCT, and the filter
 * steps before them that fill the window buffer. */
#define WARMUP_STEPS 15
#define STREAM_STEPS (WARMUP_STEPS + 36)
#define STREAM_SAMPLES (32 * STREAM_STEPS)

typedef struct {
  const char *name;
  double max, squares;
  long count;
  double bound;
} error_stats_t;

enum { FILTER, LONG_NORM, LONG_START, LONG_STOP, SHORT, ERROR_KINDS };

static error_stats_t errors[ERROR_KINDS] = {
    {"filter", 0, 0, 0, FILTER_ERROR_BOUND},
    {"long MDCT, normal window", 0, 0, 0, MDCT_ERROR_BOUND},
    {"long MDCT, start window", 0, 0, 0, MDCT_ERROR_BOUND},
    {"long MDCT, stop window", 0, 0, 0, MDCT_ERROR_BOUND},
    {"short MDCT", 0, 0, 0, MDCT_ERROR_BOUND}};

static const int long_types[3] = {BLOCK_NORM, BLOCK_START, BLOCK_STOP};

/* Kernel sets to compare, the first one checked against double precision. */
static const shine_kernels_t *kernel_sets[3];
static const char *kernel_names[3];
static int kernel_count;
static long identity_checks, identity_failures;

static uint32_t seed = 1;

static uint32_t next_random(void) {
  seed = seed * 1103515245u + 12345u;
  return seed;
}

static int32_t random_sample(void) {
  return (int32_t)((int64_t)(int16_t)(next_random() >> 16) * 65536);
}

static int32_t full_scale(int negative) {
  return negative ? NEGATIVE_FULL_SCALE : FULL_SCALE;
}

static void record(int kind, const int32_t *out, const double *exact, int n) {
  for (int i = 0; i < n; i++) {
    double error = fabs(out[i] - exact[i]);

    if (error > errors[kind].max)
      errors[kind].max = error;
    errors[kind].squares += error * error;
    errors[kind].count++;
  }
}

/* The matrixing coefficient of window tap j for band i, with the window's
 * scale: y = x * shine_enwindow / 2**32, s = y . cos / 2. */
static double filter_coefficient(int band, int tap) {
  return shine_enwindow[tap] / 4294967296.0 *
         cos((2 * band + 1) * (16 - tap % 64) * M_PI / 64) / 2;
}

static void filter_exact(const int32_t x[512], int off, double s[32]) {
  for (int band = 0; band < 32; band++) {
    s[band] = 0;
    for (int tap = 0; tap < 512; tap++)
      s[band] += x[(off + tap) & 511] * filter_coefficient(band, tap);
  }
}

/* ISO 11172-3 2.4.3.4.10.3, sample i of 36. */
static double block_window(int type, int i) {
  if (type == BLOCK_START)
    return i < 18   ? sin(M_PI / 36 * (i + 0.5))
           : i < 24 ? 1
           : i < 30 ? sin(M_PI / 12 * (i - 18 + 0.5))
                    : 0;
  if (type == BLOCK_STOP)
    return i < 6    ? 0
           : i < 12 ? sin(M_PI / 12 * (i - 6 + 0.5))
           : i < 18 ? 1
                    : sin(M_PI / 36 * (i + 0.5));
  return sin(M_PI / 36 * (i + 0.5));
}

/* With the gain of shine_mdct_window: 1/9, and 1/12 for short blocks. */
static double long_coefficient(int type, int k, int j) {
  return block_window(type, j) * cos(M_PI / 72 * (2 * j + 19) * (2 * k + 1)) /
         9;
}

static double short_coefficient(int k, int j) {
  return sin(M_PI / 12 * (j + 0.5)) *
         cos(M_PI / 24 * (2 * j + 7) * (2 * k + 1)) / 12;
}

static void mdct_exact(const int32_t in[36], int type, double long_out[18],
                       double short_out[3][6]) {
  for (int k = 0; k < 18; k++) {
    long_out[k] = 0;
    for (int j = 0; j < 36; j++)
      long_out[k] += in[j] * long_coefficient(type, k, j);
  }
  for (int w = 0; w < 3; w++)
    for (int k = 0; k < 6; k++) {
      short_out[w][k] = 0;
      for (int j = 0; j < 12; j++)
        short_out[w][k] += in[6 + 6 * w + j] * short_coefficient(k, j);
    }
}

static void check_filter(const int32_t x[512], int off) {
  int32_t s[3][32];
  double exact[32];

  for (int set = 0; set < kernel_count; set++) {
    kernel_sets[set]->filter(x, off, s[set]);
    if (set) {
      identity_checks++;
      identity_failures += memcmp(s[set], s[0], sizeof(s[0])) != 0;
    }
  }
  filter_exact(x, off, exact);
  record(FILTER, s[0], exact, 32);
}

static void check_mdct(const int32_t in[36], int type_index) {
  const int type = long_types[type_index];
  int32_t long_out[3][18], short_out[3][3][6], long_only[18];
  double long_exact[18], short_exact[3][6];

  for (int set = 0; set < kernel_count; set++) {
    kernel_sets[set]->mdct(in, shine_mdct_window[type], long_out[set],
                           short_out[set]);
    kernel_sets[set]->mdct(in, shine_mdct_window[type], long_only, NULL);
    identity_checks++;
    identity_failures +=
        memcmp(long_only, long_out[set], sizeof(long_only)) != 0;
    if (set) {
      identity_checks++;
      identity_failures +=
          memcmp(long_out[set], long_out[0], sizeof(long_out[0])) != 0 ||
          memcmp(short_out[set], short_out[0], sizeof(short_out[0])) != 0;
    }
  }
  mdct_exact(in, type, long_exact, short_exact);
  record(LONG_NORM + type_index, long_out[0], long_exact, 18);
  record(SHORT, &short_out[0][0][0], &short_exact[0][0], 18);
}

/* Runs the filter over a stream of samples as l3subband.c does, giving the
 * MDCT input of `band` after the warm-up: the 36 subband samples, odd ones
 * of odd bands inverted as l3mdct.c does. */
static void subband_input(const int16_t stream[STREAM_SAMPLES], int band,
                          int32_t in[36]) {
  int32_t x[512] = {0}, s[32];
  int off = 0;

  for (int step = 0; step < STREAM_STEPS; step++) {
    for (int i = 32; i--;)
      x[i + off] = stream[32 * step + 31 - i] * 65536;
    kernel_sets[0]->filter(x, off, s);
    off = (off + 480) & 511;
    if (step >= WARMUP_STEPS) {
      int j = step - WARMUP_STEPS;

      in[j] = band % 2 && j % 2 ? -s[band] : s[band];
    }
  }
}

/* The gain of stream sample q on sum over j of weights[j] in[j] for the
 * subband_input of `band`: at step t the filter takes sample 32 t + 31 - tap
 * at `tap`. */
static double stream_gain(const double weights[36], int band, int q) {
  double gain = 0;

  for (int j = 0; j < 36; j++) {
    int tap = 32 * (j + WARMUP_STEPS) + 31 - q;

    if (tap >= 0 && tap < 512)
      gain += weights[j] * (band % 2 && j % 2 ? -1 : 1) *
              filter_coefficient(band, tap);
  }
  return gain;
}

static void stream_maximising(const double weights[36], int band, int negative,
                              int16_t stream[STREAM_SAMPLES]) {
  for (int q = 0; q < STREAM_SAMPLES; q++)
    stream[q] =
        (stream_gain(weights, band, q) >= 0) != negative ? 32767 : -32768;
}

static void check_filter_random(void) {
  for (int round = 0; round < 2000; round++) {
    int32_t x[512];

    for (int i = 0; i < 512; i++)
      x[i] = random_sample();
    check_filter(x, 32 * (round % 16));
  }
}

static void check_filter_full_scale(void) {
  for (int round = 0; round < 64; round++) {
    int32_t x[512];

    for (int i = 0; i < 512; i++)
      x[i] = full_scale(round < 2 ? round : next_random() >> 31);
    check_filter(x, 32 * (round % 16));
  }
}

/* x[tap] = +-full scale with the sign of the coefficient, for every band. */
static void check_filter_maximising(void) {
  for (int band = 0; band < 32; band++)
    for (int negative = 0; negative < 2; negative++) {
      int32_t x[512];
      int off = 32 * ((band + negative) % 16);

      for (int tap = 0; tap < 512; tap++)
        x[(off + tap) & 511] =
            full_scale((filter_coefficient(band, tap) < 0) != negative);
      check_filter(x, off);
    }
}

/* The largest subband sample the filter can make. */
static int32_t subband_bound(void) {
  double bound = 0;

  for (int band = 0; band < 32; band++) {
    double sum = 0;

    for (int tap = 0; tap < 512; tap++)
      sum += fabs(filter_coefficient(band, tap)) * 2147483648.0;
    if (sum > bound)
      bound = sum;
  }
  return (int32_t)bound;
}

/* Inputs anywhere in the range of subband samples, and at its ends. */
static void check_mdct_box(void) {
  const int32_t bound = subband_bound();

  for (int round = 0; round < 3000; round++) {
    int32_t in[36];

    for (int j = 0; j < 36; j++) {
      uint32_t r = next_random();

      in[j] = round % 3 ? (int32_t)((int64_t)bound * (int32_t)r / INT32_MAX)
              : r >> 31 ? -bound
                        : bound;
    }
    check_mdct(in, round % 3);
  }
}

/* in[j] = +-bound with the sign of each coefficient. */
static void check_mdct_box_maximising(void) {
  const int32_t bound = subband_bound();

  for (int t = 0; t < 3; t++)
    for (int k = 0; k < 18; k++)
      for (int negative = 0; negative < 2; negative++) {
        int32_t in[36];

        for (int j = 0; j < 36; j++)
          in[j] = (long_coefficient(long_types[t], k, j) < 0) != negative
                      ? -bound
                      : bound;
        check_mdct(in, t);
      }
  for (int w = 0; w < 3; w++)
    for (int k = 0; k < 6; k++)
      for (int negative = 0; negative < 2; negative++) {
        int32_t in[36] = {0};

        for (int j = 0; j < 12; j++)
          in[6 + 6 * w + j] =
              (short_coefficient(k, j) < 0) != negative ? -bound : bound;
        check_mdct(in, w);
      }
}

static void check_mdct_subbands_random(void) {
  for (int round = 0; round < 200; round++) {
    int16_t stream[STREAM_SAMPLES];
    int32_t in[36];

    for (int q = 0; q < STREAM_SAMPLES; q++)
      stream[q] = (int16_t)(next_random() >> 16);
    subband_input(stream, round % 32, in);
    check_mdct(in, round % 3);
  }
}

/* The stream that maximises each coefficient of each band through the
 * filter. */
static void check_mdct_subbands_maximising(void) {
  for (int band = 0; band < 32; band++)
    for (int negative = 0; negative < 2; negative++) {
      for (int t = 0; t < 3; t++)
        for (int k = 0; k < 18; k++) {
          double weights[36];
          int16_t stream[STREAM_SAMPLES];
          int32_t in[36];

          for (int j = 0; j < 36; j++)
            weights[j] = long_coefficient(long_types[t], k, j);
          stream_maximising(weights, band, negative, stream);
          subband_input(stream, band, in);
          check_mdct(in, t);
        }
      for (int w = 0; w < 3; w++)
        for (int k = 0; k < 6; k++) {
          double weights[36] = {0};
          int16_t stream[STREAM_SAMPLES];
          int32_t in[36];

          for (int j = 0; j < 12; j++)
            weights[6 + 6 * w + j] = short_coefficient(k, j);
          stream_maximising(weights, band, negative, stream);
          subband_input(stream, band, in);
          check_mdct(in, w);
        }
    }
}

static void add_kernel_set(const char *name, const shine_kernels_t *kernels) {
  kernel_names[kernel_count] = name;
  kernel_sets[kernel_count++] = kernels;
}

static int report(void) {
  int failed = 0;
  long checked = 0;

  for (int kind = 0; kind < ERROR_KINDS; kind++) {
    const error_stats_t *e = &errors[kind];
    int over = e->max > e->bound || e->count == 0;

    printf("%-26s %7ld outputs, max error %.2f, rms %.3f, bound %.1f%s\n",
           e->name, e->count, e->max,
           e->count ? sqrt(e->squares / e->count) : 0, e->bound,
           over ? ": FAIL" : "");
    failed |= over;
    checked += e->count;
  }
  printf("kernel sets:");
  for (int set = 0; set < kernel_count; set++)
    printf(" %s", kernel_names[set]);
  printf("; %ld identity checks, %ld failed\n", identity_checks,
         identity_failures);
  return failed || identity_failures || checked == 0 || identity_checks == 0;
}

int main(void) {
  shine_kernels_t selected;

  add_kernel_set("plain", &shine_kernels_plain);
#ifdef SHINE_KERNELS_AVX2
  __builtin_cpu_init();
  if (__builtin_cpu_supports("avx2"))
    add_kernel_set("avx2", &shine_kernels_avx2);
  else
    printf("avx2 not run: the processor does not have it\n");
#endif
  shine_kernels_select(&selected);
  add_kernel_set("chosen", &selected);

  check_filter_random();
  check_filter_full_scale();
  check_filter_maximising();
  check_mdct_box();
  check_mdct_box_maximising();
  check_mdct_subbands_random();
  check_mdct_subbands_maximising();
  return report();
}
