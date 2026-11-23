/* Conformance checks of the psychoacoustic model, phase 1: the invariants of
 * doc/psy/tests.md §1-§2 and the checks of doc/psy/known-complexity.md
 * (KC n below). Each check reports how many times it ran, and the suite fails
 * if any ran zero times.
 *
 * It includes the model's source to reach its internal values. */

#include "psy.c"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  PARTITIONS_COVER,     /* spectral-analysis §3, KC 1 */
  PARTITIONS_NEST,      /* spectral-analysis §3, KC 1, 16 */
  TABLES_RECOMPUTED,    /* KC 3, 14 */
  SINE_ENERGY,          /* conventions, KC 20 */
  BAND_ENERGY_SUM,      /* spectral-analysis §5, KC 1 */
  SFB_EDGE_SWEEP,       /* KC 1 */
  THRESHOLD_BOUNDS,     /* masking, KC 4 */
  MASK_ADD,             /* masking §2, KC 13 */
  TONALITY,             /* masking §1, KC 15 */
  TONE_THRESHOLD,       /* masking, KC 15, 16 */
  LIMIT_ORDER,          /* masking §3.4, KC 17 */
  PREECHO_LIMIT,        /* masking §3.1, KC 9 */
  SPREADING_RATES,      /* KC 14 */
  LEVEL_SCALING,        /* KC 19 */
  ATH_PART,             /* ath §2, KC 2 */
  LOUDNESS_REFERENCE,   /* ath §4.1, KC 18, 19 */
  ADAPTIVE_ATH_FADE,    /* ath §4.2, KC 18 */
  ADAPTIVE_ATH_LEVEL,   /* ath §4.2, KC 18, 19 */
  ATH_ADJUST,           /* ath §3, KC 18 */
  MS_SIDE_ZERO,         /* stereo, KC 12 */
  MS_SUM_LIMIT,         /* stereo §2, KC 12 */
  MS_DEMASKING,         /* stereo §2, KC 12 */
  MS_INDEPENDENCE,      /* stereo, KC 12 */
  PE_BOUNDS,            /* perceptual-entropy, KC 21 */
  PE_CAP_CONTINUITY,    /* perceptual-entropy §2, KC 21 */
  CLICK_ALIGNMENT,      /* signal-path, KC 20 */
  LEADING_SILENCE,      /* signal-path §4, KC 9 */
  SILENCE_STATE,        /* signal-path §4, KC 5 */
  INDEPENDENT_ENCODERS, /* signal-path §5, KC 8, 20 */
  ALLOCATION_BOUNDS,    /* noise-allocation §1, §3 */
  SCALEFACTOR_CODING,   /* noise-allocation §5 */
  ALLOCATION_BEST,      /* noise-allocation §4 */
  ALLOCATION_SILENCE,   /* noise-allocation §4 */
  ALLOCATION_TIE,       /* noise-allocation §2 */
  NOISE_MEASURE,        /* noise-allocation §1, §2 */
  ALLOWED_DISTORTION,   /* allowed-distortion §2 */
  GRANULE_BITS,         /* reservoir §3, KC 21 */
  PE_SMOOTHING,         /* perceptual-entropy §3, KC 21 */
  RESERVOIR_STEADY,     /* reservoir §3 */
  RESERVOIR_BURST,      /* reservoir §3 */
  STREAM_LAYOUT,        /* reservoir §1, §4 */
  ALLOCATION_SURPLUS,   /* noise-allocation §4.1 */
  CHECK_COUNT
};

static const char *check_names[CHECK_COUNT] = {
    [PARTITIONS_COVER] = "partitions cover the lines",
    [PARTITIONS_NEST] = "partitions nest in sfbs",
    [TABLES_RECOMPUTED] = "tables match their formulas",
    [SINE_ENERGY] = "full-scale sine has energy 1",
    [BAND_ENERGY_SUM] = "band energy is the line sum",
    [SFB_EDGE_SWEEP] = "tone across an sfb edge",
    [THRESHOLD_BOUNDS] = "threshold bounds",
    [MASK_ADD] = "mask_add law",
    [TONALITY] = "tonality index",
    [TONE_THRESHOLD] = "tone threshold",
    [LIMIT_ORDER] = "order of the limits",
    [PREECHO_LIMIT] = "pre-echo limit",
    [SPREADING_RATES] = "spreading across rates",
    [LEVEL_SCALING] = "level scaling",
    [ATH_PART] = "partition ATH",
    [LOUDNESS_REFERENCE] = "loudness reference",
    [ADAPTIVE_ATH_FADE] = "adaptive ATH fade",
    [ADAPTIVE_ATH_LEVEL] = "adaptive ATH level",
    [ATH_ADJUST] = "athAdjust",
    [MS_SIDE_ZERO] = "mid and side of L = R",
    [MS_SUM_LIMIT] = "M/S sum limit",
    [MS_DEMASKING] = "M/S demasking",
    [MS_INDEPENDENCE] = "M/S parameters",
    [GRANULE_BITS] = "bits of a granule",
    [PE_SMOOTHING] = "PE smoothing",
    [RESERVOIR_STEADY] = "steady signal gets the mean",
    [RESERVOIR_BURST] = "burst draws on the reservoir",
    [STREAM_LAYOUT] = "stream layout",
    [ALLOCATION_SURPLUS] = "bits a channel does not need",
    [PE_BOUNDS] = "PE bounds",
    [PE_CAP_CONTINUITY] = "PE cap continuity",
    [CLICK_ALIGNMENT] = "click alignment",
    [LEADING_SILENCE] = "leading silence",
    [SILENCE_STATE] = "silence state",
    [INDEPENDENT_ENCODERS] = "independent encoders",
    [ALLOCATION_BOUNDS] = "ix and bits within bounds",
    [SCALEFACTOR_CODING] = "scalefactor coding",
    [ALLOCATION_BEST] = "best attempt",
    [ALLOCATION_SILENCE] = "silent granule",
    [ALLOCATION_TIE] = "bands allowed their whole energy",
    [NOISE_MEASURE] = "noise measure",
    [ALLOWED_DISTORTION] = "allowed distortion",
};

static int runs[CHECK_COUNT], failures[CHECK_COUNT];

static int check(int id, int ok, const char *format, ...) {
  runs[id]++;
  if (!ok && failures[id]++ < 3) {
    va_list args;

    printf("FAIL %s: ", check_names[id]);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
  }
  return ok;
}

static double db(int32_t l) {
  return l == PSY_LOG_ZERO ? -INFINITY : l / 65536.0 * 10 * log10(2);
}

static double linear(int32_t l) {
  return l == PSY_LOG_ZERO ? 0 : exp2(l / 65536.0);
}

static const int rates[9] = {44100, 48000, 32000, 22050, 24000,
                             16000, 11025, 12000, 8000};

static shine_t open_encoder(int samplerate, int channels, int mode,
                            int bitrate) {
  shine_config_t config;
  shine_t encoder;

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.samplerate = samplerate;
  config.wave.channels = channels;
  config.mpeg.mode = mode;
  config.mpeg.bitrate = bitrate;
  config.mpeg.quality = 1;
  encoder = shine_initialise(&config);
  if (!encoder || !encoder->psy) {
    printf("FAIL: cannot open an encoder at %d Hz\n", samplerate);
    exit(1);
  }
  return encoder;
}

static int lowest_bitrate(int samplerate) {
  return samplerate >= 32000 ? 32 : 8;
}

static int highest_bitrate(int samplerate) {
  return samplerate >= 32000 ? 320 : samplerate >= 16000 ? 160 : 64;
}

/* A signal, sample n of channel ch, before clipping to 16 bits. */
typedef double (*signal_t)(long n, int ch, const void *context);

static int16_t clip(double v) {
  return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrint(v);
}

static void check_granule(shine_t e);

/* allowed-distortion §2: long bands 0..20 have an xmin, 0..16 at 8 kHz. */
static int xmin_bands(shine_t e) {
  return e->wave.samplerate <= 8000 ? 17 : 21;
}

/* Encodes one frame of `signal` from sample `*position`, checking the
 * allocation of every granule; returns the size of the data written. */
static int encode_frame(shine_t encoder, signal_t signal, const void *context,
                        long *position, uint64_t *hash) {
  int16_t samples[2 * SHINE_MAX_SAMPLES];
  int channels = encoder->wave.channels, written;
  int count = shine_samples_per_pass(encoder);
  unsigned char *data;

  for (int i = 0; i < count; i++, (*position)++)
    for (int ch = 0; ch < channels; ch++)
      samples[i * channels + ch] = clip(signal(*position, ch, context));
  data = shine_encode_buffer_interleaved(encoder, samples, &written);
  check_granule(encoder);
  if (hash)
    for (int i = 0; i < written; i++)
      *hash = (*hash ^ data[i]) * 0x100000001b3ULL;
  return written;
}

typedef struct {
  double frequency, amplitude, right; /* right = gain of the right channel */
  int samplerate;
} tone_t;

static double tone(long n, int ch, const void *context) {
  const tone_t *t = context;

  return (ch ? t->right : 1) * t->amplitude *
         sin(2 * M_PI * t->frequency * n / t->samplerate);
}

/* Deterministic noise in [-1, 1), from the sample index. */
static double noise_at(long n, int ch) {
  uint64_t x = (uint64_t)n * 2 + (uint64_t)ch + 0x9e3779b97f4a7c15ULL;

  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return (double)(x >> 11) / (1ULL << 52) - 1;
}

typedef struct {
  double amplitude;
  int binary;
} noise_t;

static double noise(long n, int ch, const void *context) {
  const noise_t *z = context;
  double v = noise_at(n, ch);

  return z->amplitude * (z->binary ? (v < 0 ? -1 : 1) : v);
}

/* Noise plus tones, different on each channel, at a gain. */
static double music(long n, int ch, const void *context) {
  double gain = *(const double *)context;

  return gain *
         (4000 * noise_at(n, ch) + 9000 * sin(n * 0.0712) +
          5000 * sin(n * (ch ? 0.311 : 0.4001)) + 3000 * sin(n * 1.1 + ch));
}

static double silence(long n, int ch, const void *context) {
  (void)n, (void)ch, (void)context;
  return 0;
}

/* doc/psy/tests.md §2. */
enum {
  FAILURE_SILENCE,
  FAILURE_ONE_SILENT,
  FAILURE_EQUAL,
  FAILURE_OPPOSITE,
  FAILURE_IMPULSE,
  FAILURE_DC,
  FAILURE_CLIPPED,
  FAILURE_FIRST_SAMPLE,
  FAILURE_COUNT
};

