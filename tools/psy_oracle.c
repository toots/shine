/* Float oracle of the psychoacoustic model (doc/psy/tests.md §3): the spec in
 * double precision, run on the encoder's own MDCT beside the fixed-point
 * model, from the same generated tables. A development aid while a phase is
 * built: it checks that the integer arithmetic implements the spec, not the
 * quality of the model. It covers the model (phase 1) and the allowed
 * distortion and noise allocation (phase 2), sharing the encoder's quantiser
 * and Huffman bit count. Delete it once phase 2 lands with its goldens.
 *
 * Usage: psy_oracle [file.wav ...]. With no argument, runs the synthetic
 * signals at every rate and channel mode. */

#include "l3loop.h"
#include "l3mdct.h"
#include "layer3.h"
#include "psy.h"
#include "reservoir.h"
#include "tables.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THRESHOLD_TOLERANCE_DB 0.5
#define PE_TOLERANCE 0.01
#define DECISION_AGREEMENT 0.99
#define NUMERIC_AGREEMENT 0.999
#define ALLOCATION_STRIDE 8

/* PSY_ORACLE_VERBOSE=1 prints the first disagreements of each signal. */
static int verbose;

static double from_log(int32_t l) {
  return l == PSY_LOG_ZERO ? 0 : exp2(l / 65536.0);
}

static double to_db(double x) { return 10 * log10(x); }

typedef struct {
  int granules_seen;
  double last_energy[GRANULE_SIZE];
  double nb_1[PSY_NPART_MAX], nb_2[PSY_NPART_MAX];
} oracle_channel_t;

typedef struct {
  int rate, npart, channels, kbps;
  const psy_bitrate_row_t *row;
  double ath_line[GRANULE_SIZE], eql_w[GRANULE_SIZE];
  double ath_part[PSY_NPART_MAX], minval[PSY_NPART_MAX];
  double adjust_factor, adjust_limit;
  oracle_channel_t channel[PSY_CHANNELS];
  /* Outputs. */
  double en[MAX_GRANULES][PSY_CHANNELS][PSY_LONG_BANDS];
  double thm[MAX_GRANULES][PSY_CHANNELS][PSY_LONG_BANDS];
  double pe[MAX_GRANULES][PSY_CHANNELS];
  int ms;
  /* perceptual-entropy.md §3: the PE in use of the last 19 frames, and F. */
  double pe_history[19], pe_filtered;
  double xmin[MAX_GRANULES][MAX_CHANNELS][PSY_LONG_BANDS];
} oracle_t;

static const double tab[9] = {1.0,     0.79433, 0.63096, 0.63096, 0.63096,
                              0.63096, 0.63096, 0.25119, 0.11749};
static const int delta[9] = {2, 2, 2, 1, 1, 1, 0, 0, -1};
static const double boost[9] = {1.33352, 1.35879, 1.38454, 1.39497, 1.40548,
                                1.3537,  1.30382, 1.22321, 1.14758};
static const double pe_long[21] = {6.8,  5.8,  5.8,  6.4,  6.5,  9.9,  12.1,
                                   14.4, 15.0, 18.9, 21.6, 26.9, 34.2, 40.2,
                                   46.8, 56.5, 60.7, 73.9, 85.7, 93.4, 126.1};

static void oracle_init(oracle_t *o, const psy_model_t *model) {
  const int16_t *start;
  double total = 0;

  memset(o, 0, sizeof(*o));
  o->rate = model->rate;
  o->npart = model->npart;
  o->channels = model->channels;
  o->row = model->row;
  o->kbps = model->row->kbps;
  start = psy_part_start[o->rate];
  o->adjust_factor = 0.01;
  o->adjust_limit = 1.0;

  for (int k = 0; k < GRANULE_SIZE; k++) {
    o->ath_line[k] =
        exp2((psy_ath_line[o->rate][k] +
              psy_ath_line_curve[o->rate][k] * (o->row->ath_curve / 65536.0)) /
             65536.0) *
        from_log(o->row->log2_ath_offset);
    total += 1 / o->ath_line[k];
  }
  for (int k = 0; k < GRANULE_SIZE; k++)
    o->eql_w[k] = 1 / o->ath_line[k] / total;

  for (int b = 0; b < o->npart; b++) {
    double bval = psy_part_bval[o->rate][b] / 65536.0;
    double depth = 5.0 * o->kbps / 320;
    double x = 20 * (bval / 10 - 1);
    int lines = start[b + 1] - start[b];

    if (x > 6)
      x = 30;
    if (x < -depth)
      x = -depth;
    o->minval[b] = pow(10, (x - 8) / 10) * lines;
    o->ath_part[b] = INFINITY;
    for (int k = start[b]; k < start[b + 1]; k++)
      if (o->ath_line[k] < o->ath_part[b])
        o->ath_part[b] = o->ath_line[k];
  }
}

static double mask_add(double m1, double m2, int distance, int d) {
  double r;

  if (m1 == 0)
    return m2;
  if (m2 == 0)
    return m1;
  r = fmax(m1, m2) / fmin(m1, m2);
  if (abs(distance) <= d) {
    if (r >= pow(10, 9.0 / 16))
      return m1 + m2;
    return (m1 + m2) * boost[(int)(16 * log10(r))] *
           boost[(int)(16 * log10(r))];
  }
  if (r < pow(10, 24.0 / 16))
    return m1 + m2;
  return fmax(m1, m2);
}

