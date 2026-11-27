/* Generates src/lib/psy_tables.h, the constants of the psychoacoustic model
 * (doc/psy/), so that the library computes no floating point at run time.
 * A float oracle of a phase being built (doc/psy/tests.md §3) reads them
 * too.
 *
 * It runs the library's own filterbank to measure the MDCT gain, so it is
 * built with it:
 *
 * cc -Isrc/lib tools/generate_psy_tables.c src/lib/l3mdct.c \
 *   src/lib/l3subband.c src/lib/kernels.c src/lib/tables.c \
 *   src/lib/precomputed_tables.c \
 *   -lm -o generate_psy_tables
 * ./generate_psy_tables > src/lib/psy_tables.h
 * clang-format -i src/lib/psy_tables.h
 *
 * Logarithms are log2 in Q16 (PSY_LOG_ONE is 1.0), energies are in the
 * energy unit of doc/psy/conventions.md. */

#include "l3mdct.h"
#include "psy.h"
#include "tables.h"
#include "types.h"

#include <math.h>
#include <stdio.h>

#define LOG_ONE 65536.0
/* The spreading matrix, Q29: its entries reach 1.01. */
#define SPREAD_ONE 536870912.0
#define LONG_LINES 576
#define NPART_CAPACITY 128

#define DELBARK 0.34
#define MASKING_TILT -7.4
#define ATH_FS_SPL 96.0
#define ATH_ADAPT_RANGE 90.30873362
#define ATH_ADAPT_GAIN -4.51571501
#define LOUDNESS_FS_NOISE 3.0
#define MS_SIMILARITY 1.58
#define AVG_MASK_FACTOR 0.5
#define PE_RATIO_CAP_LOG10 10.0

static double bark(double f) {
  double F = (f > 0 ? f : 0) / 1000;
  return 13 * atan(0.76 * F) + 3.5 * atan((F / 7.5) * (F / 7.5));
}

/* dB SPL, curve parameter c. */
static double ath_curve(double f, double c) {
  double F = f / 1000;

  if (F < 0.1)
    F = 0.1;
  if (F > 24.0)
    F = 24.0;
  return 3.640 * pow(F, -0.8) - 6.800 * exp(-0.6 * (F - 3.4) * (F - 3.4)) +
         6.000 * exp(-0.15 * (F - 8.7) * (F - 8.7)) +
         (0.6 + 0.04 * c) * 0.001 * F * F * F * F;
}

static double s3(double d) {
  double t = d >= 0 ? 3 * d : 1.5 * d;
  double x = 0, y;

  if (t >= 0.5 && t <= 2.5)
    x = 8 * ((t - 0.5) * (t - 0.5) - 2 * (t - 0.5));
  t += 0.474;
  y = 15.811389 + 7.5 * t - 17.5 * sqrt(1 + t * t);
  if (y <= -60)
    return 0;
  return pow(10, (x + y) / 10) / 0.6609193;
}

static double stereo_demask(double f) {
  double m = bark(f);

  m = (m < 15.5 ? m : 15.5) / 15.5;
  return pow(10, 1.25 * (1 - cos(M_PI * m)) - 2.5);
}

static long fixed(double x, double one) { return lround(x * one); }

/* log2 in Q16. */
static long log_q16(double x) { return fixed(log2(x), LOG_ONE); }

/* dB as an energy factor, log2 Q16. */
static long db_q16(double db) { return fixed(db * log2(10) / 10, LOG_ONE); }

/* The MDCT calls the model's lowpass; the generator measures the
 * transform without one. */
void shine_psy_lowpass(const psy_model_t *model, int32_t *long_lines,
                       int32_t (*short_lines)[192]) {
  (void)model, (void)long_lines, (void)short_lines;
}

static void print_array(const char *type, const char *name, const long *values,
                        int count) {
  printf("static const %s %s[%d] = {", type, name, count);
  for (int i = 0; i < count; i++)
    printf("%s%ld", i ? ", " : "", values[i]);
  printf("};\n");
}