static double failure_mode(long n, int ch, const void *context) {
  int mode = *(const int *)context;
  double v = 40000 * noise_at(n, 0);

  switch (mode) {
  case FAILURE_SILENCE:
    return 0;
  case FAILURE_ONE_SILENT:
    return ch ? 0 : v;
  case FAILURE_EQUAL:
    return v;
  case FAILURE_OPPOSITE:
    return ch ? -v : v;
  case FAILURE_IMPULSE:
    return n == 0 ? 32767 : 0;
  case FAILURE_DC:
    return 32767;
  case FAILURE_CLIPPED:
    return (n / 37) % 2 ? 32767 : -32768;
  default:
    return n == 0 && ch == 0 ? 1 : 0;
  }
}

/* The independent formulas of doc/psy/conventions.md. */
static double bark(double f) {
  double F = fmax(f, 0) / 1000;
  return 13 * atan(0.76 * F) + 3.5 * atan(F / 7.5 * (F / 7.5));
}

static double ath_db(double f, double c) {
  double F = fmin(fmax(f / 1000, 0.1), 24.0);
  return 3.64 * pow(F, -0.8) - 6.8 * exp(-0.6 * (F - 3.4) * (F - 3.4)) +
         6 * exp(-0.15 * (F - 8.7) * (F - 8.7)) +
         (0.6 + 0.04 * c) * 0.001 * pow(F, 4);
}

static double spreading(double d) {
  double t = d >= 0 ? 3 * d : 1.5 * d, x = 0, y;

  if (t >= 0.5 && t <= 2.5)
    x = 8 * ((t - 0.5) * (t - 0.5) - 2 * (t - 0.5));
  t += 0.474;
  y = 15.811389 + 7.5 * t - 17.5 * sqrt(1 + t * t);
  return y <= -60 ? 0 : pow(10, (x + y) / 10) / 0.6609193;
}

static void check_tables(void) {
  for (int r = 0; r < 9; r++) {
    const int *sfb = shine_scale_fact_band_index[r];
    const int16_t *start = psy_part_start[r];
    int npart = psy_npart[r], ok = start[0] == 0 && start[npart] == 576;
    double delta = rates[r] / 1152.0;

    for (int b = 0; b < npart; b++)
      ok &= start[b + 1] > start[b];
    check(PARTITIONS_COVER, ok, "%d Hz: partitions do not cover 0..575",
          rates[r]);
    for (int b = 0; b < npart; b++) {
      int straddles = 0;

      for (int sb = 0; sb <= 22; sb++)
        straddles |= sfb[sb] > start[b] && sfb[sb] < start[b + 1];
      /* The tonality index's 64-bit headroom assumes under 256 lines. */
      check(PARTITIONS_NEST, !straddles && start[b + 1] - start[b] < 256,
            "%d Hz: partition %d spans lines %d..%d", rates[r], b, start[b],
            start[b + 1] - 1);
    }
    for (int b = 0; b < npart; b++) {
      int s = start[b], w = start[b + 1] - s;
      double bval = (bark((s + 0.5) * delta) + bark((s + w - 0.5) * delta)) / 2;
      int lo = psy_spread_lo[r][b], hi = psy_spread_hi[r][b];

      check(TABLES_RECOMPUTED,
            fabs(psy_part_bval[r][b] / 65536.0 - bval) < 1e-4,
            "%d Hz: bval of partition %d", rates[r], b);
      check(TABLES_RECOMPUTED, lo <= b && b <= hi,
            "%d Hz: spreading row %d does not contain itself", rates[r], b);
      for (int j = lo; j <= hi; j++) {
        int sj = start[j], wj = start[j + 1] - sj;
        double bj =
            (bark((sj + 0.5) * delta) + bark((sj + wj - 0.5) * delta)) / 2;
        double width = bark((s + w) * delta) - bark(s * delta);
        double expected = log2(spreading(bval - bj) * width);
        double actual =
            psy_spread[r][psy_spread_offset[r][b] + j - lo] / 65536.0;

        check(TABLES_RECOMPUTED, fabs(actual - expected) < 1e-4,
              "%d Hz: S[%d][%d] is 2^%f, expected 2^%f", rates[r], b, j, actual,
              expected);
      }
    }
    for (int k = 0; k < 576; k++) {
      /* Line centre frequency, through the ATH it is evaluated at. */
      double f = (k + 0.5) * rates[r] / 1152.0;
      double expected = (ath_db(f, 0) - 96) * log2(10) / 10;

      check(TABLES_RECOMPUTED,
            fabs(psy_ath_line[r][k] / 65536.0 - expected) < 1e-4,
            "%d Hz: ATH of line %d", rates[r], k);
    }
  }
}

/* KC 20: on a line centre and between lines, averaged over phase. */
static void check_sine_energy(void) {
  for (int r = 0; r < 9; r++)
    for (int between = 0; between < 2; between++) {
      tone_t t = {(37 + 0.5 * !between + 0.5 * between) * rates[r] / 1152.0,
                  32767, 0, rates[r]};
      shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
      long position = 0;
      double sum = 0;
      int granules = 0;

      for (int frame = 0; frame < 60; frame++) {
        encode_frame(e, tone, &t, &position, NULL);
        if (frame >= 4)
          for (int g = 0; g < e->mpeg.granules_per_frame; g++, granules++)
            sum += linear(e->psy->energy[g][0]);
      }
      sum = 10 * log10(sum / granules * (32768.0 / 32767) * (32768.0 / 32767));
      check(SINE_ENERGY, fabs(sum) < 0.25, "%d Hz, %s: mean energy %.3f dB",
            rates[r], between ? "between lines" : "on a line", sum);
      shine_close(e);
    }
}

/* Band energies against K_long times the squared coefficients, and the
 * band thresholds' bounds, on music-like input. */
static void check_band_energy(void) {
  for (int r = 0; r < 9; r++) {
    shine_t e = open_encoder(rates[r], 1, SHINE_MONO, lowest_bitrate(rates[r]));
    double gain = 1;
    long position = 0;

    for (int frame = 0; frame < 6; frame++) {
      encode_frame(e, music, &gain, &position, NULL);
      for (int g = 0; g < e->mpeg.granules_per_frame; g++) {
        const int *sfb = shine_scale_fact_band_index[e->mpeg.samplerate_index];

        for (int sb = 0; sb < 22; sb++) {
          double direct = 0;

          for (int k = sfb[sb]; k < sfb[sb + 1]; k++)
            direct += (double)e->mdct_freq[0][g][k] * e->mdct_freq[0][g][k];
          direct = 10 * log10(direct * exp2(PSY_LOG2_K_LONG / 65536.0));
          check(BAND_ENERGY_SUM, fabs(db(e->psy->en[g][0][sb]) - direct) < 0.01,
                "%d Hz band %d: %.3f dB, lines sum to %.3f dB", rates[r], sb,
                db(e->psy->en[g][0][sb]), direct);
        }
      }
    }
    shine_close(e);
  }
}

/* KC 1: a tone gliding across the edge between two sfbs leaves its energy
 * in them, moving from one to the other with no jump. */
static double glide(long n, int ch, const void *context) {
  const double *f = context; /* from, to, samplerate, samples */
  double progress = fmin(n / f[3], 1);
  double phase =
      2 * M_PI * (f[0] * n + (f[1] - f[0]) * progress * n / 2) / f[2];

  (void)ch;
  return 30000 * sin(phase);
}

static void check_sfb_edge(void) {
  /* Per granule, the split between the two lines of a tone depends on its
   * phase: the share is compared over blocks of granules. */
  enum { BLOCK = 8, BLOCKS = 8 };

  for (int r = 0; r < 9; r++)
    for (int sb = 6; sb <= 12; sb += 3) {
      const int *sfb = shine_scale_fact_band_index[r];
      double line = rates[r] / 1152.0;
      double f[4] = {(sfb[sb + 1] - 1.5) * line, (sfb[sb + 1] + 1.5) * line,
                     rates[r], (BLOCKS + 1) * BLOCK * 576};
      shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
      long position = 0;
      double low = 0, high = 0, total = 0, last_share = -1, first_share = -1;
      int granule = 0;

      while (position < f[3]) {
        encode_frame(e, glide, f, &position, NULL);
        for (int g = 0; g < e->mpeg.granules_per_frame; g++, granule++) {
          double share;

          if (granule < BLOCK)
            continue;
          low += linear(e->psy->en[g][0][sb]);
          high += linear(e->psy->en[g][0][sb + 1]);
          total += linear(e->psy->energy[g][0]);
          if ((granule + 1) % BLOCK)
            continue;
          share = high / (low + high);
          check(SFB_EDGE_SWEEP, low + high > 0.9 * total,
                "%d Hz edge %d: the two bands hold %.2f of the energy",
                rates[r], sfb[sb + 1], (low + high) / total);
          check(SFB_EDGE_SWEEP, share > last_share - 0.1,
                "%d Hz edge %d: share falls from %.2f to %.2f", rates[r],
                sfb[sb + 1], last_share, share);
          if (first_share < 0)
            first_share = share;
          last_share = share;
          low = high = total = 0;
        }
      }
      check(SFB_EDGE_SWEEP, first_share < 0.2 && last_share > 0.8,
            "%d Hz edge %d: the share moved from %.2f to %.2f", rates[r],
            sfb[sb + 1], first_share, last_share);
      shine_close(e);
    }
}

/* Every output finite, within its stated bounds. */
static void check_outputs(const psy_model_t *m, int granules,
                          const char *name) {
  const int32_t pe_max = PSY_PE_LONG_BASE + 10 * 256 * 1000;

  for (int g = 0; g < granules; g++)
    for (int c = 0; c < m->channels; c++) {
      int ok = 1;

      for (int sb = 0; sb < 22; sb++) {
        int32_t en = m->en[g][c][sb], thm = m->thm[g][c][sb];

        ok &= thm <= en;
        ok &= en == PSY_LOG_ZERO || (en > -400 * ONE && en < 8 * ONE);
        ok &= thm == PSY_LOG_ZERO || thm > -500 * ONE;
      }
      check(THRESHOLD_BOUNDS, ok, "%s gr %d ch %d", name, g, c);
      check(PE_BOUNDS, m->pe[g][c] >= PSY_PE_LONG_BASE && m->pe[g][c] < pe_max,
            "%s gr %d ch %d: PE %d", name, g, c, m->pe[g][c]);
    }
  check(ADAPTIVE_ATH_FADE,
        m->adjust_factor >= PSY_ADJUST_MIN &&
            m->adjust_factor <= PSY_ADJUST_ONE,
        "%s: adjust_factor %f", name, m->adjust_factor / 2147483648.0);
}