typedef struct {
  double eb[PSY_NPART_MAX], max[PSY_NPART_MAX], thr[PSY_NPART_MAX];
  int idx[PSY_NPART_MAX];
} oracle_parts_t;

static double athadjust(const oracle_t *o, double x) {
  double a = o->adjust_factor, floor = from_log(o->row->log2_ath_floor);
  double w = a > 0 ? fmax(0, 1 + 10 * log10(a * a) / 90.30873362) : 0;

  return floor * pow(x / floor, w) * pow(10, -4.51571501 / 10);
}

static void oracle_channel(oracle_t *o, oracle_channel_t *ch, const double *x,
                           oracle_parts_t *parts, double *en, double *thm) {
  const int16_t *start = psy_part_start[o->rate];
  const double k_long = exp2(PSY_LOG2_K_LONG / 65536.0);
  double e[GRANULE_SIZE], et[GRANULE_SIZE], max_t[PSY_NPART_MAX],
      avg_t[PSY_NPART_MAX];

  for (int k = 0; k < GRANULE_SIZE; k++) {
    e[k] = k_long * x[k] * x[k];
    et[k] = ch->granules_seen ? (e[k] + ch->last_energy[k]) / 2 : e[k];
    ch->last_energy[k] = e[k];
  }
  for (int b = 0; b < o->npart; b++) {
    double sum = 0, max = 0, sum_t = 0, peak_t = 0;
    for (int k = start[b]; k < start[b + 1]; k++) {
      sum += e[k];
      max = fmax(max, e[k]);
      sum_t += et[k];
      peak_t = fmax(peak_t, et[k]);
    }
    parts->eb[b] = sum;
    parts->max[b] = max;
    max_t[b] = peak_t;
    avg_t[b] = sum_t / (start[b + 1] - start[b]);
  }
  /* masking.md §1 */
  for (int b = 0; b < o->npart; b++) {
    int first = b > 0 ? b - 1 : b, last = b < o->npart - 1 ? b + 1 : b;
    int n = last - first + 1, lines = start[last + 1] - start[first] - 1;
    double a = 0, m = 0;

    for (int p = first; p <= last; p++) {
      a += avg_t[p];
      m = fmax(m, max_t[p]);
    }
    parts->idx[b] =
        a > 0 && lines > 0 ? (int)fmin(20 * (n * m - a) / (a * lines), 8) : 0;
  }
  /* masking.md §2-3 */
  for (int b = 0; b < o->npart; b++) {
    int lo = psy_spread_lo[o->rate][b], hi = psy_spread_hi[o->rate][b];
    const int32_t *spread = &psy_spread[o->rate][psy_spread_offset[o->rate][b]];
    double ecb = 0, thr, avg_mask, ml, lim = INFINITY;
    int idx_sum = 0;

    for (int p = lo; p <= hi; p++) {
      double term =
          from_log(spread[p - lo]) * parts->eb[p] * tab[parts->idx[p]];
      ecb = p == lo ? term : mask_add(ecb, term, p - b, delta[parts->idx[b]]);
      idx_sum += parts->idx[p];
    }
    avg_mask = tab[(1 + 2 * idx_sum) / (2 * (hi - lo + 1))] * 0.5;
    ecb *= avg_mask;

    if (ch->granules_seen >= 1 && ch->nb_1[b] > 0)
      lim = fmin(lim, 2 * ch->nb_1[b]);
    if (ch->granules_seen >= 2 && ch->nb_2[b] > 0)
      lim = fmin(lim, 16 * ch->nb_2[b]);
    thr = fmin(ecb, lim);
    ch->nb_2[b] = ch->nb_1[b];
    ch->nb_1[b] = ecb;

    if (start[b + 1] - start[b] >= 2)
      thr = fmin(thr, parts->max[b] * o->minval[b] * avg_mask);

    ml = from_log(psy_part_masking_lower[o->rate][b]) *
         from_log(o->row->log2_mask_adjust);
    if (ml > 1)
      thr *= ml;
    thr = fmin(thr, parts->eb[b]);
    if (ml < 1)
      thr *= ml;
    parts->thr[b] = thr;
  }
  if (ch->granules_seen < 2)
    ch->granules_seen++;
  (void)en;
  (void)thm;
}

static void band_sums(const oracle_t *o, const oracle_parts_t *parts,
                      const psy_model_t *model, double *en, double *thm) {
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
    en[sb] = thm[sb] = 0;
    for (int b = model->band_part[sb]; b < model->band_part[sb + 1]; b++) {
      en[sb] += parts->eb[b];
      thm[sb] += parts->thr[b];
    }
  }
  (void)o;
}

static double pe_formula(const double *en, const double *thm) {
  double pe = 1124.23 / 4;

  for (int sb = 0; sb < 21; sb++)
    if (thm[sb] > 0 && en[sb] > thm[sb])
      pe += pe_long[sb] * fmin(log10(en[sb] / thm[sb]), 10);
  return pe;
}