/* Mean sum of the squared MDCT coefficients of a granule, for a sine at
 * full scale, averaged over phase and over frequencies away from DC and
 * Nyquist. */
/* Mean energy of a full-scale sine in the squared coefficients of a granule,
 * or of one short window when `short_blocks`. */
static double mdct_sine_energy(int short_blocks) {
  static shine_global_config config;
  static psy_model_t model;
  static int16_t samples[GRANULE_SIZE];
  const int warmup = 4, granules = 40, tones = 64;
  double total = 0;

  for (int tone = 0; tone < tones; tone++) {
    /* Not on a line centre, so the phase drifts across granules. */
    double frequency = (0.01 + 0.97 * (tone + 0.37) / tones) * M_PI;
    long n = 0;

    memset(&config, 0, sizeof(config));
    config.wave.channels = 1;
    config.mpeg.granules_per_frame = 1;
    config.psy = &model;
    shine_kernels_select(&config.kernels);
    config.side_info.gr[0].ch[0].tt.block_type =
        short_blocks ? BLOCK_SHORT : BLOCK_NORM;
    for (int g = 0; g < warmup + granules; g++) {
      double energy = 0;

      for (int i = 0; i < GRANULE_SIZE; i++, n++)
        samples[i] = (int16_t)lround(32767 * sin(frequency * n));
      config.buffer[0] = samples;
      shine_mdct_sub(&config, 1);
      for (int k = 0; k < GRANULE_SIZE; k++)
        energy += (double)config.mdct_freq[0][0][k] * config.mdct_freq[0][0][k];
      if (g >= warmup)
        total += energy;
    }
  }
  /* Full scale is 32768. */
  return total / (tones * granules) * (32768.0 / 32767) * (32768.0 / 32767) /
         (short_blocks ? 3 : 1);
}

typedef struct {
  int npart;
  int start[NPART_CAPACITY + 1];
  double bval[NPART_CAPACITY], width[NPART_CAPACITY];
} layout_t;

static void partition(layout_t *layout, int rate, int lines) {
  const int *sfb = lines == LONG_LINES
                       ? shine_scale_fact_band_index[rate]
                       : shine_scale_fact_band_index_short[rate];
  double delta = shine_samplerates[rate] / (2.0 * lines);
  int i = 0, k = 0, edge = 1;

  while (k < lines) {
    int k2 = k + 1;

    layout->start[i] = k;
    while (sfb[edge] <= k)
      edge++;
    while (k2 < lines && k2 != sfb[edge] &&
           bark(k2 * delta) - bark(k * delta) < DELBARK)
      k2++;
    layout->bval[i] = (bark((k + 0.5) * delta) + bark((k2 - 0.5) * delta)) / 2;
    layout->width[i] = bark(k2 * delta) - bark(k * delta);
    i++;
    k = k2;
  }
  layout->start[i] = lines;
  layout->npart = i;
}

static layout_t layouts[9], short_layouts[9];

/* The tables of a layout: `tag` is "" for the long one and "_s" for the
 * short one. */