/* KC 4: doc/psy/tests.md §2, at every rate, mono and joint stereo. */
static void check_failure_modes(void) {
  for (int r = 0; r < 9; r++)
    for (int stereo = 0; stereo < 2; stereo++)
      for (int mode = 0; mode < FAILURE_COUNT; mode++) {
        int bitrate = lowest_bitrate(rates[r]);
        shine_t e =
            open_encoder(rates[r], stereo + 1,
                         stereo ? SHINE_JOINT_STEREO : SHINE_MONO, bitrate);
        long position = 0;
        char name[64];

        snprintf(name, sizeof(name), "%d Hz %s, failure mode %d", rates[r],
                 stereo ? "stereo" : "mono", mode);
        for (int frame = 0; frame < 8; frame++) {
          encode_frame(e, failure_mode, &mode, &position, NULL);
          check_outputs(e->psy, e->mpeg.granules_per_frame, name);
        }
        shine_close(e);
      }
}

/* KC 13 */
static void check_mask_add(void) {
  for (int32_t x = -40 * ONE; x <= 0; x += 3 * ONE + 777) {
    check(MASK_ADD,
          mask_add(x, ZERO, 0, 2) == x && mask_add(ZERO, x, 5, 0) == x,
          "mask_add(%d, 0) is not %d", x, x);
    for (int distance = -3; distance <= 3; distance++)
      for (int d = -1; d <= 2; d++)
        for (int32_t y = x - 30 * ONE; y <= x + 30 * ONE; y += ONE / 4 + 1) {
          double m1 = linear(x), m2 = linear(y),
                 r = fmax(m1, m2) / fmin(m1, m2);
          double result = linear(mask_add(x, y, distance, d)), expected;
          int near = abs(distance) <= d;

          check(MASK_ADD,
                result >= fmax(m1, m2) * 0.9999 &&
                    result <= (m1 + m2) * 1.40548 * 1.40548 * 1.0001,
                "mask_add(%g, %g) = %g", m1, m2, result);
          /* The law, away from its steps. */
          if (fabs(16 * log10(r) - lrint(16 * log10(r))) < 0.01)
            continue;
          if (near)
            expected =
                r >= pow(10, 9.0 / 16)
                    ? m1 + m2
                    : (m1 + m2) *
                          pow(linear(psy_add_boost[(int)(16 * log10(r))]), 1);
          else
            expected = r < pow(10, 24.0 / 16) ? m1 + m2 : fmax(m1, m2);
          check(MASK_ADD, fabs(10 * log10(result / expected)) < 0.002,
                "mask_add(%g, %g, %d, %d) = %g, law gives %g", m1, m2, distance,
                d, result, expected);
        }
  }
}

/* KC 15: a flat spectrum gives 0, a tone on a line centre the maximum, and
 * white noise a mean index that falls as partitions widen. */
static void check_tonality(void) {
  for (int r = 0; r < 9; r++) {
    shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
    psy_model_t *m = e->psy;
    uint64_t flat[PSY_NPART_MAX], average[PSY_NPART_MAX];
    psy_partitions_t parts;
    int ok = 1, line = 70;
    tone_t t = {(line + 0.5) * rates[r] / 1152.0, 30000, 0, rates[r]};
    noise_t z = {30000, 0};
    double narrow = 0, wide = 0;
    int narrow_count = 0, wide_count = 0;
    long position = 0;

    for (int b = 0; b < m->npart; b++)
      flat[b] = average[b] = 123456789;
    tonality(m, flat, average, &parts);
    for (int b = 0; b < m->npart; b++)
      ok &= parts.idx[b] == 0;
    check(TONALITY, ok, "%d Hz: a flat spectrum is tonal", rates[r]);

    /* §spectral-analysis 1.2: a tone, then a flat spectrum; their average
     * is still peaked. */
    for (int pass = 0; pass < 2; pass++) {
      psy_channel_t channel;
      psy_partitions_t smoothed;
      int64_t x[GRANULE_SIZE];

      memset(&channel, 0, sizeof(channel));
      for (int k = 0; k < GRANULE_SIZE; k++)
        x[k] = k == line ? 1 << 28 : 0;
      if (pass)
        analyse(m, &channel, x, &smoothed);
      channel.granules_seen = pass;
      for (int k = 0; k < GRANULE_SIZE; k++)
        x[k] = 1 << 20;
      analyse(m, &channel, x, &smoothed);
      for (int b = 0; b < m->npart; b++)
        if (psy_part_start[r][b] <= line && line < psy_part_start[r][b + 1])
          check(TONALITY, pass ? smoothed.idx[b] == 8 : smoothed.idx[b] == 0,
                "%d Hz: flat spectrum %s a tone has index %d", rates[r],
                pass ? "after" : "without", smoothed.idx[b]);
    }

    for (int frame = 0; frame < 6; frame++)
      encode_frame(e, tone, &t, &position, NULL);
    for (int b = 0; b < m->npart; b++)
      if (psy_part_start[r][b] <= line && line < psy_part_start[r][b + 1])
        check(TONALITY, m->parts[0].idx[b] == 8,
              "%d Hz: a tone on line %d has index %d", rates[r], line,
              m->parts[0].idx[b]);
    shine_close(e);

    e = open_encoder(rates[r], 1, SHINE_MONO, 64);
    m = e->psy;
    position = 0;
    for (int frame = 0; frame < 40; frame++) {
      encode_frame(e, noise, &z, &position, NULL);
      for (int b = 1; b < m->npart - 1; b++) {
        int lines = psy_part_start[r][b + 2] - psy_part_start[r][b - 1];

        if (lines <= 6) {
          narrow += m->parts[0].idx[b];
          narrow_count++;
        } else if (lines >= 60) {
          wide += m->parts[0].idx[b];
          wide_count++;
        }
      }
    }
    if (wide_count && narrow_count)
      check(TONALITY,
            wide / wide_count < narrow / narrow_count && wide / wide_count < 2,
            "%d Hz: noise has mean index %.2f on narrow neighbourhoods, "
            "%.2f on wide ones",
            rates[r], narrow / narrow_count, wide / wide_count);
    shine_close(e);
  }
}

/* KC 15, 16: a pure tone in every usable sfb, including the top one at
 * 8 kHz, has a non-zero threshold below its band energy. */
static void check_tone_threshold(void) {
  for (int r = 0; r < 9; r++)
    for (int sb = 0; sb < 21; sb++) {
      const int *sfb = shine_scale_fact_band_index[r];
      tone_t t = {(sfb[sb] + sfb[sb + 1]) / 2.0 * rates[r] / 1152.0, 20000, 0,
                  rates[r]};
      shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
      long position = 0;

      for (int frame = 0; frame < 4; frame++)
        encode_frame(e, tone, &t, &position, NULL);
      check(TONE_THRESHOLD,
            e->psy->thm[0][0][sb] != PSY_LOG_ZERO &&
                e->psy->thm[0][0][sb] < e->psy->en[0][0][sb],
            "%d Hz sfb %d: threshold %.1f dB, energy %.1f dB", rates[r], sb,
            db(e->psy->thm[0][0][sb]), db(e->psy->en[0][0][sb]));
      shine_close(e);
    }
}

/* KC 17: a partition whose spread threshold exceeds its energy, lowered
 * and raised. */
static void check_limit_order(void) {
  shine_t e = open_encoder(44100, 1, SHINE_MONO, 128);
  psy_model_t *m = e->psy;
  psy_bitrate_row_t row = *m->row;

  for (int raise = 0; raise < 2; raise++) {
    psy_channel_t channel;
    psy_partitions_t parts;
    const int b = 5; /* one line, so the tonal ceiling does not apply */
    int32_t lowering;

    memset(&channel, 0, sizeof(channel));
    memset(&parts, 0, sizeof(parts));
    row.log2_mask_adjust = raise ? 20 * ONE : 0;
    m->row = &row;
    for (int p = 0; p < m->npart; p++) {
      parts.eb[p] = parts.max[p] = p == b ? -40 * ONE : 0;
      parts.idx[p] = 0;
    }
    /* Alone, partition b + 20 spreads below its energy: only a raise before
     * the clamp brings it to the energy. */
    parts.eb[b + 20] = parts.max[b + 20] = 0;
    for (int p = b + 17; p <= b + 23; p++)
      if (p != b + 20)
        parts.eb[p] = parts.max[p] = PSY_LOG_ZERO;
    threshold(m, &channel, &parts);
    lowering = psy_part_masking_lower[0][b] + row.log2_mask_adjust;
    if (raise)
      check(LIMIT_ORDER,
            parts.ecb[b + 20] < parts.eb[b + 20] &&
                parts.thr[b + 20] == parts.eb[b + 20],
            "raised by 20 dB: threshold %.2f dB, spread %.2f dB, energy "
            "%.2f dB",
            db(parts.thr[b + 20]), db(parts.ecb[b + 20]), db(parts.eb[b + 20]));
    check(LIMIT_ORDER, parts.ecb[b] > parts.eb[b],
          "the spread threshold "
          "%.1f dB does not exceed the energy",
          db(parts.ecb[b]));
    check(LIMIT_ORDER,
          parts.thr[b] == (raise ? parts.eb[b] : parts.eb[b] + lowering),
          "lowering %.2f dB: threshold %.2f dB, energy %.2f dB", db(lowering),
          db(parts.thr[b]), db(parts.eb[b]));
  }
  shine_close(e);
}

/* masking.md §3.1: at an onset, no partition threshold exceeds twice the
 * spread threshold of the granule before, or 16 times the one before that.
 * One granule per frame, so that the scratch values are every granule's. */
static double onset(long n, int ch, const void *context) {
  (void)ch, (void)context;
  return (n < 8 * 576 ? 1000 : 30000) * noise_at(n, 0);
}

static void check_preecho(void) {
  for (int r = 3; r < 9; r++) {
    shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
    psy_model_t *m = e->psy;
    int32_t ecb_1[PSY_NPART_MAX], ecb_2[PSY_NPART_MAX];
    long position = 0;

    for (int frame = 0; frame < 10; frame++) {
      int limited = 0, ok = 1;

      memcpy(ecb_2, ecb_1, sizeof(ecb_1));
      memcpy(ecb_1, m->parts[0].ecb, sizeof(ecb_1));
      encode_frame(e, onset, NULL, &position, NULL);
      if (frame < 2)
        continue;
      for (int b = 0; b < m->npart; b++) {
        ok &= m->parts[0].thr[b] <= ecb_1[b] + PSY_LOG2_PREECHO_RATIO_1 &&
              m->parts[0].thr[b] <= ecb_2[b] + PSY_LOG2_PREECHO_RATIO_2;
        limited += m->parts[0].ecb[b] > ecb_1[b] + PSY_LOG2_PREECHO_RATIO_1;
      }
      check(PREECHO_LIMIT, ok, "%d Hz granule %d", rates[r], frame);
      if (frame == 8)
        check(PREECHO_LIMIT, limited > m->npart / 2,
              "%d Hz: the onset raises only %d spread thresholds", rates[r],
              limited);
    }
    shine_close(e);
  }
}