static void oracle_frame(oracle_t *o, const psy_model_t *model,
                         int32_t mdct[MAX_CHANNELS][MAX_GRANULES][GRANULE_SIZE],
                         int granules, int stereo) {
  const double k_long = exp2(PSY_LOG2_K_LONG / 65536.0);
  const double k_loud = exp2(PSY_LOG2_K_LOUD / 65536.0);
  double p = 0, pe_lr = 0, pe_ms = 0;

  /* ath.md §4 */
  for (int g = 0; g < granules; g++) {
    double loud[2] = {0, 0};
    for (int ch = 0; ch < (stereo ? 2 : 1); ch++)
      for (int k = 0; k < GRANULE_SIZE; k++)
        loud[ch] += k_loud * k_long * (double)mdct[ch][g][k] * mdct[ch][g][k] *
                    o->eql_w[k];
    p = fmax(p, loud[0] + (stereo ? loud[1] : loud[0]));
  }
  p *= 0.5;
  if (p > 0.03125) {
    if (o->adjust_factor >= 1)
      o->adjust_factor = 1;
    else if (o->adjust_factor < o->adjust_limit)
      o->adjust_factor = o->adjust_limit;
    o->adjust_limit = 1;
  } else {
    double level = 31.98 * p + 0.000625;
    if (o->adjust_factor >= level) {
      o->adjust_factor *= 0.075 * level + 0.925;
      if (o->adjust_factor < level)
        o->adjust_factor = level;
    } else if (o->adjust_limit >= level)
      o->adjust_factor = level;
    else if (o->adjust_factor < o->adjust_limit)
      o->adjust_factor = o->adjust_limit;
    o->adjust_limit = level;
  }

  for (int g = 0; g < granules; g++) {
    static double x[PSY_CHANNELS][GRANULE_SIZE];
    static oracle_parts_t parts[PSY_CHANNELS];

    for (int k = 0; k < GRANULE_SIZE; k++) {
      x[0][k] = mdct[0][g][k];
      x[1][k] = stereo ? mdct[1][g][k] : 0;
      /* The coefficients an M/S frame codes, rounded to integers. */
      x[2][k] = rint((x[0][k] + x[1][k]) / sqrt(2));
      x[3][k] = rint((x[0][k] - x[1][k]) / sqrt(2));
    }
    for (int c = 0; c < o->channels; c++)
      oracle_channel(o, &o->channel[c], x[c], &parts[c], NULL, NULL);

    /* stereo.md §2 */
    if (o->channels == 4)
      for (int b = 0; b < o->npart; b++) {
        double tl = parts[0].thr[b], tr = parts[1].thr[b], tm = parts[2].thr[b],
               ts = parts[3].thr[b], ebm = parts[2].eb[b], ebs = parts[3].eb[b];
        double mld = from_log(psy_part_mld[o->rate][b]);
        double ath = athadjust(o, o->ath_part[b]);
        double rm = tm, rs = ts, tlr, m, s,
               msfix2 = from_log(o->row->log2_two_msfix);

        if (tl <= 1.58 * tr && tr <= 1.58 * tl) {
          rm = fmax(tm, fmin(ts, mld * ebs));
          rs = fmax(ts, fmin(tm, mld * ebm));
        }
        tlr = fmin(fmax(tl, ath), fmax(tr, ath));
        m = fmax(rm, ath);
        s = fmax(rs, ath);
        if (tlr * msfix2 < m + s) {
          double f = tlr * msfix2 / (m + s);
          m *= f;
          s *= f;
        }
        parts[2].thr[b] = fmin(fmin(m, rm), ebm);
        parts[3].thr[b] = fmin(fmin(s, rs), ebs);
      }

    for (int c = 0; c < o->channels; c++) {
      band_sums(o, &parts[c], model, o->en[g][c], o->thm[g][c]);
      o->pe[g][c] = pe_formula(o->en[g][c], o->thm[g][c]);
    }
    if (o->channels == 4) {
      pe_lr += o->pe[g][0] + o->pe[g][1];
      pe_ms += o->pe[g][2] + o->pe[g][3];
    }
  }
  o->ms = o->channels == 4 && pe_ms <= pe_lr;

  /* perceptual-entropy.md §3 */
  {
    static const double k[9] = {-0.0207887, -0.0378413, -0.0432472,
                                -0.031183,  0,          0.0467745,
                                0.10091,    0.151365,   0.187098};
    double pe_sum = 0;

    for (int g = 0; g < granules; g++)
      for (int ch = 0; ch < (stereo ? 2 : 1); ch++)
        pe_sum += o->pe[g][o->ms ? ch + 2 : ch];
    memmove(o->pe_history, o->pe_history + 1, 18 * sizeof(double));
    o->pe_history[18] = pe_sum;
    o->pe_filtered = o->pe_history[9];
    for (int i = 0; i < 9; i++)
      o->pe_filtered += 5 * k[i] * (o->pe_history[i] + o->pe_history[18 - i]);
  }

  /* allowed-distortion.md §2 */
  for (int g = 0; g < granules; g++)
    for (int ch = 0; ch < (stereo ? 2 : 1); ch++) {
      int c = o->ms ? ch + 2 : ch;
      const int16_t *start = psy_part_start[o->rate];

      for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
        double fact = pow(10, (sb <= 6    ? -0.5
                               : sb <= 13 ? -0.25
                                          : -0.025) /
                                  10);
        double ath = INFINITY, en = o->en[g][c][sb], x;

        for (int b = model->band_part[sb]; b < model->band_part[sb + 1]; b++)
          for (int k = start[b]; k < start[b + 1]; k++)
            ath = fmin(ath, o->ath_line[k]);
        x = fmin(en, athadjust(o, ath) * fact);
        if (en > 0)
          x = fmax(x, o->thm[g][c][sb] * fact);
        o->xmin[g][ch][sb] = fmax(x, 1e-20);
      }
    }
}