static void partitions(layout_t *set, int lines, const char *tag,
                       const char *macro) {
  int npart_max = 0;

  for (int r = 0; r < 9; r++) {
    partition(&set[r], r, lines);
    if (set[r].npart > npart_max)
      npart_max = set[r].npart;
  }

  printf("/* %s partitions, per samplerate index "
         "(doc/psy/spectral-analysis.md §3). */\n",
         lines == LONG_LINES ? "Long" : "Short");
  printf("#define %s %d\n", macro, npart_max);
  printf("static const int16_t psy_npart%s[9] = {", tag);
  for (int r = 0; r < 9; r++)
    printf("%s%d", r ? ", " : "", set[r].npart);
  printf("};\n");

  printf("/* First line of each partition, then %d. */\n", lines);
  printf("static const int16_t psy_part_start%s[9][%s + 1] = {\n", tag, macro);
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int i = 0; i <= set[r].npart; i++)
      printf("%s%d", i ? ", " : "", set[r].start[i]);
    printf("},\n");
  }
  printf("};\n");

  printf("/* bval in Bark, Q16. */\n");
  printf("static const int32_t psy_part_bval%s[9][%s] = {\n", tag, macro);
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int i = 0; i < set[r].npart; i++)
      printf("%s%ld", i ? ", " : "", fixed(set[r].bval[i], LOG_ONE));
    printf("},\n");
  }
  printf("};\n");

  printf("/* log2 mld. */\n");
  printf("static const int32_t psy_part_mld%s[9][%s] = {\n", tag, macro);
  for (int r = 0; r < 9; r++) {
    double delta = shine_samplerates[r] / (2.0 * lines);

    printf("{");
    for (int i = 0; i < set[r].npart; i++) {
      int s = set[r].start[i], w = set[r].start[i + 1] - s;
      printf("%s%ld", i ? ", " : "",
             log_q16(stereo_demask((s + w / 2.0) * delta)));
    }
    printf("},\n");
  }
  printf("};\n");

  printf("/* log2 masking_lower, the fixed tilt (doc/psy/masking.md §3.4). "
         "*/\n");
  printf("static const int32_t psy_part_masking_lower%s[9][%s] = {\n", tag,
         macro);
  for (int r = 0; r < 9; r++) {
    int npart = set[r].npart;

    printf("{");
    for (int b = 0; b < npart; b++)
      printf("%s%ld", b ? ", " : "",
             db_q16(MASKING_TILT * (npart - b) / npart));
    printf("},\n");
  }
  printf("};\n\n");
}

/* Rows of the long spreading matrix, packed: row i holds S[i][lo..hi]. */
/* §4: the SNR offset of short partitions, as a factor. */
static double short_norm(double bval) {
  double snr = bval < 13 ? -8.25 : -8.25 + (bval - 13) * (8.25 - 4.5) / 11;

  return pow(10, snr / 10);
}

static void spreading(const layout_t *layouts, int is_short, const char *tag,
                      const char *macro, const char *size_macro) {
  static long values[9][NPART_CAPACITY * NPART_CAPACITY];
  static int lo[9][NPART_CAPACITY], hi[9][NPART_CAPACITY],
      offset[9][NPART_CAPACITY];
  int size_max = 0;

  for (int r = 0; r < 9; r++) {
    const layout_t *layout = &layouts[r];
    int size = 0;

    for (int i = 0; i < layout->npart; i++) {
      lo[r][i] = -1;
      for (int j = 0; j < layout->npart; j++)
        if (s3(layout->bval[i] - layout->bval[j]) > 0) {
          if (lo[r][i] < 0)
            lo[r][i] = j;
          hi[r][i] = j;
        }
      offset[r][i] = size;
      /* The positive range is contiguous: s3 is unimodal. */
      for (int j = lo[r][i]; j <= hi[r][i]; j++)
        values[r][size++] =
            fixed(s3(layout->bval[i] - layout->bval[j]) * layout->width[i] *
                      (is_short ? short_norm(layout->bval[i]) : 1),
                  SPREAD_ONE);
    }
    if (size > size_max)
      size_max = size;
  }

  printf("/* %s spreading rows (doc/psy/spectral-analysis.md §4): row i "
         "holds\n * S[i][j] in Q29 for j = lo..hi at psy_spread%s[offset]. "
         "*/\n",
         is_short ? "Short" : "Long", tag);
  printf("#define %s %d\n", size_macro, size_max);
  printf("static const uint8_t psy_spread_lo%s[9][%s] = {\n", tag, macro);
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int i = 0; i < layouts[r].npart; i++)
      printf("%s%d", i ? ", " : "", lo[r][i]);
    printf("},\n");
  }
  printf("};\n");
  printf("static const uint8_t psy_spread_hi%s[9][%s] = {\n", tag, macro);
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int i = 0; i < layouts[r].npart; i++)
      printf("%s%d", i ? ", " : "", hi[r][i]);
    printf("},\n");
  }
  printf("};\n");
  printf("static const int16_t psy_spread_offset%s[9][%s] = {\n", tag, macro);
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int i = 0; i < layouts[r].npart; i++)
      printf("%s%d", i ? ", " : "", offset[r][i]);
    printf("},\n");
  }
  printf("};\n");
  printf("static const uint32_t psy_spread%s[9][%s] = {\n", tag, size_macro);
  for (int r = 0; r < 9; r++) {
    int size = offset[r][layouts[r].npart - 1] + hi[r][layouts[r].npart - 1] -
               lo[r][layouts[r].npart - 1] + 1;
    printf("{");
    for (int i = 0; i < size; i++)
      printf("%s%ld", i ? ", " : "", values[r][i]);
    printf("},\n");
  }
  printf("};\n\n");
}