/* KC 14: spreading alone. On a flat density of energy over Bark, with every
 * index 8, neighbouring maskers add linearly, so the spread threshold of an
 * interior partition is its energy times the integral of s3, 1, times tab[8]
 * twice and AVG_MASK_FACTOR, at every rate. Around a single masker, the
 * spread per Bark follows s3. */
static void check_spreading_rates(void) {
  const double integral = 20 * log10(0.11749) + 10 * log10(0.5);

  for (int r = 0; r < 9; r++) {
    shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
    psy_model_t *m = e->psy;
    const int16_t *start = psy_part_start[r];
    double delta = rates[r] / 1152.0;
    int masker = m->npart / 2;

    for (int pass = 0; pass < 2; pass++) {
      psy_channel_t channel;
      psy_partitions_t parts;

      memset(&channel, 0, sizeof(channel));
      memset(&parts, 0, sizeof(parts));
      for (int b = 0; b < m->npart; b++) {
        double width = bark(start[b + 1] * delta) - bark(start[b] * delta);

        parts.eb[b] = pass ? (b == masker ? 0 : PSY_LOG_ZERO)
                           : (int32_t)lrint(log2(width) * 65536);
        parts.max[b] = parts.eb[b];
        parts.idx[b] = pass ? 0 : 8;
      }
      threshold(m, &channel, &parts);
      for (int b = 0; b < m->npart; b++) {
        double z = psy_part_bval[r][b] / 65536.0,
               zm = psy_part_bval[r][masker] / 65536.0;
        double width = bark(start[b + 1] * delta) - bark(start[b] * delta);

        if (!pass && z > 3 && z < 15)
          check(SPREADING_RATES,
                fabs(db(parts.ecb[b]) - db(parts.eb[b]) - integral) < 1,
                "%d Hz partition %d at %.1f Bark: flat density spreads to "
                "%.2f dB of its energy, the integral gives %.2f dB",
                rates[r], b, z, db(parts.ecb[b]) - db(parts.eb[b]), integral);
        /* tab[0] = 1 and AVG_MASK_FACTOR scale the single masker. */
        if (pass && spreading(z - zm) > 1e-5)
          check(SPREADING_RATES,
                fabs(db(parts.ecb[b]) - 10 * log10(width / 2) -
                     10 * log10(spreading(z - zm))) < 0.01,
                "%d Hz: spread at %.2f Bark from the masker is %.2f dB per "
                "Bark, s3 gives %.2f dB",
                rates[r], z - zm, db(parts.ecb[b]) - 10 * log10(width),
                10 * log10(spreading(z - zm) / 2));
      }
    }
    shine_close(e);
  }
}

/* KC 19: in L/R, the thresholds follow the input level exactly, since no
 * absolute level enters them. */
static void check_level_scaling(void) {
  for (int r = 0; r < 9; r += 4) {
    shine_t quiet = open_encoder(rates[r], 2, SHINE_STEREO, 64),
            loud = open_encoder(rates[r], 2, SHINE_STEREO, 64);
    double gain_quiet = 0.25, gain_loud = 0.5;
    long position_quiet = 0, position_loud = 0;
    int bands = 0, off = 0;

    for (int frame = 0; frame < 20; frame++) {
      encode_frame(quiet, music, &gain_quiet, &position_quiet, NULL);
      encode_frame(loud, music, &gain_loud, &position_loud, NULL);
      for (int g = 0; g < quiet->mpeg.granules_per_frame; g++)
        for (int c = 0; c < 2; c++)
          for (int sb = 0; sb < 22; sb++) {
            double step =
                db(loud->psy->thm[g][c][sb]) - db(quiet->psy->thm[g][c][sb]);
            bands++;
            off += fabs(step - 10 * log10(4)) > 0.05;
          }
    }
    /* Only the steps of the model (tests.md §3) may differ. */
    check(LEVEL_SCALING, off <= bands / 50,
          "%d Hz: %d of %d thresholds do not move by 6.02 dB", rates[r], off,
          bands);
    shine_close(quiet);
    shine_close(loud);
  }
}

/* KC 2: the partition ATH is positive and finite at every rate and row. */
static void check_ath_part(void) {
  for (int r = 0; r < 9; r++)
    for (int b = 0; b < 15; b++) {
      static const int bitrates[15] = {8,  16, 24,  32,  40,  48,  56, 64,
                                       80, 96, 112, 128, 160, 256, 320};
      shine_t e;

      if (shine_check_config(rates[r], bitrates[b]) < 0)
        continue;
      e = open_encoder(rates[r], 1, SHINE_MONO, bitrates[b]);
      for (int p = 0; p < e->psy->npart; p++)
        check(ATH_PART,
              e->psy->ath_part[p] > -200 * ONE &&
                  e->psy->ath_part[p] < 100 * ONE,
              "%d Hz %d kbps partition %d: ATH %.1f dB", rates[r], bitrates[b],
              p, db(e->psy->ath_part[p]));
      shine_close(e);
    }
}

/* ath.md §4.1: binary white noise at full scale has loudness
 * LOUDNESS_FS_NOISE, averaged over granules, and the loudness follows the
 * input level. */
static void check_loudness(void) {
  for (int r = 0; r < 9; r++) {
    noise_t loud = {32768, 1}, quiet = {3276.8, 1};
    shine_t a = open_encoder(rates[r], 1, SHINE_MONO, 64),
            b = open_encoder(rates[r], 1, SHINE_MONO, 64);
    long pa = 0, pb = 0;
    double sum = 0, ratio = 0;
    int granules = 0;

    for (int frame = 0; frame < 400; frame++) {
      encode_frame(a, noise, &loud, &pa, NULL);
      encode_frame(b, noise, &quiet, &pb, NULL);
      if (frame < 2)
        continue;
      for (int g = 0; g < a->mpeg.granules_per_frame; g++, granules++) {
        sum += linear(a->psy->loudness[g][0]);
        ratio += db(b->psy->loudness[g][0]) - db(a->psy->loudness[g][0]);
      }
    }
    check(LOUDNESS_REFERENCE, fabs(sum / granules - 3.0) < 0.15,
          "%d Hz: full-scale noise has loudness %.3f", rates[r],
          sum / granules);
    check(LOUDNESS_REFERENCE, fabs(ratio / granules + 20) < 0.05,
          "%d Hz: -20 dB moves loudness by %.3f dB", rates[r],
          ratio / granules);
    shine_close(a);
    shine_close(b);
  }
}

/* KC 18: a fade out then in. The factor falls gradually in the quiet part,
 * and is back to 1 one frame after a loud frame. */
typedef struct {
  long length;
  int samplerate;
} fade_t;

/* 1 kHz, loud in equal-loudness terms, -60 dB in the middle part. */
static double fade(long n, int ch, const void *context) {
  const fade_t *f = context;
  double gain = n < f->length ? 1 : n < 2 * f->length ? 0.001 : 1;

  (void)ch;
  return gain * 30000 * sin(2 * M_PI * 1000 * n / f->samplerate);
}

static void check_adaptive_fade(void) {
  for (int r = 0; r < 9; r += 3) {
    shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
    fade_t f = {60 * 1152, rates[r]};
    long length = f.length, position = 0;
    int64_t last = -1;
    int loud_frames = 0;

    while (position < 3 * length) {
      long start = position;

      encode_frame(e, fade, &f, &position, NULL);
      check_outputs(e->psy, e->mpeg.granules_per_frame, "fade");
      if (start >= length + 4 * 1152 && position < 2 * length) {
        int64_t f = e->psy->adjust_factor;

        /* At its floor, the factor follows the quiet loudness. */
        check(ADAPTIVE_ATH_FADE,
              last < 0 || ((f <= last || f < 2 * PSY_ADJUST_MIN) &&
                           f >= (last * 92) / 100),
              "%d Hz: factor %f after %f in the quiet part", rates[r],
              f / 2147483648.0, last / 2147483648.0);
        last = f;
      }
      if (start >= 2 * length + 1152 && ++loud_frames >= 2)
        check(ADAPTIVE_ATH_FADE, e->psy->adjust_factor == PSY_ADJUST_ONE,
              "%d Hz: factor %f after two loud frames", rates[r],
              e->psy->adjust_factor / 2147483648.0);
    }
    check(ADAPTIVE_ATH_FADE, last > 0 && last < PSY_ADJUST_ONE / 10,
          "%d Hz: the quiet part left the factor at %f", rates[r],
          last / 2147483648.0);
    shine_close(e);
  }
}

/* KC 18, 19: steady input at 0 dB and -20 dB gives the factor the curve
 * predicts from its loudness. */
static void check_adaptive_level(void) {
  for (int r = 0; r < 9; r += 2)
    for (int level = 0; level < 2; level++) {
      tone_t t = {1000, level ? 3276.7 : 32767, 0, rates[r]};
      shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
      long position = 0;
      double low = INFINITY, high = 0, factor;

      for (int frame = 0; frame < 300; frame++) {
        double p = 0;

        encode_frame(e, tone, &t, &position, NULL);
        for (int g = 0; g < e->mpeg.granules_per_frame; g++)
          p = fmax(p, linear(e->psy->loudness[g][0]));
        /* The update follows the loudness of the last frames. */
        if (frame >= 290) {
          double expected = p > 0.03125 ? 1 : 31.98 * p + 0.000625;
          low = fmin(low, expected);
          high = fmax(high, expected);
        }
      }
      factor = e->psy->adjust_factor / 2147483648.0;
      check(ADAPTIVE_ATH_LEVEL, factor >= low * 0.999 && factor <= high * 1.001,
            "%d Hz at %d dB: factor %f, the curve gives %f..%f", rates[r],
            level ? -20 : 0, factor, low, high);
      check(ADAPTIVE_ATH_LEVEL, level ? factor < 1 : factor == 1,
            "%d Hz at %d dB: factor %f", rates[r], level ? -20 : 0, factor);
      shine_close(e);
    }
}