typedef struct {
  long bands, band_failures, pes, pe_failures, frames, ms_frames,
      decision_failures;
  double worst_db, worst_pe;
  long xmins, xmin_failures, granules, allocation_failures, step_only;
  long budgets, budget_failures, surplus;
} tally_t;

static void compare(const oracle_t *o, const psy_model_t *model, int granules,
                    tally_t *tally, const char *name) {
  for (int g = 0; g < granules; g++)
    for (int c = 0; c < o->channels; c++) {
      double pe_fixed = model->pe[g][c] / 256.0,
             pe_error = fabs(pe_fixed - o->pe[g][c]) / o->pe[g][c];

      tally->pes++;
      if (pe_error > tally->worst_pe)
        tally->worst_pe = pe_error;
      if (pe_error > PE_TOLERANCE) {
        if (tally->pe_failures++ < 3 && verbose)
          printf("  %s: frame %ld gr %d ch %d: PE %.2f, oracle %.2f\n", name,
                 tally->frames, g, c, pe_fixed, o->pe[g][c]);
      }
      for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
        double ath = INFINITY, error;
        const int16_t *start = psy_part_start[o->rate];

        for (int b = model->band_part[sb]; b < model->band_part[sb + 1]; b++)
          for (int k = start[b]; k < start[b + 1]; k++)
            ath = fmin(ath, o->ath_line[k]);
        if (o->en[g][c][sb] <= athadjust(o, ath))
          continue;
        tally->bands++;
        error = fabs(to_db(from_log(model->thm[g][c][sb])) -
                     to_db(o->thm[g][c][sb]));
        if (!(error <= tally->worst_db))
          tally->worst_db = error;
        if (!(error <= THRESHOLD_TOLERANCE_DB) && tally->band_failures++ < 3 &&
            verbose)
          printf("  %s: frame %ld gr %d ch %d sb %d: threshold %.2f dB, "
                 "oracle %.2f dB\n",
                 name, tally->frames, g, c, sb,
                 to_db(from_log(model->thm[g][c][sb])),
                 to_db(o->thm[g][c][sb]));
      }
    }
  tally->frames++;
  tally->ms_frames += o->ms;
  if (o->ms != model->ms)
    tally->decision_failures++;
}

/* noise-allocation.md, in double precision. */
#define STEP_MIN -120
#define OUTER_ATTEMPTS 8
#define IX_MAX 8192

typedef struct {
  int sf[PSY_LONG_BANDS], scale;
} oracle_scalefactors_t;

/* §1, with the encoder's quantiser, as the bits are counted by its Huffman
 * tables: returns 0 if a step is out of range or an ix above IX_MAX. */
static int oracle_quantize(shine_t encoder, int *ix, int q,
                           const oracle_scalefactors_t *s, const int *sfb) {
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
    int step = q - 2 * (1 + s->scale) * s->sf[sb];

    if (step < STEP_MIN ||
        shine_quantize_band(ix, sfb[sb], sfb[sb + 1], step, encoder) > IX_MAX)
      return 0;
  }
  return 1;
}

/* §5: the fewest bits that code every scalefactor. */
static int oracle_part2(const oracle_scalefactors_t *s, int mpeg1) {
  static const int groups[5] = {0, 6, 11, 16, 21};
  int need[4], best = -1;

  for (int g = 0; g < 4; g++) {
    int max = 0;

    for (int sb = groups[g]; sb < groups[g + 1]; sb++)
      max = s->sf[sb] > max ? s->sf[sb] : max;
    for (need[g] = 0; max >> need[g]; need[g]++)
      ;
  }
  if (!mpeg1)
    return 6 * need[0] + 5 * (need[1] + need[2] + need[3]);
  for (int c = 0; c < 16; c++) {
    int slen1 = shine_slen1_tab[c], slen2 = shine_slen2_tab[c];

    if (slen1 >= need[0] && slen1 >= need[1] && slen2 >= need[2] &&
        slen2 >= need[3] && (best < 0 || 11 * slen1 + 10 * slen2 < best))
      best = 11 * slen1 + 10 * slen2;
  }
  return best;
}

/* §3-§4 on one granule. Returns 0 if no attempt could be made. */
/* noise-allocation.md §4.1: whether step size q leaves no band over and
 * uses floor_bits to ceiling_bits Huffman bits. */