/* ATH(f_k) at curve parameter 0, and its F^4 term per unit of the parameter,
 * at every long line centre: the library adds the per-bitrate curve and
 * offset at initialisation (doc/psy/ath.md §1). */
static void ath_lines(void) {
  printf("/* log2 of 10^((ATH(f_k) - ATH_FS_SPL)/10) at curve parameter 0, "
         "per long\n * line (doc/psy/ath.md §1). */\n");
  printf("static const int32_t psy_ath_line[9][576] = {\n");
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int k = 0; k < LONG_LINES; k++) {
      double f = (k + 0.5) * shine_samplerates[r] / 1152.0;
      printf("%s%ld", k ? ", " : "", db_q16(ath_curve(f, 0) - ATH_FS_SPL));
    }
    printf("},\n");
  }
  printf("};\n");
  printf("/* log2 of the F^4 term per unit of the curve parameter, Q16. */\n");
  printf("static const int32_t psy_ath_line_curve[9][576] = {\n");
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int k = 0; k < LONG_LINES; k++) {
      double f = (k + 0.5) * shine_samplerates[r] / 1152.0;
      printf("%s%ld", k ? ", " : "", db_q16(ath_curve(f, 1) - ath_curve(f, 0)));
    }
    printf("},\n");
  }
  printf("};\n");
  printf("/* The same two tables at the centres of the 192 short lines. */\n");
  for (int curve = 0; curve < 2; curve++) {
    printf("static const int32_t psy_ath_line%s_s[9][192] = {\n",
           curve ? "_curve" : "");
    for (int r = 0; r < 9; r++) {
      printf("{");
      for (int k = 0; k < 192; k++) {
        double f = (k + 0.5) * shine_samplerates[r] / 384.0;
        printf("%s%ld", k ? ", " : "",
               db_q16(curve ? ath_curve(f, 1) - ath_curve(f, 0)
                            : ath_curve(f, 0) - ATH_FS_SPL));
      }
      printf("},\n");
    }
    printf("};\n");
  }
  printf("\n");
}

/* The per-bitrate set (doc/psy/parameters.md §1). */
static const struct {
  int kbps;
  double msfix, mask_adjust, ath_lower, ath_curve;
} rows[] = {
    {8, 3.5, 0, -30, 11},    {16, 3.5, 0, -25, 11},  {24, 3.5, 0, -20, 11},
    {32, 3.5, 6, -15, 11},   {40, 3.5, 6, -10, 11},  {48, 3.5, 5, -10, 11},
    {56, 3.5, 5, -6, 11},    {64, 3.5, 4, -2, 11},   {80, 3.5, 0.5, 0, 8},
    {96, 2.50, 0, 1, 5.5},   {112, 1.80, 0, 2, 4.5}, {128, 1.41, 0, 3, 4},
    {160, 1.13, -2, 5, 3.5}, {192, 1.49, -4, 7, 3},  {224, 1.25, -6, 9, 2},
    {256, 0.97, -8, 10, 1},  {320, 0.90, -10, 12, 0}};