/* KC 12: L = R leaves the side silent, with finite thresholds. */
static void check_side_zero(void) {
  for (int r = 0; r < 9; r++) {
    shine_t e =
        open_encoder(rates[r], 2, SHINE_JOINT_STEREO, lowest_bitrate(rates[r]));
    int mode = FAILURE_EQUAL;
    long position = 0;

    for (int frame = 0; frame < 4; frame++) {
      encode_frame(e, failure_mode, &mode, &position, NULL);
      for (int g = 0; g < e->mpeg.granules_per_frame; g++)
        for (int sb = 0; sb < 22; sb++)
          check(MS_SIDE_ZERO,
                e->psy->en[g][3][sb] == PSY_LOG_ZERO &&
                    fabs(db(e->psy->en[g][2][sb]) - db(e->psy->en[g][0][sb]) -
                         10 * log10(2)) < 0.01 &&
                    e->psy->thm[g][3][sb] == PSY_LOG_ZERO &&
                    e->psy->thm[g][2][sb] <= e->psy->en[g][2][sb],
                "%d Hz sfb %d: side energy %.1f dB, threshold %.1f dB",
                rates[r], sb, db(e->psy->en[g][3][sb]),
                db(e->psy->thm[g][3][sb]));
    }
    shine_close(e);
  }
}

/* KC 12: per partition, the M/S thresholds sum to at most 2·MSFIX times the
 * smaller L/R threshold, each floored at the ATH. */
static void check_ms_sum(void) {
  for (int r = 0; r < 9; r += 2) {
    shine_t e = open_encoder(rates[r], 2, SHINE_JOINT_STEREO,
                             128 > 0 && rates[r] < 32000 ? 64 : 128);
    psy_model_t *m = e->psy;
    double gain = 1;
    long position = 0;

    for (int frame = 0; frame < 10; frame++) {
      int32_t weight;

      encode_frame(e, music, &gain, &position, NULL);
      weight = ath_weight(m);
      for (int b = 0; b < m->npart; b++) {
        int32_t ath = ath_adjust(m, weight, m->ath_part[b]);
        double tlr = fmin(fmax(linear(m->parts[0].thr[b]), linear(ath)),
                          fmax(linear(m->parts[1].thr[b]), linear(ath)));
        double ms = linear(m->parts[2].thr[b]) + linear(m->parts[3].thr[b]);

        check(MS_SUM_LIMIT,
              ms <= tlr * linear(m->row->log2_two_msfix) * 1.001 &&
                  m->parts[2].thr[b] <= m->parts[2].eb[b] &&
                  m->parts[3].thr[b] <= m->parts[3].eb[b],
              "%d Hz partition %d: M + S thresholds %g, limit %g", rates[r], b,
              ms, tlr * linear(m->row->log2_two_msfix));
      }
    }
    shine_close(e);
  }
}

/* KC 12: demasking raises mid toward the side's energy lowered by mld only
 * when the L and R thresholds are within MS_SIMILARITY. */
static void check_ms_demasking(void) {
  shine_t e = open_encoder(44100, 2, SHINE_JOINT_STEREO, 128);
  psy_model_t *m = e->psy;
  const int b = 40;

  for (int apart = 0; apart < 2; apart++) {
    psy_partitions_t *parts = m->parts;

    memset(parts, 0, sizeof(m->parts));
    for (int p = 0; p < m->npart; p++)
      for (int c = 0; c < 4; c++) {
        parts[c].eb[p] = 0;
        parts[c].thr[p] = -20 * ONE;
      }
    parts[1].thr[b] = -20 * ONE + apart * 2 * ONE; /* 6 dB apart */
    parts[2].thr[b] = -60 * ONE;
    parts[3].thr[b] = -30 * ONE;
    ms_correction(m, parts, ONE);
    check(MS_DEMASKING,
          apart ? parts[2].thr[b] <= -60 * ONE : parts[2].thr[b] > -60 * ONE,
          "L and R %s: mid threshold %.1f dB from -180.6 dB",
          apart ? "6 dB apart" : "equal", db(parts[2].thr[b]));
  }
  shine_close(e);
}

/* ath.md §3: the weight w and athAdjust at its limits. */
static void check_ath_adjust(void) {
  shine_t e = open_encoder(44100, 1, SHINE_MONO, 128);
  psy_model_t *m = e->psy;
  int32_t floor = m->row->log2_ath_floor, x = floor + 20 * ONE;

  for (double a = 0.000625; a <= 1; a *= 1.7) {
    double expected = fmax(0, 1 + 20 * log10(a) / 90.30873362);

    m->adjust_factor = (int64_t)llrint(a * 2147483648.0);
    check(ATH_ADJUST, fabs(ath_weight(m) / 65536.0 - expected) < 0.001,
          "adjust_factor %f: weight %f, expected %f", a,
          ath_weight(m) / 65536.0, expected);
  }
  check(ATH_ADJUST, ath_adjust(m, ONE, x) == x + PSY_LOG2_ATH_ADAPT_GAIN,
        "w = 1 does not keep the curve");
  check(ATH_ADJUST, ath_adjust(m, 0, x) == floor + PSY_LOG2_ATH_ADAPT_GAIN,
        "w = 0 does not flatten the curve to its floor");
  shine_close(e);
}

/* KC 12: mono, stereo and dual-channel output does not depend on MSFIX. */
static void check_ms_independence(void) {
  static const int modes[3] = {SHINE_MONO, SHINE_STEREO, SHINE_DUAL_CHANNEL};

  for (int i = 0; i < 3; i++) {
    uint64_t hash[2] = {0, 0};

    for (int variant = 0; variant < 2; variant++) {
      shine_t e =
          open_encoder(44100, modes[i] == SHINE_MONO ? 1 : 2, modes[i], 128);
      psy_bitrate_row_t row = *e->psy->row;
      double gain = 1;
      long position = 0;

      row.log2_two_msfix += variant * 3 * ONE;
      e->psy->row = &row;
      for (int frame = 0; frame < 10; frame++)
        encode_frame(e, music, &gain, &position, &hash[variant]);
      shine_close(e);
    }
    check(MS_INDEPENDENCE, hash[0] == hash[1],
          "mode %d: the stream depends on MSFIX", modes[i]);
  }
}

/* KC 21: PE is continuous across the ratio cap. */
static void check_pe_cap(void) {
  int32_t en[22], thm[22];
  /* log2 of PE_RATIO_CAP = 10^10. */
  int32_t cap = (int32_t)lrint(10 / log10(2) * 65536);

  for (int sb = 0; sb < 22; sb++) {
    en[sb] = 0;
    thm[sb] = PSY_LOG_ZERO;
  }
  for (int sb = 0; sb < 21; sb++) {
    int32_t below, above, far;

    thm[sb] = -cap + ONE / 64;
    below = perceptual_entropy(en, thm);
    thm[sb] = -cap - 30 * ONE;
    far = perceptual_entropy(en, thm);
    thm[sb] = -cap - ONE / 64;
    above = perceptual_entropy(en, thm);
    thm[sb] = PSY_LOG_ZERO;
    check(PE_CAP_CONTINUITY,
          above >= below && above - below <= psy_pe_long[sb] / 100 + 1 &&
              far == above,
          "sfb %d: PE %d below the cap, %d above, %d far above", sb, below,
          above, far);
  }
}

/* signal-path §1: a click shows in the granule whose MDCT codes it, and no
 * earlier than the granule before. With the encoder delay D, granule g codes
 * the input samples [576 g - D, 576 g + 576 - D). */
static double click(long n, int ch, const void *context) {
  (void)ch;
  return n == *(const long *)context ? 32767 : 0;
}

static void check_click(void) {
  for (int r = 0; r < 9; r += 3)
    for (long n = 3000; n < 3000 + 576; n += 61) {
      shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
      long coded = (n + shine_encoder_delay(e)) / 576, granule = 0,
           position = 0;
      int ok = 1;

      while (granule <= coded + 1) {
        encode_frame(e, click, &n, &position, NULL);
        for (int g = 0; g < e->mpeg.granules_per_frame; g++, granule++) {
          int32_t energy = e->psy->energy[g][0];

          if (granule < coded - 1)
            ok &= energy == PSY_LOG_ZERO;
          if (granule == coded)
            ok &=
                energy != PSY_LOG_ZERO && e->psy->thm[g][0][10] != PSY_LOG_ZERO;
        }
      }
      check(CLICK_ALIGNMENT, ok, "%d Hz click at %ld, coded by granule %ld",
            rates[r], n, coded);
      shine_close(e);
    }
}

/* KC 9: with and without a leading granule of silence, the thresholds agree
 * from the third granule on. */
static double delayed_music(long n, int ch, const void *context) {
  double gain = 1;
  long delay = *(const long *)context;

  return n < delay ? 0 : music(n - delay, ch, &gain);
}

static void check_leading_silence(void) {
  for (int r = 0; r < 9; r += 4) {
    static int32_t thm[2][40][PSY_CHANNELS][22];
    int granules[2] = {0, 0};

    for (int variant = 0; variant < 2; variant++) {
      shine_t e = open_encoder(rates[r], 2, SHINE_JOINT_STEREO, 64);
      long delay = 576 * variant, position = 0;

      while (granules[variant] < 40) {
        encode_frame(e, delayed_music, &delay, &position, NULL);
        for (int g = 0; g < e->mpeg.granules_per_frame; g++)
          memcpy(thm[variant][granules[variant]++], e->psy->thm[g],
                 sizeof(thm[0][0]));
      }
      shine_close(e);
    }
    for (int g = 2; g + 1 < 40; g++)
      check(LEADING_SILENCE,
            !memcmp(thm[0][g], thm[1][g + 1], sizeof(thm[0][0])),
            "%d Hz granule %d differs with a leading granule of silence",
            rates[r], g);
  }
}

/* KC 5: after clipped full-scale input then silence, the state is the
 * state silence alone leads to. */
static void check_silence_state(void) {
  for (int r = 0; r < 9; r += 4) {
    shine_t a = open_encoder(rates[r], 2, SHINE_JOINT_STEREO, 64),
            b = open_encoder(rates[r], 2, SHINE_JOINT_STEREO, 64);
    int clipped = FAILURE_CLIPPED;
    long pa = 0, pb = 0;

    for (int frame = 0; frame < 50; frame++) {
      encode_frame(a, failure_mode, &clipped, &pa, NULL);
      encode_frame(b, silence, NULL, &pb, NULL);
    }
    for (int frame = 0; frame < 250; frame++) {
      encode_frame(a, silence, NULL, &pa, NULL);
      encode_frame(b, silence, NULL, &pb, NULL);
    }
    check(SILENCE_STATE,
          !memcmp(a->psy->channel, b->psy->channel, sizeof(a->psy->channel)) &&
              a->psy->adjust_factor == b->psy->adjust_factor &&
              a->psy->adjust_limit == b->psy->adjust_limit,
          "%d Hz: the state after clipping and silence is not the silence "
          "state",
          rates[r]);
    shine_close(a);
    shine_close(b);
  }
}