static int oracle_under(shine_t encoder, const double *xr, const double *en,
                        const double *xmin, int bands,
                        const oracle_scalefactors_t *s, int q, int floor_bits,
                        int ceiling_bits) {
  const int *sfb = shine_scale_fact_band_index[encoder->mpeg.samplerate_index];
  const double k_long = exp2(PSY_LOG2_K_LONG / 65536.0);
  int ix[GRANULE_SIZE], bits;
  gr_info gi;

  memset(&gi, 0, sizeof(gi));
  if (q > MAX_STEPSIZE || !oracle_quantize(encoder, ix, q, s, sfb))
    return 0;
  bits = shine_count_bits(ix, &gi, encoder);
  if (bits < floor_bits || bits > ceiling_bits)
    return 0;
  for (int sb = 0; sb < bands; sb++) {
    double step = q - 2 * (1 + s->scale) * s->sf[sb], noise = 0;

    for (int k = sfb[sb]; k < sfb[sb + 1]; k++) {
      double e =
          fabs(xr[k]) - pow(ix[k], 4.0 / 3) * pow(2, step / 4) * 2147483648.0;
      noise += e * e;
    }
    if (noise * k_long > xmin[sb] && xmin[sb] < en[sb])
      return 0;
  }
  return 1;
}

/* Returns 0 if no attempt could be made, 2 if the best attempt leaves no
 * band over, 1 otherwise; *best_bits is its Huffman bits. */
static int oracle_allocate(shine_t encoder, const double *xr, const double *en,
                           const double *xmin, int bands, int max_bits,
                           oracle_scalefactors_t *best_s, int *best_q,
                           int *best_bits) {
  const int *sfb = shine_scale_fact_band_index[encoder->mpeg.samplerate_index];
  const int mpeg1 = encoder->mpeg.version == SHINE_MPEG_I;
  const double k_long = exp2(PSY_LOG2_K_LONG / 65536.0);
  oracle_scalefactors_t s;
  double best_d = -1;
  int ix[GRANULE_SIZE];

  memset(&s, 0, sizeof(s));
  for (int attempt = 0; attempt < OUTER_ATTEMPTS; attempt++) {
    int part2 = oracle_part2(&s, mpeg1), q, over[PSY_LONG_BANDS], any = 0,
        raised = 0, all = 1, bits = 0;
    double d = 0;
    gr_info gi;

    if (part2 > max_bits)
      break;
    /* The smallest step that fits, by exhaustion. */
    for (q = STEP_MIN; q <= MAX_STEPSIZE; q++) {
      memset(&gi, 0, sizeof(gi));
      if (oracle_quantize(encoder, ix, q, &s, sfb) &&
          (bits = shine_count_bits(ix, &gi, encoder)) <= max_bits - part2)
        break;
    }
    if (q > MAX_STEPSIZE)
      break;
    for (int sb = 0; sb < bands; sb++) {
      double step = q - 2 * (1 + s.scale) * s.sf[sb], noise = 0;

      for (int k = sfb[sb]; k < sfb[sb + 1]; k++) {
        double e =
            fabs(xr[k]) - pow(ix[k], 4.0 / 3) * pow(2, step / 4) * 2147483648.0;
        noise += e * e;
      }
      noise *= k_long;
      over[sb] = noise > xmin[sb] && xmin[sb] < en[sb];
      if (over[sb]) {
        d += log2(noise / xmin[sb]);
        any = 1;
      }
    }
    if (best_d < 0 || d < best_d) {
      best_d = d;
      *best_s = s;
      *best_q = q;
      *best_bits = bits;
    }
    if (!any)
      break;
    if (!s.scale)
      for (int sb = 0; sb < bands; sb++)
        if (over[sb] && s.sf[sb] == (sb <= 10 ? 15 : 7)) {
          s.scale = 1;
          for (int b = 0; b < bands; b++)
            s.sf[b] = (s.sf[b] + 1) / 2;
          break;
        }
    for (int sb = 0; sb < bands; sb++)
      if (over[sb] && s.sf[sb] < (sb <= 10 ? 15 : 7)) {
        s.sf[sb]++;
        raised++;
      }
    if (!raised)
      break;
    for (int sb = 0; sb < bands; sb++)
      all &= s.sf[sb] > 0;
    if (all)
      break;
  }
  return best_d < 0 ? 0 : best_d == 0 ? 2 : 1;
}

/* The allowed distortion and the allocation of the frame the encoder just
 * ran, against the oracle's. */