#define ROWS (int)(sizeof(rows) / sizeof(*rows))

static void bitrate_rows(void) {
  const int count = ROWS;

  printf("/* Per-bitrate rows (doc/psy/parameters.md §1). Logarithms in "
         "Q16. */\n");
  printf("typedef struct {\n"
         "  int kbps;\n"
         "  int32_t log2_two_msfix;  /* 2 * MSFIX */\n"
         "  int32_t log2_mask_adjust; /* 10^(MASK_ADJUST/10) */\n"
         "  int32_t log2_ath_offset; /* ATH_OFFSET */\n"
         "  int32_t ath_curve;       /* ATH_CURVE, Q16 */\n"
         "  int32_t log2_ath_floor;  /* ath_floor, with ATH_OFFSET */\n"
         "  int32_t minval_depth;    /* MINVAL_DEPTH in dB, Q16 */\n"
         "  int32_t log2_mask_adjust_short; /* MASK_ADJUST_SHORT */\n"
         "  int32_t attack_ratio;    /* ATTACK_RATIO, Q16 */\n"
         "  int32_t attack_ratio_side; /* ATTACK_RATIO_SIDE, Q16 */\n"
         "  int32_t pe_split_power;  /* PE_SPLIT_POWER, Q16 */\n"
         "  int32_t log2_mask_adjust_2ch; /* MASK_ADJUST, 2 channels */\n"
         "  int32_t log2_mask_adjust_short_2ch;\n"
         "} psy_bitrate_row_t;\n");
  printf("#define PSY_BITRATE_ROWS %d\n", count);
  printf("static const psy_bitrate_row_t psy_bitrate_rows[PSY_BITRATE_ROWS] = "
         "{\n");
  for (int i = 0; i < count; i++) {
    double c = rows[i].ath_curve, ath_min = 1e9;

    /* The curve's minimum, near 3.4 kHz. */
    for (double f = 1000; f < 8000; f += 0.01)
      if (ath_curve(f, c) < ath_min)
        ath_min = ath_curve(f, c);
    /* ATTACK_RATIO and ATTACK_RATIO_SIDE of the row. */
    int k = rows[i].kbps;
    double attack = k <= 112   ? 6.60
                    : k == 128 ? 6.40
                    : k == 160 ? 6.00
                    : k == 192 ? 5.60
                               : 5.20;
    double side = k <= 112 ? 145 : k == 128 ? 140 : k == 160 ? 135 : 125;
    double split = k <= 80 ? 2.4 : k == 96 ? 1.8 : k <= 128 ? 1.4 : 1;
    /* MASK_ADJUST of two-channel streams, where it differs. */
    double adjust2 = k == 32 || k == 40 || k == 56 ? 3
                     : k == 48 || k == 64          ? 3.5
                                                   : rows[i].mask_adjust;

    double adjust = rows[i].mask_adjust;

    printf(
        "{%d, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld},\n",
        rows[i].kbps, log_q16(2 * rows[i].msfix), db_q16(rows[i].mask_adjust),
        db_q16(-rows[i].ath_lower), fixed(c, LOG_ONE),
        db_q16(ath_min - ATH_FS_SPL - rows[i].ath_lower),
        fixed(5.0 * rows[i].kbps / 320, LOG_ONE),
        db_q16(adjust > 0 ? 0.9 * adjust : 1.1 * adjust),
        fixed(attack, LOG_ONE), fixed(side, LOG_ONE), fixed(split, LOG_ONE),
        db_q16(adjust2), db_q16(adjust2 > 0 ? 0.9 * adjust2 : 1.1 * adjust2));
  }
  printf("};\n\n");
}

/* Attack detection (doc/psy/block-switching.md §1). Filtered samples and
 * peaks are in Q16 of a 16-bit sample: full scale is 2^31. */