/* KC 8, 20: encoders interleaved give the streams they give alone. */
static void check_independent_encoders(void) {
  uint64_t alone[2] = {0, 0}, together[2] = {0, 0};
  double gain = 1;

  for (int pass = 0; pass < 2; pass++) {
    shine_t e[2];
    long position[2] = {0, 0};

    for (int i = 0; i < 2; i++)
      e[i] = i ? open_encoder(16000, 1, SHINE_MONO, 32)
               : open_encoder(44100, 2, SHINE_JOINT_STEREO, 128);
    for (int step = 0; step < 40; step++) {
      /* Alone: all of encoder 0, then all of encoder 1. */
      int i = pass ? step % 2 : step / 20;
      uint64_t *hash = pass ? &together[i] : &alone[i];
      const unsigned char *model = (const unsigned char *)e[i]->psy->thm;

      encode_frame(e[i], music, &gain, &position[i], hash);
      /* The model's outputs too: phase 1 streams hardly depend on them. */
      for (size_t k = 0; k < sizeof(e[i]->psy->thm); k++)
        *hash = (*hash ^ model[k]) * 0x100000001b3ULL;
    }
    for (int i = 0; i < 2; i++)
      shine_close(e[i]);
  }
  for (int i = 0; i < 2; i++)
    check(INDEPENDENT_ENCODERS, alone[i] == together[i],
          "encoder %d: interleaving changes its stream", i);
}

/* noise-allocation §5, by brute force over every coding: the chosen one
 * codes every scalefactor, with the fewest bits. */
static int coding_bits(int mpeg1, unsigned compress, const int32_t *sf,
                       int *codes) {
  /* Both formats send bands 0..5, 6..10, 11..15 and 16..20 with one slen
   * each: slen1, slen1, slen2, slen2 in MPEG-1. */
  static const int groups[5] = {0, 6, 11, 16, 21};
  int slen[4], bits = 0;

  if (mpeg1) {
    slen[0] = slen[1] = shine_slen1_tab[compress];
    slen[2] = slen[3] = shine_slen2_tab[compress];
  } else {
    slen[0] = (compress >> 4) / 5;
    slen[1] = (compress >> 4) % 5;
    slen[2] = (compress & 15) >> 2;
    slen[3] = compress & 3;
  }
  *codes = 1;
  for (int g = 0; g < 4; g++)
    for (int sb = groups[g]; sb < groups[g + 1]; sb++) {
      *codes &= sf[sb] < (1 << slen[g]);
      bits += slen[g];
    }
  return bits;
}

/* perceptual-entropy §3: k[i] / 5. */
static const double spec_taps[9] = {-0.0207887, -0.0378413, -0.0432472,
                                  -0.031183,  0,          0.0467745,
                                  0.10091,    0.151365,   0.187098};

/* The filter output F on the model's PE history. */
static double pe_filter(const psy_model_t *m) {
  double filtered = (double)m->pe_history[9];

  for (int i = 0; i < 9; i++)
    filtered +=
        5 * spec_taps[i] * (double)(m->pe_history[i] + m->pe_history[18 - i]);
  return filtered;
}

/* reservoir §3, in floating point, from the model's PE and the reservoir
 * the encoder had. */
static double granule_budget(shine_t e, int gr) {
  const psy_model_t *m = e->psy;
  const int channels = e->wave.channels, granules = e->mpeg.granules_per_frame;
  double mean = e->mean_bits, reservoir = m->reservoir[gr];
  double filtered = pe_filter(m), taps = 1, pe = 0, demand, bits;

  for (int i = 0; i < 9; i++)
    taps += 2 * 5 * spec_taps[i];
  for (int ch = 0; ch < channels; ch++)
    pe += m->pe[gr][(m->ms ? 2 : 0) + ch];
  /* The smoothed PE, over PE_STEADY per channel. */
  demand = filtered > 0 ? mean * (pe * 670 * 5 * granules * channels / filtered) /
                              (670 * 5 / taps * channels)
                        : mean;
  bits = mean + fmin(fmax(0, demand - mean), reservoir / 2);
  return fmin(bits, channels * 4095);
}

/* noise-allocation §4.1: whether step size q, with scalefactors s, leaves no
 * band over and uses floor_bits to ceiling_bits Huffman bits. */
static int under(shine_t e, int gr, int ch, const scalefactors_t *s, int q,
                 int floor_bits, int ceiling_bits) {
  int ix[GRANULE_SIZE], bits;
  int32_t noise[PSY_LONG_BANDS];
  gr_info counted = e->side_info.gr[gr].ch[ch].tt;

  if (quantize_granule(e, ix, q, s) > PSY_IX_MAX)
    return 0;
  bits = shine_count_bits(ix, &counted, e);
  if (bits < floor_bits || bits > ceiling_bits)
    return 0;
  band_noise(e, e->psy, ix, q, s, noise);
  for (int sb = 0; sb < e->psy->xmin_bands; sb++)
    if (noise[sb] > e->psy->xmin[gr][ch][sb] &&
        e->psy->xmin[gr][ch][sb] < e->psy->en[gr][e->psy->ms ? ch + 2 : ch][sb])
      return 0;
  return 1;
}

static void prepare(shine_t e, int gr, int ch);

/* noise-allocation §4.1: a channel leaves bits only with no band over, and
 * then as many as the reservoir can take. `reservoir` is what it held when
 * the channel was allocated. */
static void check_surplus(shine_t e, int gr, int ch, int max_bits,
                          int reservoir, const char *name) {
  const gr_info *gi = &e->side_info.gr[gr].ch[ch].tt;
  const int q = gi->quantizerStepSize, part2 = (int)gi->part2_length;
  gr_info counted = *gi;
  scalefactors_t s;
  int ix[GRANULE_SIZE], fitted_q, fitted_bits;
  int floor_bits;

  if (!e->l3loop.xrmax)
    return;
  s.scale = (int)gi->scalefac_scale;
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
    s.sf[sb] = (int)e->scalefactor.l[gr][ch][sb];
  fitted_bits = fit(e, ix, &counted, max_bits - part2, &s, &fitted_q);
  if (fitted_bits < 0)
    return;
  floor_bits = fitted_bits - (e->ResvMax - reservoir);
  if (fitted_bits <= floor_bits ||
      !under(e, gr, ch, &s, fitted_q, INT_MIN, fitted_bits)) {
    check(ALLOCATION_SURPLUS, q == fitted_q,
          "%s gr %d ch %d: step %d, the smallest that fits is %d, with %s",
          name, gr, ch, q, fitted_q,
          fitted_bits <= floor_bits ? "no room" : "a band over");
    return;
  }
  check(ALLOCATION_SURPLUS,
        q >= fitted_q &&
            (q == fitted_q ||
             under(e, gr, ch, &s, q, floor_bits, fitted_bits)) &&
            (q == MAX_STEPSIZE ||
             !under(e, gr, ch, &s, q + 1, floor_bits, fitted_bits)),
        "%s gr %d ch %d: step %d after %d, at least %d bits of %d", name, gr,
        ch, q, fitted_q, floor_bits, fitted_bits);
}

static void check_granule(shine_t e) {
  const int mpeg1 = e->mpeg.version == SHINE_MPEG_I;
  char name[64];

  snprintf(name, sizeof(name), "%d Hz %d kbps mode %d", e->wave.samplerate,
           e->mpeg.bitrate, e->mpeg.mode);
  for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++) {
    int bits = e->psy->granule_bits[gr], reservoir = e->psy->reservoir[gr];
    double expected = granule_budget(e, gr);

    check(GRANULE_BITS,
          bits >= 0 && bits <= e->wave.channels * 4095 &&
              bits <= e->mean_bits + reservoir && reservoir >= 0 &&
              reservoir <= e->ResvMax + 2 * e->mean_bits &&
              fabs(bits - expected) <= 1,
          "%s gr %d: %d bits with a mean of %d and a reservoir of %d of %d, "
          "expected %.1f",
          name, gr, bits, e->mean_bits, reservoir, e->ResvMax, expected);
  }
  for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++) {
    int reservoir = e->psy->reservoir[gr];

    for (int ch = 0; ch < e->wave.channels; ch++) {
      gr_info *gi = &e->side_info.gr[gr].ch[ch].tt;
      const int32_t *sf = e->scalefactor.l[gr][ch];
      int ix_max = 0, in_range = 1, codes, cheapest = -1;

      for (int k = 0; k < GRANULE_SIZE; k++)
        if (abs(e->l3_enc[ch][gr][k]) > ix_max)
          ix_max = abs(e->l3_enc[ch][gr][k]);
      /* part2_3_length also holds the frame's stuffing bits: count the
       * granule's own. */
      gr_info counted = *gi;
      int magnitudes[GRANULE_SIZE], bits;

      /* The bitstream writer signs ix in place. */
      for (int k = 0; k < GRANULE_SIZE; k++)
        magnitudes[k] = abs(e->l3_enc[ch][gr][k]);
      bits = (int)gi->part2_length + shine_count_bits(magnitudes, &counted, e);

      /* noise-allocation §3.1, in floating point. */
      const int32_t *pe = &e->psy->pe[gr][e->psy->ms ? 2 : 0];
      int total = e->psy->granule_bits[gr], max_bits;

      if (e->wave.channels == 1)
        max_bits = total > 4095 ? 4095 : total;
      else {
        int first = (int)floor((double)total * pe[0] / ((double)pe[0] + pe[1]));

        first = first > 4095 ? 4095 : first;
        first = total - first > 4095 ? total - 4095 : first;
        max_bits = ch ? total - first : first;
      }
      check(ALLOCATION_BOUNDS, ix_max <= PSY_IX_MAX && bits <= max_bits,
            "%s gr %d ch %d: ix up to %d, %d of %d bits", name, gr, ch, ix_max,
            bits, max_bits);
      prepare(e, gr, ch);
      check_surplus(e, gr, ch, max_bits, reservoir, name);
      /* What the encoder's reservoir became after this channel. */
      reservoir += e->mean_bits / e->wave.channels - (e->l3loop.xrmax ? bits : 0);

      /* noise-allocation §2: never over, so never amplified. */
      for (int sb = 0; sb < xmin_bands(e); sb++)
        if (e->psy->xmin[gr][ch][sb] >=
            e->psy->en[gr][e->psy->ms ? ch + 2 : ch][sb])
          check(ALLOCATION_TIE, sf[sb] == 0,
                "%s gr %d ch %d band %d: amplified by %d with its whole "
                "energy allowed",
                name, gr, ch, sb, sf[sb]);
      for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
        in_range &= sf[sb] >= 0 && sf[sb] <= (sb <= 10 ? 15 : 7) &&
                    (sb < xmin_bands(e) || sf[sb] == 0);
      for (unsigned c = 0; c < (mpeg1 ? 16u : 400u); c++) {
        int bits = coding_bits(mpeg1, c, sf, &codes);
        if (codes && (cheapest < 0 || bits < cheapest))
          cheapest = bits;
      }
      check(SCALEFACTOR_CODING,
            in_range &&
                coding_bits(mpeg1, gi->scalefac_compress, sf, &codes) ==
                    (int)gi->part2_length &&
                codes && (int)gi->part2_length == cheapest,
            "%s gr %d ch %d: scalefac_compress %u, %u bits, cheapest %d", name,
            gr, ch, gi->scalefac_compress, gi->part2_length, cheapest);
    }
  }
}