static void compare_allocation(const oracle_t *o, shine_t encoder,
                               tally_t *tally, const char *name) {
  const psy_model_t *model = encoder->psy;
  const int bands = encoder->wave.samplerate <= 8000 ? 17 : 21;

  for (int g = 0; g < encoder->mpeg.granules_per_frame; g++)
    for (int ch = 0, room = 0; ch < encoder->wave.channels; ch++) {
      const gr_info *gi = &encoder->side_info.gr[g].ch[ch].tt;
      double xr[GRANULE_SIZE], peak = 0;
      const double *en = o->en[g][o->ms ? ch + 2 : ch];
      oracle_scalefactors_t s;
      int q, same = 1, max_bits, outcome, fitted_bits = 0, step_agrees;
      int encoder_q = gi->quantizerStepSize;

      /* reservoir.md §3, from the reservoir the encoder had. */
      if (ch == 0) {
        const int nch = encoder->wave.channels;
        double mean = encoder->mean_bits;
        double reservoir = encoder->psy->reservoir[g];
        double pe = o->pe[g][o->ms ? 2 : 0] +
                    (nch == 2 ? o->pe[g][o->ms ? 3 : 1] : 0);
        double smoothed = o->pe_filtered > 0
                              ? pe * 670 * 5 * encoder->mpeg.granules_per_frame *
                                    nch / o->pe_filtered
                              : 739.3719 * nch;
        double demand = mean * smoothed / (739.3719 * nch), bits;

        bits = mean + fmin(fmax(0, demand - mean), 0.5 * reservoir);
        bits = fmin(bits, nch * 4095);
        room = encoder->ResvMax - encoder->psy->reservoir[g];
        tally->budgets++;
        /* The PE of the two models agree within PE_TOLERANCE. */
        if (fabs(bits - encoder->psy->granule_bits[g]) >
                2 + 2 * PE_TOLERANCE * mean &&
            tally->budget_failures++ < 3 && verbose)
          printf("  %s: gr %d: %d bits, oracle %.1f (mean %.0f, reservoir "
                 "%.0f)\n",
                 name, g, encoder->psy->granule_bits[g], bits, mean,
                 reservoir);
      }

      /* noise-allocation.md §3.1, on the granule's bits. */
      {
        int base = o->ms ? 2 : 0;
        int total = encoder->psy->granule_bits[g];
        int first = (int)floor(total * o->pe[g][base] /
                               (o->pe[g][base] + o->pe[g][base + 1]));

        if (encoder->wave.channels == 1)
          max_bits = total > 4095 ? 4095 : total;
        else {
          first = first > 4095 ? 4095 : first;
          first = total - first > 4095 ? total - 4095 : first;
          max_bits = ch ? total - first : first;
        }
      }

      for (int sb = 0; sb < bands; sb++) {
        double error = fabs(to_db(from_log(model->xmin[g][ch][sb])) -
                            to_db(o->xmin[g][ch][sb]));

        tally->xmins++;
        if (!(error <= THRESHOLD_TOLERANCE_DB) && tally->xmin_failures++ < 3 &&
            verbose)
          printf("  %s: gr %d ch %d sb %d: xmin %.2f dB, oracle %.2f dB\n",
                 name, g, ch, sb, to_db(from_log(model->xmin[g][ch][sb])),
                 to_db(o->xmin[g][ch][sb]));
      }
      /* The quantiser reads the granule from the loop's state. */
      encoder->l3loop.xr = encoder->mdct_freq[ch][g];
      encoder->l3loop.xrmax = 0;
      for (int k = 0; k < GRANULE_SIZE; k++) {
        xr[k] = encoder->mdct_freq[ch][g][k];
        peak = fmax(peak, fabs(xr[k]));
        encoder->l3loop.xrabs[k] = labs(encoder->mdct_freq[ch][g][k]);
        if (encoder->l3loop.xrabs[k] > encoder->l3loop.xrmax)
          encoder->l3loop.xrmax = encoder->l3loop.xrabs[k];
      }
      outcome = peak == 0 ? 0
                          : oracle_allocate(encoder, xr, en, o->xmin[g][ch],
                                            bands, max_bits, &s, &q,
                                            &fitted_bits);
      /* What the encoder's reservoir can take after this channel. */
      {
        int channel_room = room, used = 0;

        if (peak != 0) {
          int magnitudes[GRANULE_SIZE];
          gr_info counted = *gi;

          for (int k = 0; k < GRANULE_SIZE; k++)
            magnitudes[k] = abs(encoder->l3_enc[ch][g][k]);
          used = (int)gi->part2_length +
                 shine_count_bits(magnitudes, &counted, encoder);
        }
        room -= encoder->mean_bits / encoder->wave.channels - used;
        if (!outcome)
          continue;
        /* noise-allocation.md §4.1: with no band over and room, a step
         * size that leaves none over and whose successor does not. */
        int low = fitted_bits - channel_room;

        if (outcome == 2 && channel_room > 0) {

          tally->surplus += encoder_q > q;
          step_agrees =
              encoder_q >= q &&
              (encoder_q == q ||
               oracle_under(encoder, xr, en, o->xmin[g][ch], bands, &s,
                            encoder_q, low, fitted_bits)) &&
              (encoder_q == MAX_STEPSIZE ||
               !oracle_under(encoder, xr, en, o->xmin[g][ch], bands, &s,
                             encoder_q + 1, low, fitted_bits));
        } else
          step_agrees = encoder_q == q;
      }
      tally->granules++;
      same = s.scale == (int)gi->scalefac_scale;
      for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
        same &= s.sf[sb] == encoder->scalefactor.l[g][ch][sb];
      if (same && !step_agrees)
        tally->step_only++;
      if (!same || !step_agrees) {
        if (tally->allocation_failures++ < 3 && verbose)
          printf("  %s: gr %d ch %d: step %d scale %u, oracle step %d scale "
                 "%d%s\n",
                 name, g, ch, gi->quantizerStepSize, gi->scalefac_scale, q,
                 s.scale, same ? "" : ", scalefactors differ");
      }
    }
}