static void attack_tables(void) {
  static const double taps[5] = {-0.0170317, 0.0418072, -0.0876324, 0.186348,
                                 -0.627638};
  const double full_scale = 32768.0 * 65536;

  printf("/* Attack filter taps c[1], c[3], .. c[9], Q16. */\n"
         "static const int32_t psy_attack_taps[5] = {");
  for (int i = 0; i < 5; i++)
    printf("%s%ld", i ? ", " : "", fixed(taps[i], LOG_ONE));
  printf("};\n");
  printf("#define PSY_PEAK_FLOOR %ld\n",
         fixed(full_scale * pow(10, -90.3 / 20), 1));
  printf("#define PSY_ATTACK_DECAY 10\n");
  printf("#define PSY_PERIODIC_RATIO %ld /* Q16 */\n", fixed(1.7, LOG_ONE));
  printf("/* 3 * PERIODIC_CEILING, for a sum of three peaks. */\n"
         "#define PSY_PERIODIC_CEILING_SUM %ldLL\n",
         fixed(3 * full_scale * pow(10, -7.8 / 20), 1));
  printf("#define PSY_PULSE_DIVISOR 6 /* 1 / PULSE_FRACTION */\n\n");
}

/* doc/psy/masking.md §3.3, short: x in dB before its clamps, Q16. */
static void short_minval(void) {
  printf("/* x of the short tonal ceiling, before its clamps, in dB Q16\n"
         " * (doc/psy/masking.md §3.3). */\n");
  printf("static const int32_t psy_part_minval_x_s[9][PSY_NPART_S_MAX] = {\n");
  for (int r = 0; r < 9; r++) {
    printf("{");
    for (int i = 0; i < short_layouts[r].npart; i++) {
      double bval = short_layouts[r].bval[i], x = 7 * (bval / 12 - 1);

      if (bval > 12)
        x *= 1 + 3.1 * log(1 + x);
      if (bval < 12)
        x *= 1 + 2.3 * log(1 - x);
      printf("%s%ld", i ? ", " : "", fixed(x, LOG_ONE));
    }
    printf("},\n");
  }
  printf("};\n\n");
}

static void masking_tables(void) {
  static const double tab[9] = {1.0,     0.79433, 0.63096, 0.63096, 0.63096,
                                0.63096, 0.63096, 0.25119, 0.11749};
  long values[9];

  printf("/* Masking table (doc/psy/conventions.md, masking.md §2). */\n");
  for (int k = 0; k < 9; k++)
    values[k] = log_q16(tab[k]);
  print_array("int32_t", "psy_tab", values, 9);
  printf("#define PSY_LOG2_AVG_MASK_FACTOR %ld\n", log_q16(AVG_MASK_FACTOR));
  printf("#define PSY_LOG2_PREECHO_RATIO_1 %ld\n", log_q16(2));
  printf("#define PSY_LOG2_PREECHO_RATIO_2 %ld\n", log_q16(16));
  printf("#define PSY_LOG2_MS_SIMILARITY %ld\n", log_q16(MS_SIMILARITY));
  printf("/* 1/sqrt(2), Q31: mid and side (doc/psy/conventions.md). */\n");
  printf("#define PSY_INV_SQRT2 %ld\n", fixed(sqrt(0.5), 2147483648.0));
  printf("/* log2(10)/10, Q16: dB to log2 of an energy factor. */\n");
  printf("#define PSY_DB_TO_LOG2 %ld\n\n", db_q16(1));
}