/* noise-allocation §1, §3, §5 on every rate, mono and joint stereo. */
static void check_allocation_bounds(void) {
  for (int r = 0; r < 9; r++)
    for (int stereo = 0; stereo < 2; stereo++)
      for (int b = 0; b < 2; b++) {
        int bitrate = b ? highest_bitrate(rates[r]) : lowest_bitrate(rates[r]);
        shine_t e =
            open_encoder(rates[r], stereo + 1,
                         stereo ? SHINE_JOINT_STEREO : SHINE_MONO, bitrate);
        double gain = 1;
        long position = 0;

        /* encode_frame checks every granule. */
        for (int frame = 0; frame < 6; frame++)
          encode_frame(e, music, &gain, &position, NULL);
        for (int mode = 0; mode < FAILURE_COUNT; mode++) {
          position = 0;
          for (int frame = 0; frame < 3; frame++)
            encode_frame(e, failure_mode, &mode, &position, NULL);
        }
        shine_close(e);
      }
}

/* D of the granule's current quantization, with the scalefactors s. */
static int64_t distortion(shine_t e, int gr, int ch, const scalefactors_t *s,
                          int q) {
  int32_t noise[PSY_LONG_BANDS];
  int64_t d = 0;

  band_noise(e, e->psy, e->l3_enc[ch][gr], q, s, noise);
  for (int sb = 0; sb < e->psy->xmin_bands; sb++)
    if (noise[sb] > e->psy->xmin[gr][ch][sb] &&
        e->psy->xmin[gr][ch][sb] < e->psy->en[gr][e->psy->ms ? ch + 2 : ch][sb])
      d += noise[sb] - e->psy->xmin[gr][ch][sb];
  return d;
}

/* Prepares granule gr, channel ch of the last frame as the iteration loop
 * does. */
static void prepare(shine_t e, int gr, int ch) {
  e->l3loop.xr = e->mdct_freq[ch][gr];
  e->l3loop.xrmax = 0;
  for (int k = 0; k < GRANULE_SIZE; k++) {
    e->l3loop.xrabs[k] = labs(e->l3loop.xr[k]);
    if (e->l3loop.xrabs[k] > e->l3loop.xrmax)
      e->l3loop.xrmax = e->l3loop.xrabs[k];
  }
}

/* noise-allocation §4: the chosen attempt is no worse than attempt 0, and a
 * granule with nothing over keeps its scalefactors at 0. */
static void check_allocation_best(void) {
  for (int r = 0; r < 9; r += 2)
    for (int b = 0; b < 2; b++) {
      int bitrate = b ? highest_bitrate(rates[r]) : lowest_bitrate(rates[r]);
      shine_t e = open_encoder(rates[r], 2, SHINE_JOINT_STEREO, bitrate);
      double gain = 1;
      long position = 0;
      int max_bits;
      for (int frame = 0; frame < 5; frame++) {
        encode_frame(e, music, &gain, &position, NULL);
        for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
          for (int ch = 0; ch < 2; ch++) {
            gr_info *gi = &e->side_info.gr[gr].ch[ch].tt;
            scalefactors_t zero, chosen;
            int q0, chosen_q;
            int64_t d0, d;

            prepare(e, gr, ch);
            memset(&zero, 0, sizeof(zero));
            max_bits = shine_psy_channel_bits(e, gr, ch);
            if (fit(e, e->l3_enc[ch][gr], gi, max_bits, &zero, &q0) < 0)
              continue;
            d0 = distortion(e, gr, ch, &zero, q0);

            shine_psy_allocate(e, e->l3_enc[ch][gr], gr, ch, max_bits);
            chosen.scale = (int)gi->scalefac_scale;
            for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
              chosen.sf[sb] = (int)e->scalefactor.l[gr][ch][sb];
            chosen_q = gi->quantizerStepSize;
            d = distortion(e, gr, ch, &chosen, chosen_q);
            check(ALLOCATION_BEST, d <= d0,
                  "%d Hz %d kbps gr %d ch %d: D %lld, attempt 0 %lld", rates[r],
                  bitrate, gr, ch, (long long)d, (long long)d0);

            /* Nothing over: every band is allowed full scale. */
            for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
              e->psy->xmin[gr][ch][sb] = 40 * ONE;
            shine_psy_allocate(e, e->l3_enc[ch][gr], gr, ch, max_bits);
            for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
              check(ALLOCATION_BEST, e->scalefactor.l[gr][ch][sb] == 0,
                    "%d Hz: band %d amplified with nothing over", rates[r], sb);
          }
      }
      shine_close(e);
    }
}

/* noise-allocation §1-§2: the noise of each band against the reconstruction
 * of the decoder's formula, in double precision. */
static void check_noise_measure(void) {
  for (int r = 0; r < 9; r += 4) {
    shine_t e = open_encoder(rates[r], 1, SHINE_MONO, 64);
    const int *sfb = shine_scale_fact_band_index[r];
    double gain = 1;
    long position = 0;

    for (int frame = 0; frame < 4; frame++)
      encode_frame(e, music, &gain, &position, NULL);
    for (int scale = 0; scale < 2; scale++)
      for (int q = -40; q <= 10; q += 7) {
        scalefactors_t s;
        int32_t noise[PSY_LONG_BANDS];

        memset(&s, 0, sizeof(s));
        s.scale = scale;
        for (int sb = 0; sb < PSY_LONG_BANDS - 1; sb++)
          s.sf[sb] = sb % 4;
        prepare(e, 0, 0);
        if (quantize_granule(e, e->l3_enc[0][0], q, &s) > PSY_IX_MAX)
          continue;
        band_noise(e, e->psy, e->l3_enc[0][0], q, &s, noise);
        for (int sb = 0; sb < e->psy->xmin_bands; sb++) {
          double step = q - 2 * (1 + scale) * s.sf[sb], sum = 0;

          for (int k = sfb[sb]; k < sfb[sb + 1]; k++) {
            double x = pow(e->l3_enc[0][0][k], 4.0 / 3) * pow(2, step / 4 + 31);
            sum += (e->l3loop.xrabs[k] - x) * (e->l3loop.xrabs[k] - x);
          }
          sum *= exp2(PSY_LOG2_K_LONG / 65536.0);
          if (sum > 0)
            check(NOISE_MEASURE, fabs(db(noise[sb]) - 10 * log10(sum)) < 0.01,
                  "%d Hz step %d band %d: noise %.3f dB, reconstruction "
                  "gives %.3f dB",
                  rates[r], (int)step, sb, db(noise[sb]), 10 * log10(sum));
        }
      }
    shine_close(e);
  }
}

/* allowed-distortion §2, recomputed from the model's outputs, on music and
 * on a tone in the top bands at 8 kHz, which have no xmin and so keep their
 * scalefactors at 0. */
static void check_xmin(shine_t e) {
  psy_model_t *m = e->psy;
  int32_t weight = ath_weight(m);

  for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
    for (int ch = 0; ch < e->wave.channels; ch++) {
      int c = m->ms ? ch + 2 : ch, ok = 1;

      for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
        int32_t en = m->en[gr][c][sb], x, expected;

        if (sb >= xmin_bands(e)) {
          ok &= m->xmin[gr][ch][sb] == PSY_LOG_ZERO &&
                e->scalefactor.l[gr][ch][sb] == 0;
          continue;
        }
        x = min32(en,
                  ath_adjust(m, weight, m->ath_band[sb]) + psy_band_offset[sb]);
        if (en != PSY_LOG_ZERO && m->thm[gr][c][sb] != PSY_LOG_ZERO)
          x = max32(x, m->thm[gr][c][sb] + psy_band_offset[sb]);
        expected = max32(x, PSY_LOG2_XMIN_FLOOR);
        ok &= m->xmin[gr][ch][sb] == expected &&
              m->xmin[gr][ch][sb] >= PSY_LOG2_XMIN_FLOOR;
      }
      check(ALLOWED_DISTORTION, ok, "%d Hz gr %d ch %d", e->wave.samplerate, gr,
            ch);
    }
}

static void check_allowed_distortion(void) {
  for (int r = 0; r < 9; r += 2) {
    shine_t e =
        open_encoder(rates[r], 2, SHINE_JOINT_STEREO, lowest_bitrate(rates[r]));
    double gain = 1;
    long position = 0;

    for (int frame = 0; frame < 6; frame++) {
      encode_frame(e, music, &gain, &position, NULL);
      check_xmin(e);
    }
    shine_close(e);
  }
  for (int b = 0; b < 2; b++) {
    tone_t t = {570.5 * 8000 / 1152.0, 20000, 1, 8000};
    shine_t e = open_encoder(8000, 1, SHINE_MONO, b ? 64 : 8);
    long position = 0;

    for (int frame = 0; frame < 8; frame++) {
      encode_frame(e, tone, &t, &position, NULL);
      check_xmin(e);
    }
    shine_close(e);
  }
}

/* noise-allocation §4: only the bands that are over are raised, and the
 * switch to scalefac_scale = 1 halves every scalefactor, rounded up. */
static void check_amplify(void) {
  scalefactors_t s;
  int over[PSY_LONG_BANDS] = {0}, raised, only_over = 1;

  memset(&s, 0, sizeof(s));
  over[3] = over[12] = 1;
  raised = amplify(&s, over, 21);
  for (int sb = 0; sb < 21; sb++)
    only_over &= s.sf[sb] == (over[sb] ? 1 : 0);
  check(ALLOCATION_BEST, raised == 2 && only_over && !s.scale,
        "raising bands 3 and 12 raised %d bands", raised);

  for (int sb = 0; sb < 21; sb++)
    s.sf[sb] = sb % 6;
  s.sf[12] = 7; /* at its largest */
  raised = amplify(&s, over, 21);
  only_over = s.scale == 1;
  for (int sb = 0; sb < 21; sb++) {
    int halved = ((sb == 12 ? 7 : sb % 6) + 1) / 2;
    only_over &= s.sf[sb] == halved + (over[sb] ? 1 : 0);
  }
  check(ALLOCATION_BEST, raised == 2 && only_over,
        "the switch to scalefac_scale 1 left the wrong scalefactors");
}