/* Encodes `count` interleaved samples, comparing every frame. */
static void run(const shine_config_t *config, const int16_t *samples,
                long count, tally_t *tally, const char *name) {
  shine_config_t copy = *config;
  shine_t encoder = shine_initialise(&copy);
  static oracle_t oracle;
  static int32_t mdct[MAX_CHANNELS][MAX_GRANULES][GRANULE_SIZE];
  int channels = config->wave.channels, per_pass;
  long frame = 0;

  if (!encoder || !encoder->psy) {
    printf("FAIL %s: no model\n", name);
    exit(1);
  }
  per_pass = shine_samples_per_pass(encoder);
  oracle_init(&oracle, encoder->psy);
  for (int i = 0; i < 19; i++)
    oracle.pe_history[i] =
        700.0 * encoder->mpeg.granules_per_frame * config->wave.channels;
  for (long i = 0; i + per_pass <= count; i += per_pass) {
    encoder->buffer[0] = samples + i * channels;
    if (channels == 2)
      encoder->buffer[1] = samples + i * channels + 1;
    shine_mdct_sub(encoder, channels);
    memcpy(mdct, encoder->mdct_freq, sizeof(mdct));
    shine_psy_frame(encoder);
    oracle_frame(&oracle, encoder->psy, mdct, encoder->mpeg.granules_per_frame,
                 channels == 2);
    compare(&oracle, encoder->psy, encoder->mpeg.granules_per_frame, tally,
            name);
    /* The allocation has no state: one frame in ALLOCATION_STRIDE. */
    if (frame++ % ALLOCATION_STRIDE == 0) {
      int frame_bits = 8 * encoder->mpeg.whole_slots_per_frame;
      /* reservoir.md §2, with the stream's longest frame. */
      int longest = frame_bits + 8 * (encoder->mpeg.frac_slots_per_frame != 0);
      int limit = 8 * (encoder->mpeg.version == SHINE_MPEG_I ? 511 : 255);

      limit = limit < 7680 - longest ? limit : 7680 - longest;
      encoder->ResvMax = limit > 0 ? limit : 0;
      encoder->mean_bits = (frame_bits - encoder->sideinfo_len) /
                           encoder->mpeg.granules_per_frame;
      shine_iteration_loop(encoder);
      compare_allocation(&oracle, encoder, tally, name);
    }
  }
  shine_close(encoder);
}

static int16_t clamp16(double v) {
  return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrint(v);
}

/* Synthetic signals, `channels` interleaved. */
static void synthesize(int signal, int16_t *out, long count, int channels,
                       int samplerate) {
  unsigned seed = 1;

  for (long i = 0; i < count; i++) {
    double t = (double)i / samplerate, l = 0, r;
    double noise =
        ((seed = seed * 1103515245 + 12345) >> 8 & 0xffff) / 32768.0 - 1;

    switch (signal) {
    case 0: /* tone, full scale */
      l = 32767 * sin(2 * M_PI * 997 * t);
      break;
    case 1: /* tone, -40 dB */
      l = 327 * sin(2 * M_PI * 997 * t);
      break;
    case 2: /* white noise */
      l = 32767 * noise;
      break;
    case 3: /* sweep */
      l = 20000 * sin(2 * M_PI * (50 + 0.45 * samplerate * t / 4) * t);
      break;
    case 4: /* bursts */
      l = i % 1000 < 64 ? 32767 * noise : 0;
      break;
    case 5: /* two tones, fading in */
      l = (i * 4.0 / count) * 3000 *
          (sin(2 * M_PI * 440 * t) + sin(2 * M_PI * 3100 * t));
      break;
    case 6: /* silence */
      l = 0;
      break;
    }
    /* Right channel: correlated, opposite, silent, independent. */
    switch (signal % 4) {
    case 0:
      r = l;
      break;
    case 1:
      r = -l;
      break;
    case 2:
      r = 0.3 * l + 8000 * sin(2 * M_PI * 5003 * t);
      break;
    default:
      r = 0;
      break;
    }
    out[i * channels] = clamp16(l);
    if (channels == 2)
      out[i * channels + 1] = clamp16(r);
  }
}

static int report(const tally_t *t) {
  int ok = t->bands > 0 &&
           t->band_failures <= (1 - NUMERIC_AGREEMENT) * t->bands &&
           t->pe_failures <= (1 - NUMERIC_AGREEMENT) * t->pes &&
           t->decision_failures <= (1 - DECISION_AGREEMENT) * t->frames;

  printf("%ld bands, %ld outside %.1f dB (worst %.3f dB); %ld PE, %ld outside "
         "1%% (worst %.4f%%); %ld frames, %ld M/S, %ld decisions differ\n",
         t->bands, t->band_failures, THRESHOLD_TOLERANCE_DB, t->worst_db,
         t->pes, t->pe_failures, 100 * t->worst_pe, t->frames, t->ms_frames,
         t->decision_failures);
  printf("%ld xmin, %ld outside %.1f dB; %ld granules allocated, %ld differ "
         "(%ld in the step size only)\n",
         t->xmins, t->xmin_failures, THRESHOLD_TOLERANCE_DB, t->granules,
         t->allocation_failures, t->step_only);
  printf("%ld granule budgets, %ld differ; %ld channels left bits\n",
         t->budgets, t->budget_failures, t->surplus);
  ok = ok && t->granules > 0 && t->budgets > 0 && t->surplus > 0 &&
       t->budget_failures <= (1 - NUMERIC_AGREEMENT) * t->budgets &&
       t->xmin_failures <= (1 - NUMERIC_AGREEMENT) * t->xmins &&
       t->allocation_failures <= (1 - DECISION_AGREEMENT) * t->granules;
  if (!t->bands)
    printf("FAIL: no band was compared\n");
  return ok;
}