static void ath_constants(void) {
  printf("/* ATH adaptation (doc/psy/ath.md §3, §4). */\n");
  printf("/* 20 * log10(2) / ATH_ADAPT_RANGE, Q16: the weight slope per log2 "
         "of\n * adjust_factor. */\n");
  printf("#define PSY_ATH_ADAPT_SLOPE %ld\n",
         fixed(20 * log10(2) / ATH_ADAPT_RANGE, LOG_ONE));
  printf("#define PSY_LOG2_ATH_ADAPT_GAIN %ld\n", db_q16(ATH_ADAPT_GAIN));
  printf("/* Adaptive ATH update, Q31. */\n");
  printf("#define PSY_ADJUST_LOUD %ld\n", fixed(0.03125, 2147483648.0));
  printf("#define PSY_ADJUST_SLOPE %ld /* 31.98, Q16 */\n",
         fixed(31.98, LOG_ONE));
  printf("#define PSY_ADJUST_MIN %ld\n", fixed(0.000625, 2147483648.0));
  printf("#define PSY_ADJUST_RELEASE_SLOPE %ld\n", fixed(0.075, 2147483648.0));
  printf("#define PSY_ADJUST_RELEASE_BASE %ld\n", fixed(0.925, 2147483648.0));
  printf("#define PSY_ADJUST_INITIAL %ld\n", fixed(0.01, 2147483648.0));
  printf("#define PSY_ADJUST_ONE 2147483648\n\n");
}

/* PE (doc/psy/perceptual-entropy.md §2), Q8. */
static void pe_constants(void) {
  static const double coefficients[21] = {
      6.8,  5.8,  5.8,  6.4,  6.5,  9.9,  12.1, 14.4, 15.0, 18.9, 21.6,
      26.9, 34.2, 40.2, 46.8, 56.5, 60.7, 73.9, 85.7, 93.4, 126.1};
  long values[21];

  printf("/* PE (doc/psy/perceptual-entropy.md §2), Q8. */\n");
  for (int i = 0; i < 21; i++)
    values[i] = fixed(coefficients[i], 256);
  print_array("int32_t", "psy_pe_long", values, 21);
  printf("#define PSY_PE_LONG_BASE %ld\n", fixed(1124.23 / 4, 256));
  {
    static const double short_coefficients[12] = {
        11.8, 13.6, 17.2, 32, 46.5, 51.3, 57.5, 67.1, 71.5, 84.6, 97.6, 130};

    for (int i = 0; i < 12; i++)
      values[i] = fixed(short_coefficients[i], 256);
    print_array("int32_t", "psy_pe_short", values, 12);
    printf("#define PSY_PE_SHORT_BASE %ld\n", fixed(1236.28 / 4, 256));
  }
  printf("/* log10(2), and the ratio cap log10(PE_RATIO_CAP), Q16. */\n");
  printf("#define PSY_LOG10_2 %ld\n", fixed(log10(2), LOG_ONE));
  printf("#define PSY_PE_RATIO_CAP %ld\n\n",
         fixed(PE_RATIO_CAP_LOG10, LOG_ONE));
}

/* Energy unit: a sine at full scale has a total line energy of 1. */
static void energy_unit(void) {
  double sine = mdct_sine_energy(0), short_sine = mdct_sine_energy(1);

  printf("/* Energy unit (doc/psy/conventions.md). log2 K_long, for squared "
         "MDCT\n * coefficients, measured on the library's filterbank. */\n");
  printf("#define PSY_LOG2_K_LONG %ld\n", log_q16(1 / sine));
  printf(
      "/* log2 K_short, for the squared coefficients of a short window. */\n");
  printf("#define PSY_LOG2_K_SHORT %ld\n", log_q16(1 / short_sine));
  printf("/* log2 K_loud: binary white noise at full scale, of energy 2 per "
         "granule\n * spread evenly over the lines, gives LOUDNESS_FS_NOISE "
         "(doc/psy/ath.md\n * §4.1). */\n");
  printf("#define PSY_LOG2_K_LOUD %ld\n\n",
         log_q16(LOUDNESS_FS_NOISE / (2.0 / LONG_LINES)));
}