/* noise-allocation §4: silence has step size and scalefactors 0, and codes
 * nothing. part2_3_length may hold the frame's stuffing bits. */
static void check_allocation_silence(void) {
  for (int r = 0; r < 9; r += 3) {
    shine_t e = open_encoder(rates[r], 2, SHINE_JOINT_STEREO, 64);
    double gain = 1;
    long position = 0;

    /* After music, so that no field holds its initial 0 by chance. */
    for (int frame = 0; frame < 3; frame++)
      encode_frame(e, music, &gain, &position, NULL);
    for (int frame = 0; frame < 3; frame++) {
      encode_frame(e, silence, NULL, &position, NULL);
      for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
        for (int ch = 0; ch < 2; ch++) {
          gr_info *gi = &e->side_info.gr[gr].ch[ch].tt;
          int amplified = 0, silent = 1;

          /* The filterbank's tail of the music comes first. */
          for (int k = 0; k < GRANULE_SIZE; k++)
            silent &= e->mdct_freq[ch][gr][k] == 0;
          if (!silent)
            continue;
          for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
            amplified |= e->scalefactor.l[gr][ch][sb] != 0;
          check(ALLOCATION_SILENCE,
                gi->quantizerStepSize == 0 && !amplified &&
                    gi->global_gain == 210 + MDCT_HEADROOM_STEPS &&
                    gi->part2_length == 0 && gi->big_values == 0 &&
                    gi->count1 == 0,
                "%d Hz gr %d ch %d: step %d, global_gain %u", rates[r], gr, ch,
                gi->quantizerStepSize, gi->global_gain);
        }
    }
    shine_close(e);
  }
}

/* reservoir §3: a signal whose PE does not change demands the mean. */
static void check_reservoir_steady(void) {
  for (int r = 0; r < 9; r++) {
    shine_t e = open_encoder(rates[r], 2, SHINE_STEREO, r < 3 ? 128 : 64);
    /* Twelve periods a granule: every granule has the same spectrum. */
    tone_t t = {12 * rates[r] / 576.0, 8000, 1, rates[r]};
    long position = 0;

    for (int frame = 0; frame < 30; frame++) {
      encode_frame(e, tone, &t, &position, NULL);
      if (frame >= 22)
        for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
          check(RESERVOIR_STEADY,
                abs(e->psy->granule_bits[gr] - e->mean_bits) <= 1,
                "%d Hz frame %d gr %d: %d bits, mean %d", rates[r], frame, gr,
                e->psy->granule_bits[gr], e->mean_bits);
    }
    shine_close(e);
  }
}

/* A quiet tone, then full-scale noise from sample `start`. */
static double quiet_then_burst(long n, int ch, const void *context) {
  const tone_t *t = context;

  return n >= (long)t->right ? 30000 * noise_at(n, ch)
                             : 300 * sin(2 * M_PI * t->frequency * n /
                                         t->samplerate);
}

/* reservoir §3: a burst after a quiet passage gets more than the mean, from
 * what the quiet passage left; quiet granules after it give bits up while
 * the reservoir has room. */
static void check_reservoir_burst(void) {
  for (int r = 0; r < 9; r++) {
    shine_t e = open_encoder(rates[r], 2, SHINE_STEREO, r < 3 ? 128 : 64);
    const int per_pass = shine_samples_per_pass(e);
    tone_t t = {1000, 0, 30.0 * per_pass, rates[r]};
    tone_t quiet = {1000, 300, 1, rates[r]};
    long position = 0;
    int most = 0, least = e->mean_bits;

    for (int frame = 0; frame < 32; frame++) {
      encode_frame(e, quiet_then_burst, &t, &position, NULL);
      if (frame >= 30)
        for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
          if (e->psy->granule_bits[gr] > most)
            most = e->psy->granule_bits[gr];
    }
    check(RESERVOIR_BURST, most > e->mean_bits + e->mean_bits / 20,
          "%d Hz: the burst gets at most %d bits, mean %d", rates[r], most,
          e->mean_bits);
    for (int frame = 0; frame < 4; frame++) {
      encode_frame(e, tone, &quiet, &position, NULL);
      for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
        if (e->psy->granule_bits[gr] < least)
          least = e->psy->granule_bits[gr];
    }
    check(RESERVOIR_BURST, least < e->mean_bits,
          "%d Hz: quiet granules after the burst get at least %d bits, mean %d",
          rates[r], least, e->mean_bits);
    shine_close(e);
  }
}

/* reservoir §1, §4: the stream is each frame's header and side information
 * at its slot, and between them every frame's main data in order, then
 * zeros. */
static void check_stream_layout(void) {
  enum { FRAMES = 40, LIMIT = 1 << 17 };
  static unsigned char out[LIMIT], headers[LIMIT], main_data[LIMIT];
  static unsigned char laid[LIMIT];

  for (int r = 0; r < 9; r++)
    for (int mode = 0; mode < 2; mode++) {
      shine_t e = open_encoder(rates[r], mode ? 2 : 1,
                               mode ? SHINE_JOINT_STEREO : SHINE_MONO,
                               r < 3 ? 96 : 32);
      const int header_bytes = e->sideinfo_len / 8;
      int16_t samples[2 * SHINE_MAX_SAMPLES];
      int out_size = 0, main_size = 0, laid_size = 0, frames = 0, position = 0;
      int slot[FRAMES + 8], same;

      for (int frame = 0; frame <= FRAMES + 2; frame++) {
        unsigned char *data;
        int written, used = 0;

        if (frame == FRAMES + 2)
          data = shine_flush(e, &written);
        else {
          for (int i = 0; i < shine_samples_per_pass(e) * e->wave.channels; i++)
            samples[i] = frame % 7 >= 5 ? clip(30000 * noise_at(position++, 0))
                                        : (int16_t)(200 * (i % 23 - 11));
          data = shine_encode_buffer_interleaved(e, samples, &written);
          /* The frame as it was written, before its layout. */
          for (int gr = 0; gr < e->mpeg.granules_per_frame; gr++)
            for (int ch = 0; ch < e->wave.channels; ch++)
              used += (int)e->side_info.gr[gr].ch[ch].tt.part2_3_length;
          used = (used + e->side_info.resvDrain) / 8;
          memcpy(headers + frames * header_bytes, e->bs.data, header_bytes);
          memcpy(main_data + main_size, e->bs.data + header_bytes, used);
          main_size += used;
          slot[frames++] = e->mpeg.bits_per_frame / 8;
        }
        memcpy(out + out_size, data, written);
        out_size += written;
      }

      /* shine_flush encodes frames of its own: the stream holds at least
       * the frames recorded here. */
      position = 0;
      same = 1;
      for (int frame = 0; frame < frames && same; frame++) {
        same = position + slot[frame] <= out_size &&
               !memcmp(out + position, headers + frame * header_bytes,
                       header_bytes);
        if (same)
          memcpy(laid + laid_size, out + position + header_bytes,
                 slot[frame] - header_bytes);
        laid_size += slot[frame] - header_bytes;
        position += slot[frame];
      }
      check(STREAM_LAYOUT, same, "%d Hz mode %d: a header is not at its slot",
            rates[r], mode);
      /* The last frames' main data may end in the slots of the flush. */
      check(STREAM_LAYOUT,
            same && !memcmp(laid, main_data,
                            main_size < laid_size ? main_size : laid_size),
            "%d Hz mode %d: the main data is not laid out in order", rates[r],
            mode);
      check(STREAM_LAYOUT, main_size > laid_size - 2000 && main_size > 1000,
            "%d Hz mode %d: %d bytes of main data in %d of capacity",
            rates[r], mode, main_size, laid_size);
      shine_close(e);
    }
}

/* Silence on even frames, full-scale noise on odd ones. */
static double alternating(long n, int ch, const void *context) {
  int per_pass = *(const int *)context;

  return (n / per_pass) % 2 ? 32767 * (noise_at(n, ch) < 0 ? -1 : 1) : 0;
}

/* KC 21: full-scale noise at the lowest bitrate, silence, and the two
 * alternating every frame keep the filter output positive; check_granule
 * bounds every budget on the way. */
static void check_pe_smoothing(void) {
  for (int r = 0; r < 9; r++) {
    static const signal_t signals[3] = {noise, silence, alternating};
    noise_t z = {32767, 1};

    for (int s = 0; s < 3; s++) {
      shine_t e = open_encoder(rates[r], 2, SHINE_JOINT_STEREO,
                               lowest_bitrate(rates[r]));
      int per_pass = shine_samples_per_pass(e);
      const void *context = s == 0 ? (const void *)&z : (const void *)&per_pass;
      long position = 0;

      for (int frame = 0; frame < 45; frame++) {
        encode_frame(e, signals[s], context, &position, NULL);
        check(PE_SMOOTHING,
              e->psy->pe_filter > 0 && pe_filter(e->psy) > 0 &&
                  fabs(e->psy->pe_filter / 65536.0 / pe_filter(e->psy) - 1) <
                      1e-4,
              "%d Hz signal %d frame %d: F is %.1f, expected %.1f", rates[r],
              s, frame, e->psy->pe_filter / 65536.0, pe_filter(e->psy));
      }
      shine_close(e);
    }
  }
}

int main(void) {
  int status = 0, total = 0;

  check_tables();
  check_sine_energy();
  check_band_energy();
  check_sfb_edge();
  check_failure_modes();
  check_mask_add();
  check_tonality();
  check_tone_threshold();
  check_limit_order();
  check_preecho();
  check_spreading_rates();
  check_level_scaling();
  check_ath_part();
  check_loudness();
  check_adaptive_fade();
  check_adaptive_level();
  check_side_zero();
  check_ms_sum();
  check_ms_demasking();
  check_ath_adjust();
  check_ms_independence();
  check_pe_cap();
  check_click();
  check_leading_silence();
  check_silence_state();
  check_independent_encoders();
  check_allocation_bounds();
  check_allocation_best();
  check_allocation_silence();
  check_amplify();
  check_noise_measure();
  check_allowed_distortion();
  check_reservoir_steady();
  check_reservoir_burst();
  check_pe_smoothing();
  check_stream_layout();

  for (int i = 0; i < CHECK_COUNT; i++) {
    printf("%-30s %6d run, %d failed\n", check_names[i], runs[i], failures[i]);
    total += runs[i];
    if (failures[i] || !runs[i])
      status = 1;
  }
  printf("%d checks\n", total);
  return status;
}