static int synthetic(void) {
  static const enum shine_mode modes[] = {SHINE_MONO, SHINE_STEREO,
                                          SHINE_JOINT_STEREO};
  static const int bitrates[] = {8, 32, 64, 128, 320};
  tally_t tally = {0};
  int ok = 1;

  for (int r = 0; r < 9; r++)
    for (size_t m = 0; m < sizeof(modes) / sizeof(*modes); m++)
      for (size_t b = 0; b < sizeof(bitrates) / sizeof(*bitrates); b++) {
        int samplerate = shine_samplerates[r];
        int bitrate = bitrates[b];
        shine_config_t config;
        long count = 40 * 1152;
        int16_t *samples;

        if (shine_check_config(samplerate, bitrate) < 0)
          continue;
        shine_set_config_mpeg_defaults(&config.mpeg);
        config.wave.samplerate = samplerate;
        config.wave.channels = modes[m] == SHINE_MONO ? 1 : 2;
        config.mpeg.mode = modes[m];
        config.mpeg.bitrate = bitrate;
        config.mpeg.quality = 1;
        samples = malloc(count * 2 * sizeof(int16_t));
        for (int s = 0; s < 7; s++) {
          char name[64];
          tally_t one = {0};

          snprintf(name, sizeof(name), "%d Hz %d kbps mode %d signal %d",
                   samplerate, bitrate, modes[m], s);
          synthesize(s, samples, count, config.wave.channels, samplerate);
          run(&config, samples, count, &one, name);
          /* Every synthetic decision agrees. */
          if (one.decision_failures) {
            printf("FAIL %s: %ld M/S decisions differ\n", name,
                   one.decision_failures);
            ok = 0;
          }
          tally.bands += one.bands;
          tally.band_failures += one.band_failures;
          tally.pes += one.pes;
          tally.pe_failures += one.pe_failures;
          tally.frames += one.frames;
          tally.ms_frames += one.ms_frames;
          tally.decision_failures += one.decision_failures;
          tally.xmins += one.xmins;
          tally.xmin_failures += one.xmin_failures;
          tally.granules += one.granules;
          tally.allocation_failures += one.allocation_failures;
          tally.step_only += one.step_only;
          tally.budgets += one.budgets;
          tally.budget_failures += one.budget_failures;
          tally.surplus += one.surplus;
          tally.worst_db = fmax(tally.worst_db, one.worst_db);
          tally.worst_pe = fmax(tally.worst_pe, one.worst_pe);
        }
        free(samples);
      }
  return report(&tally) && ok;
}

/* 16-bit PCM WAV, read naively: the data chunk is found by its tag. */
static int corpus(int count, char **paths) {
  tally_t tally = {0};

  for (int i = 0; i < count; i++) {
    FILE *file = fopen(paths[i], "rb");
    unsigned char header[12], chunk[8];
    int channels = 0, samplerate = 0;
    long size = 0;
    int16_t *samples;
    shine_config_t config;

    if (!file || fread(header, 1, 12, file) != 12) {
      printf("FAIL: cannot read %s\n", paths[i]);
      return 0;
    }
    while (fread(chunk, 1, 8, file) == 8) {
      long length =
          chunk[4] | chunk[5] << 8 | chunk[6] << 16 | (long)chunk[7] << 24;
      if (!memcmp(chunk, "fmt ", 4)) {
        unsigned char fmt[16];
        if (fread(fmt, 1, 16, file) != 16)
          break;
        channels = fmt[2];
        samplerate = fmt[4] | fmt[5] << 8 | fmt[6] << 16;
        fseek(file, length - 16, SEEK_CUR);
      } else if (!memcmp(chunk, "data", 4)) {
        size = length / 2;
        break;
      } else
        fseek(file, length, SEEK_CUR);
    }
    samples = malloc(size * sizeof(int16_t));
    size = (long)fread(samples, sizeof(int16_t), size, file);
    fclose(file);
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.wave.samplerate = samplerate;
    config.wave.channels = channels;
    config.mpeg.mode = channels == 2 ? SHINE_JOINT_STEREO : SHINE_MONO;
    config.mpeg.bitrate = 128;
    config.mpeg.quality = 1;
    if (shine_check_config(samplerate, 128) < 0) {
      printf("FAIL %s: unsupported samplerate %d\n", paths[i], samplerate);
      return 0;
    }
    run(&config, samples, size / channels, &tally, paths[i]);
    free(samples);
  }
  return report(&tally);
}

int main(int argc, char **argv) {
  int ok;

  verbose = getenv("PSY_ORACLE_VERBOSE") != NULL;
  ok = argc > 1 ? corpus(argc - 1, argv + 1) : synthetic();

  if (!ok)
    printf("FAIL\n");
  return ok ? 0 : 1;
}