/* The allocation (doc/psy/allowed-distortion.md, noise-allocation.md). */
static void allocation_tables(void) {
  long values[22];

  printf("/* log2 fact[sb], the long band offsets "
         "(doc/psy/allowed-distortion.md §1). */\n");
  for (int sb = 0; sb < 22; sb++)
    values[sb] = db_q16(sb <= 6    ? -0.5
                        : sb <= 13 ? -0.25
                        : sb <= 20 ? -0.025
                                   : 0.5);
  print_array("int32_t", "psy_band_offset", values, 22);
  for (int sb = 0; sb < 13; sb++)
    values[sb] = db_q16(sb <= 2    ? -2.0
                        : sb <= 6  ? -1.0
                        : sb <= 11 ? -0.05
                                   : 0.5);
  printf("/* The short band offsets. */\n");
  print_array("int32_t", "psy_band_offset_s", values, 13);
  /* decay = 10^(-192 / (TEMPORAL_SUSTAIN fs)), TEMPORAL_SUSTAIN = 0.01 s. */
  printf("/* log2 of decay and of 1 - decay, the temporal masking between\n"
         " * short windows (doc/psy/allowed-distortion.md §3). */\n");
  for (int part = 0; part < 2; part++) {
    for (int r = 0; r < 9; r++) {
      double decay = pow(10, -192 / (0.01 * shine_samplerates[r]));

      values[r] = log_q16(part ? 1 - decay : decay);
    }
    print_array("int32_t",
                part ? "psy_log2_temporal_keep" : "psy_log2_temporal_decay",
                values, 9);
  }
  printf("/* XMIN_FLOOR, -200 dB below a full-scale sine. */\n");
  printf("#define PSY_LOG2_XMIN_FLOOR %ld\n", db_q16(-200));
  printf("/* ix^(4/3) for ix = 0..IX_MAX, Q14 (doc/psy/noise-allocation.md "
         "§1). */\n");
  printf("#define PSY_IX_MAX 8192\n");
  printf("static const uint32_t psy_pow43[PSY_IX_MAX + 1] = {");
  for (int i = 0; i <= 8192; i++)
    printf("%s%luu", i ? ", " : "",
           (unsigned long)fixed(pow(i, 4.0 / 3), 16384));
  printf("};\n");
  printf("/* 2^(r/4) for r = 0..3, Q30: the fraction of a quarter step. */\n");
  printf("static const uint32_t psy_quarter_step[4] = {");
  for (int r = 0; r < 4; r++)
    printf("%s%luu", r ? ", " : "",
           (unsigned long)fixed(pow(2, r / 4.0), 1073741824.0));
  printf("};\n\n");
}

/* log2(1 + i/512) and 2^(i/512), for the integer log2 and its inverse
 * (doc/psy/fixed-point.md §2). */
static void log_tables(void) {
  printf("/* log2(1 + i/512), Q16. */\n");
  printf("static const int32_t psy_log2_table[513] = {");
  for (int i = 0; i <= 512; i++)
    printf("%s%ld", i ? ", " : "", log_q16(1 + i / 512.0));
  printf("};\n");
  printf("/* 2^(i/512), Q30. */\n");
  printf("static const uint32_t psy_exp2_table[513] = {");
  for (int i = 0; i <= 512; i++)
    printf("%s%luu", i ? ", " : "",
           (unsigned long)fixed(pow(2, i / 512.0), 1073741824.0));
  printf("};\n\n");
}

int main(void) {
  printf("/* Generated by tools/generate_psy_tables.c: do not edit. */\n\n");
  printf("#ifndef PSY_TABLES_H\n#define PSY_TABLES_H\n\n");
  printf("#include <stdint.h>\n\n");
  printf("#define PSY_LOG_ONE 65536\n\n");
  partitions(layouts, LONG_LINES, "", "PSY_NPART_MAX");
  spreading(layouts, 0, "", "PSY_NPART_MAX", "PSY_SPREAD_MAX");
  partitions(short_layouts, 192, "_s", "PSY_NPART_S_MAX");
  spreading(short_layouts, 1, "_s", "PSY_NPART_S_MAX", "PSY_SPREAD_S_MAX");
  ath_lines();
  bitrate_rows();
  attack_tables();
  short_minval();
  masking_tables();
  ath_constants();
  pe_constants();
  energy_unit();
  allocation_tables();
  log_tables();
  printf("#endif\n");
  return 0;
}
