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

/* Kernel sets to compare, the first one checked against double precision:
 * every available one, then the chosen one. */
#define MAX_KERNEL_SETS 8
static const shine_kernels_t *kernel_sets[MAX_KERNEL_SETS];
static const char *kernel_names[MAX_KERNEL_SETS];
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

static void filter_exact(const int32_t x[512], double s[32]) {
  for (int band = 0; band < 32; band++) {
    s[band] = 0;
    for (int tap = 0; tap < 512; tap++)
      s[band] += x[tap] * filter_coefficient(band, tap);
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

/* The window of filter step `step` in the input of a granule. */
static int32_t *step_window(int32_t x[SHINE_FILTER_INPUT], int step) {
  return &x[32 * (17 - step)];
}

static void check_filter(const int32_t x[SHINE_FILTER_INPUT]) {
  static int32_t s[MAX_KERNEL_SETS][18][32];

  for (int set = 0; set < kernel_count; set++) {
    kernel_sets[set]->filter(x, s[set]);
    if (set) {
      identity_checks++;
      identity_failures += memcmp(s[set], s[0], sizeof(s[0])) != 0;
    }
  }
  for (int step = 0; step < 18; step++) {
    double exact[32];

    filter_exact(step_window((int32_t *)x, step), exact);
    record(FILTER, s[0][step], exact, 32);
  }
}

/* MDCT inputs of one window type waiting for a granule of 32 subbands to be
 * full. */
typedef struct {
  int32_t previous[18][32], current[18][32];
  int count;
} mdct_queue_t;

static mdct_queue_t mdct_queues[3];

static void flush_mdct(int type_index) {
  static int32_t long_out[MAX_KERNEL_SETS][32][18],
      short_out[MAX_KERNEL_SETS][3][192], long_only[32][18];
  mdct_queue_t *queue = &mdct_queues[type_index];
  const int32_t *window = shine_mdct_window[long_types[type_index]];

  for (int set = 0; set < kernel_count; set++) {
    kernel_sets[set]->mdct(queue->previous, queue->current, window,
                           long_out[set], short_out[set]);
    kernel_sets[set]->mdct(queue->previous, queue->current, window, long_only,
                           NULL);
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
  for (int band = 0; band < queue->count; band++) {
    int32_t in[36], short_lines[3][6];
    double long_exact[18], short_exact[3][6];

    for (int k = 0; k < 18; k++) {
      in[k] = queue->previous[k][band];
      in[k + 18] = queue->current[k][band];
    }
    for (int w = 0; w < 3; w++)
      memcpy(short_lines[w], &short_out[0][w][6 * band],
             sizeof(short_lines[w]));
    mdct_exact(in, long_types[type_index], long_exact, short_exact);
    record(LONG_NORM + type_index, long_out[0][band], long_exact, 18);
    record(SHORT, &short_lines[0][0], &short_exact[0][0], 18);
  }
  memset(queue, 0, sizeof(*queue));
}

static void check_mdct(const int32_t in[36], int type_index) {
  mdct_queue_t *queue = &mdct_queues[type_index];

  for (int k = 0; k < 18; k++) {
    queue->previous[k][queue->count] = in[k];
    queue->current[k][queue->count] = in[k + 18];
  }
  if (++queue->count == 32)
    flush_mdct(type_index);
}

static void flush_checks(void) {
  for (int type_index = 0; type_index < 3; type_index++)
    flush_mdct(type_index);
}

/* One filter step of the first kernel set: the last of a granule. */
static void filter_step(const int32_t window[512], int32_t s[32]) {
  int32_t x[SHINE_FILTER_INPUT] = {0}, steps[18][32];

  memcpy(step_window(x, 17), window, 512 * sizeof(x[0]));
  kernel_sets[0]->filter(x, steps);
  memcpy(s, steps[17], sizeof(steps[17]));
}

/* Runs the filter over a stream of samples as l3subband.c does, giving the
 * MDCT input of `band` after the warm-up: the 36 subband samples, odd ones
 * of odd bands inverted as l3mdct.c does. */
static void subband_input(const int16_t stream[STREAM_SAMPLES], int band,
                          int32_t in[36]) {
  int32_t x[512] = {0}, s[32];

  for (int step = 0; step < STREAM_STEPS; step++) {
    memmove(&x[32], x, 480 * sizeof(x[0]));
    for (int i = 32; i--;)
      x[i] = stream[32 * step + 31 - i] * 65536;
    filter_step(x, s);
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
  for (int round = 0; round < 120; round++) {
    int32_t x[SHINE_FILTER_INPUT];

    for (int i = 0; i < SHINE_FILTER_INPUT; i++)
      x[i] = random_sample();
    check_filter(x);
  }
}

static void check_filter_full_scale(void) {
  for (int round = 0; round < 8; round++) {
    int32_t x[SHINE_FILTER_INPUT];

    for (int i = 0; i < SHINE_FILTER_INPUT; i++)
      x[i] = full_scale(round < 2 ? round : (int)(next_random() >> 31));
    check_filter(x);
  }
}

/* The window of one step at +-full scale with the sign of the coefficient,
 * for every band, the samples outside it at full scale of either sign. */
static void check_filter_maximising(void) {
  for (int band = 0; band < 32; band++)
    for (int negative = 0; negative < 2; negative++) {
      int32_t x[SHINE_FILTER_INPUT];
      int32_t *window = step_window(x, (band + negative) % 18);

      for (int i = 0; i < SHINE_FILTER_INPUT; i++)
        x[i] = full_scale(next_random() >> 31);
      for (int tap = 0; tap < 512; tap++)
        window[tap] =
            full_scale((filter_coefficient(band, tap) < 0) != negative);
      check_filter(x);
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

/* With --list, prints the name of each set the processor runs, one per
 * line, then those it does not run, each after "missing ". */
static int list_sets(void) {
  for (int missing = 0; missing < 2; missing++)
    for (int i = 0; shine_kernels_name(i); i++)
      if ((shine_kernels_named(shine_kernels_name(i)) == NULL) == missing)
        printf("%s%s\n", missing ? "missing " : "", shine_kernels_name(i));
  return 0;
}

int main(int argc, char **argv) {
  shine_kernels_t selected;

  if (argc > 1 && !strcmp(argv[1], "--list"))
    return list_sets();
  add_kernel_set("plain", &shine_kernels_plain);
  for (int i = 0; shine_kernels_name(i); i++) {
    const char *name = shine_kernels_name(i);

    if (!strcmp(name, "plain"))
      continue;
    if (shine_kernels_named(name))
      add_kernel_set(name, shine_kernels_named(name));
    else
      printf("%s not run: not available\n", name);
  }
  if (shine_kernels_select(&selected) < 0)
    return 1;
  add_kernel_set("chosen", &selected);

  check_filter_random();
  check_filter_full_scale();
  check_filter_maximising();
  check_mdct_box();
  check_mdct_box_maximising();
  check_mdct_subbands_random();
  check_mdct_subbands_maximising();
  flush_checks();
  return report();
}
