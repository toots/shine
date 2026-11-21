/* Psychoacoustic model, long blocks: doc/psy/ is the specification, and
 * section numbers below refer to it. Log-domain values are log2 in Q16. */

#include "psy.h"
#include "l3loop.h"
#include "layer3.h"
#include "tables.h"

#define ONE PSY_LOG_ONE
#define ZERO PSY_LOG_ZERO

/* Squared MDCT coefficients are below 2^62. Each granule and channel shifts
 * them by a common exponent so that its largest is below 2^52, and 576 of
 * them add up in 64 bits. */
#define ENERGY_BITS 52

/* Number of the highest set bit, x > 0. */
static int highest_bit(uint64_t x) {
  int n = 0;

  for (int step = 32; step; step >>= 1)
    if (x >> step) {
      x >>= step;
      n += step;
    }
  return n;
}

int32_t shine_psy_log2(uint64_t x) {
  int n;
  uint32_t mantissa, index, rest;

  if (!x)
    return ZERO;
  n = highest_bit(x);
  /* The bits below the highest one, as a 31-bit fraction. */
  mantissa = (uint32_t)(n > 31 ? x >> (n - 31) : x << (31 - n)) & 0x7fffffff;
  index = mantissa >> 22;
  rest = mantissa & 0x3fffff;
  return n * ONE + psy_log2_table[index] +
         (int32_t)(((int64_t)(psy_log2_table[index + 1] -
                              psy_log2_table[index]) *
                    rest) >>
                   22);
}

/* 2^x in Q30 for x <= 0, interpolated in psy_exp2_table. */
static uint32_t exp2_q30(int32_t x) {
  int32_t whole = -(x >> 16); /* x = -whole + fraction, 0 <= fraction < 1 */
  uint32_t fraction = (uint32_t)x & 0xffff, index = fraction >> 7,
           rest = fraction & 0x7f;
  uint32_t value;

  if (whole > 31)
    return 0;
  value =
      psy_exp2_table[index] + (uint32_t)(((uint64_t)(psy_exp2_table[index + 1] -
                                                     psy_exp2_table[index]) *
                                          rest) >>
                                         7);
  /* value is 2^fraction in Q30, at most 2^31. */
  return whole ? value >> whole : value;
}

int32_t shine_psy_log_add(int32_t a, int32_t b) {
  int32_t high = a > b ? a : b, low = a > b ? b : a;

  if (low == ZERO)
    return high;
  /* log2(1 + 2^(low - high)) + high; the sum is below 2^31. */
  return high + shine_psy_log2((1u << 30) + exp2_q30(low - high)) - 30 * ONE;
}

/* A product with a non-zero factor b. */
static int32_t scale(int32_t a, int32_t b) { return a == ZERO ? ZERO : a + b; }

static int32_t min32(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t max32(int32_t a, int32_t b) { return a > b ? a : b; }

/* §masking 2: the non-linear addition law. */
static int32_t mask_add(int32_t m1, int32_t m2, int distance, int delta) {
  int32_t ratio;

  if (m1 == ZERO)
    return m2;
  if (m2 == ZERO)
    return m1;
  ratio = m1 > m2 ? m1 - m2 : m2 - m1;
  if ((distance < 0 ? -distance : distance) <= delta) {
    if (ratio >= PSY_LOG2_ADD_NEAR_LIMIT)
      return shine_psy_log_add(m1, m2);
    /* int(16 log10(r)), 0..8 below the near limit. */
    return shine_psy_log_add(m1, m2) +
           psy_add_boost[((int64_t)ratio * PSY_ADD_INDEX_SCALE) >> 32];
  }
  if (ratio < PSY_LOG2_ADD_FAR_LIMIT)
    return shine_psy_log_add(m1, m2);
  return max32(m1, m2);
}

/* §masking 1, from the tonality spectrum: n·m - a and a·L stay below 2^63,
 * as partitions are under 256 lines and line energies under 2^53. */
static void tonality(const psy_model_t *model, const uint64_t *max_t,
                     const uint64_t *avg_t, psy_partitions_t *parts) {
  const int16_t *start = psy_part_start[model->rate];

  for (int b = 0; b < model->npart; b++) {
    int first = b > 0 ? b - 1 : b, last = b < model->npart - 1 ? b + 1 : b;
    uint64_t a = 0, m = 0, idx;
    int n = last - first + 1, lines = start[last + 1] - start[first] - 1;

    for (int p = first; p <= last; p++) {
      a += avg_t[p];
      if (max_t[p] > m)
        m = max_t[p];
    }
    if (!a || !lines) {
      parts->idx[b] = 0;
      continue;
    }
    idx = 20 * (n * m - a) / (a * lines);
    parts->idx[b] = idx > 8 ? 8 : (int)idx;
  }
}

static int energy_shift(uint64_t peak) {
  int bits = peak ? highest_bit(peak) + 1 : 0;

  return bits > ENERGY_BITS ? bits - ENERGY_BITS : 0;
}

/* §spectral-analysis 1, 3: line energies, partition sums and tonality.
 * Returns the log2 total energy. */
static int32_t analyse(psy_model_t *model, psy_channel_t *channel,
                       const int64_t *x, psy_partitions_t *parts) {
  const int16_t *start = psy_part_start[model->rate];
  uint64_t max_t[PSY_NPART_MAX], avg_t[PSY_NPART_MAX], peak = 0, total = 0;
  int shift, shift_t, last_shift = channel->last_shift;

  for (int k = 0; k < GRANULE_SIZE; k++)
    if ((uint64_t)(x[k] * x[k]) > peak)
      peak = (uint64_t)(x[k] * x[k]);
  shift = energy_shift(peak);
  if (!channel->granules_seen)
    last_shift = shift;
  /* §1.2, as a sum of the two granules at a common scale: the index is a
   * ratio. Each term is below 2^52. */
  shift_t = shift > last_shift ? shift : last_shift;
  parts->shift = shift;
  parts->unit = PSY_LOG2_K_LONG + shift * ONE;

  for (int b = 0; b < model->npart; b++) {
    uint64_t sum = 0, max = 0, sum_t = 0, peak_t = 0;

    for (int k = start[b]; k < start[b + 1]; k++) {
      uint64_t square = (uint64_t)(x[k] * x[k]), energy = square >> shift;
      uint64_t last = channel->granules_seen ? channel->last_energy[k] : energy;
      uint64_t energy_t =
          (square >> shift_t) + (last >> (shift_t - last_shift));

      channel->last_energy[k] = energy;
      sum += energy;
      if (energy > max)
        max = energy;
      sum_t += energy_t;
      if (energy_t > peak_t)
        peak_t = energy_t;
    }
    parts->eb_raw[b] = sum;
    parts->eb[b] = scale(shine_psy_log2(sum), parts->unit);
    parts->max[b] = scale(shine_psy_log2(max), parts->unit);
    max_t[b] = peak_t;
    avg_t[b] = sum_t / (uint64_t)(start[b + 1] - start[b]);
    total += sum;
  }
  channel->last_shift = shift;
  tonality(model, max_t, avg_t, parts);
  return scale(shine_psy_log2(total), parts->unit);
}

/* §masking 2-3: spreading, pre-echo limit, tonal ceiling, lowering. */
static void threshold(psy_model_t *model, psy_channel_t *channel,
                      psy_partitions_t *parts) {
  const int rate = model->rate;
  const int16_t *start = psy_part_start[rate];

  for (int b = 0; b < model->npart; b++) {
    const int lo = psy_spread_lo[rate][b], hi = psy_spread_hi[rate][b];
    const int32_t *spread = &psy_spread[rate][psy_spread_offset[rate][b]];
    const int delta = psy_delta[parts->idx[b]];
    int32_t ecb = ZERO, thr, avg_mask, lowering;
    int idx_sum = 0;

    for (int p = lo; p <= hi; p++) {
      int32_t term =
          scale(parts->eb[p], spread[p - lo] + psy_tab[parts->idx[p]]);

      ecb = p == lo ? term : mask_add(ecb, term, p - b, delta);
      idx_sum += parts->idx[p];
    }
    avg_mask = psy_tab[(1 + 2 * idx_sum) / (2 * (hi - lo + 1))] +
               PSY_LOG2_AVG_MASK_FACTOR;
    ecb = scale(ecb, avg_mask);
    parts->ecb[b] = ecb;

    /* §3.1: every granule is NORM before short blocks, so both limits. */
    thr = ecb;
    if (channel->granules_seen >= 1 && channel->nb_1[b] != ZERO)
      thr = min32(thr, channel->nb_1[b] + PSY_LOG2_PREECHO_RATIO_1);
    if (channel->granules_seen >= 2 && channel->nb_2[b] != ZERO)
      thr = min32(thr, channel->nb_2[b] + PSY_LOG2_PREECHO_RATIO_2);
    channel->nb_2[b] = channel->nb_1[b];
    channel->nb_1[b] = ecb;

    /* §3.3 */
    if (start[b + 1] - start[b] >= 2)
      thr = min32(thr, scale(parts->max[b], model->minval[b] + avg_mask));

    /* §3.4 */
    lowering = psy_part_masking_lower[rate][b] + model->row->log2_mask_adjust;
    if (lowering > 0)
      thr = scale(thr, lowering);
    thr = min32(thr, parts->eb[b]);
    if (lowering < 0)
      thr = scale(thr, lowering);
    parts->thr[b] = thr;
  }
  if (channel->granules_seen < 2)
    channel->granules_seen++;
}

/* §ath 3 */
static int32_t ath_adjust(const psy_model_t *model, int32_t weight,
                          int32_t ath) {
  int32_t floor = model->row->log2_ath_floor;

  return floor + (int32_t)(((int64_t)weight * (ath - floor)) >> 16) +
         PSY_LOG2_ATH_ADAPT_GAIN;
}

/* §stereo 2, per partition. */
static void ms_correction(const psy_model_t *model, psy_partitions_t *parts,
                          int32_t weight) {
  for (int b = 0; b < model->npart; b++) {
    const int32_t tl = parts[0].thr[b], tr = parts[1].thr[b],
                  tm = parts[2].thr[b], ts = parts[3].thr[b];
    const int32_t ebm = parts[2].eb[b], ebs = parts[3].eb[b];
    const int32_t mld = psy_part_mld[model->rate][b];
    const int32_t ath = ath_adjust(model, weight, model->ath_part[b]);
    int32_t rm = tm, rs = ts, tlr, m, s, limit, sum;

    if (tl <= scale(tr, PSY_LOG2_MS_SIMILARITY) &&
        tr <= scale(tl, PSY_LOG2_MS_SIMILARITY)) {
      rm = max32(tm, min32(ts, scale(ebs, mld)));
      rs = max32(ts, min32(tm, scale(ebm, mld)));
    }
    tlr = min32(max32(tl, ath), max32(tr, ath));
    m = max32(rm, ath);
    s = max32(rs, ath);
    limit = tlr + model->row->log2_two_msfix;
    sum = shine_psy_log_add(m, s);
    if (limit < sum) {
      m += limit - sum;
      s += limit - sum;
    }
    parts[2].thr[b] = min32(min32(m, rm), ebm);
    parts[3].thr[b] = min32(min32(s, rs), ebs);
  }
}

/* §spectral-analysis 5 */
static void band_sums(const psy_model_t *model, const psy_partitions_t *parts,
                      int32_t *en, int32_t *thm) {
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
    uint64_t energy = 0;
    int32_t threshold = ZERO;

    for (int b = model->band_part[sb]; b < model->band_part[sb + 1]; b++) {
      energy += parts->eb_raw[b];
      threshold = shine_psy_log_add(threshold, parts->thr[b]);
    }
    en[sb] = scale(shine_psy_log2(energy), parts->unit);
    /* Each partition's threshold is at most its energy; the log-domain sum
     * may round a few LSB above the exact one. */
    thm[sb] = min32(threshold, en[sb]);
  }
}

/* §perceptual-entropy 2, long formula, Q8. */
static int32_t perceptual_entropy(const int32_t *en, const int32_t *thm) {
  int32_t pe = PSY_PE_LONG_BASE;

  for (int sb = 0; sb < 21; sb++)
    if (thm[sb] != ZERO && en[sb] > thm[sb]) {
      int32_t ratio =
          (int32_t)(((int64_t)(en[sb] - thm[sb]) * PSY_LOG10_2) >> 16);

      if (ratio > PSY_PE_RATIO_CAP)
        ratio = PSY_PE_RATIO_CAP;
      pe += (int32_t)(((int64_t)psy_pe_long[sb] * ratio) >> 16);
    }
  return pe;
}

/* §ath 4.1: K_loud Σ E·eql_w. The energies are scaled to 30 bits by the
 * granule's peak, so the sum, with weights adding up to 2^32, fits 64 bits. */
static int32_t loudness(const psy_model_t *model, const int32_t *x) {
  uint64_t peak = 0, sum = 0;
  int shift;

  for (int k = 0; k < GRANULE_SIZE; k++) {
    uint64_t energy = (uint64_t)((int64_t)x[k] * x[k]);
    if (energy > peak)
      peak = energy;
  }
  if (!peak)
    return ZERO;
  shift = highest_bit(peak) >= 30 ? highest_bit(peak) - 29 : 0;
  for (int k = 0; k < GRANULE_SIZE; k++)
    sum +=
        ((uint64_t)((int64_t)x[k] * x[k]) >> shift) * model->loudness_weight[k];
  return scale(shine_psy_log2(sum),
               (shift - 32) * ONE + PSY_LOG2_K_LONG + PSY_LOG2_K_LOUD);
}

/* §ath 4.2, once per frame. */
static void adapt_ath(psy_model_t *model, const int32_t *frame_loudness,
                      int granules) {
  int32_t loudest = ZERO;
  int64_t factor = model->adjust_factor, limit = model->adjust_limit;

  for (int g = 0; g < granules; g++)
    loudest = max32(loudest, frame_loudness[g]);
  /* P = 0.5 max_g P_g; P > 0.03125 is log2 P > -5. */
  loudest = scale(loudest, -ONE);
  if (loudest > -5 * ONE) {
    if (factor >= PSY_ADJUST_ONE)
      factor = PSY_ADJUST_ONE;
    else if (factor < limit)
      factor = limit;
    limit = PSY_ADJUST_ONE;
  } else {
    /* P in Q31, below 2^26. */
    int64_t p = loudest == ZERO ? 0 : (int64_t)exp2_q30(loudest + ONE);
    int64_t level = ((PSY_ADJUST_SLOPE * p) >> 16) + PSY_ADJUST_MIN;

    if (factor >= level) {
      factor = (factor * (((PSY_ADJUST_RELEASE_SLOPE * level) >> 31) +
                          PSY_ADJUST_RELEASE_BASE)) >>
               31;
      if (factor < level)
        factor = level;
    } else if (limit >= level)
      factor = level;
    else if (factor < limit)
      factor = limit;
    limit = level;
  }
  model->adjust_factor = factor;
  model->adjust_limit = limit;
}

/* §ath 3: the exponent w of athAdjust, Q16. */
static int32_t ath_weight(const psy_model_t *model) {
  int32_t weight;

  if (!model->adapt_ath)
    return ONE;
  weight =
      ONE +
      (int32_t)(((int64_t)PSY_ATH_ADAPT_SLOPE *
                 (shine_psy_log2((uint64_t)model->adjust_factor) - 31 * ONE)) >>
                16);
  return weight > 0 ? weight : 0;
}

/* §allowed-distortion 2, for the coded channel whose analysis channel is c.
 * On the MDCT path the model's band energy is the coded one, so the masking
 * part is thm · fact. */
static void allowed_distortion(const psy_model_t *model, int32_t weight, int c,
                               int32_t *xmin, int g) {
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
    int32_t en = model->en[g][c][sb], x;

    if (sb >= model->xmin_bands) {
      xmin[sb] = ZERO;
      continue;
    }
    x = min32(en, ath_adjust(model, weight, model->ath_band[sb]) +
                      psy_band_offset[sb]);
    if (en != ZERO)
      x = max32(x, scale(model->thm[g][c][sb], psy_band_offset[sb]));
    xmin[sb] = max32(x, PSY_LOG2_XMIN_FLOOR);
  }
}

/* Mid and side coefficients, (L ± R)/√2: below 2^31 in magnitude, as the
 * MDCT keeps each channel's below 0.705 * 2^31. */
static int64_t mid_side(int64_t sum) {
  return (sum * PSY_INV_SQRT2 + (1LL << 30)) >> 31;
}

void shine_psy_frame(shine_global_config *config) {
  psy_model_t *model = config->psy;
  const int granules = config->mpeg.granules_per_frame;
  const int stereo = config->wave.channels == 2;
  int32_t frame_loudness[MAX_GRANULES];
  int32_t pe_lr = 0, pe_ms = 0, weight;

  /* §signal-path 1 step 3: loudness and the ATH update, before the model. */
  for (int g = 0; g < granules; g++) {
    for (int ch = 0; ch < config->wave.channels; ch++)
      model->loudness[g][ch] = loudness(model, config->mdct_freq[ch][g]);
    frame_loudness[g] =
        stereo ? shine_psy_log_add(model->loudness[g][0], model->loudness[g][1])
               : scale(model->loudness[g][0], ONE);
  }
  if (model->adapt_ath)
    adapt_ath(model, frame_loudness, granules);
  weight = ath_weight(model);

  for (int g = 0; g < granules; g++) {
    int64_t (*x)[GRANULE_SIZE] = model->coefficients;
    psy_partitions_t *parts = model->parts;

    for (int k = 0; k < GRANULE_SIZE; k++) {
      x[0][k] = config->mdct_freq[0][g][k];
      if (stereo)
        x[1][k] = config->mdct_freq[1][g][k];
      if (model->channels == 4) {
        x[2][k] = mid_side(x[0][k] + x[1][k]);
        x[3][k] = mid_side(x[0][k] - x[1][k]);
      }
    }
    for (int c = 0; c < model->channels; c++) {
      model->energy[g][c] = analyse(model, &model->channel[c], x[c], &parts[c]);
      threshold(model, &model->channel[c], &parts[c]);
    }
    if (model->channels == 4)
      ms_correction(model, parts, weight);
    for (int c = 0; c < model->channels; c++) {
      band_sums(model, &parts[c], model->en[g][c], model->thm[g][c]);
      model->pe[g][c] = perceptual_entropy(model->en[g][c], model->thm[g][c]);
    }
    if (model->channels == 4) {
      pe_lr += model->pe[g][0] + model->pe[g][1];
      pe_ms += model->pe[g][2] + model->pe[g][3];
    }
  }

  /* §stereo 3: ties go to M/S. */
  model->ms = model->channels == 4 && pe_ms <= pe_lr;
  config->mpeg.mode_ext = model->ms ? 2 : 0;
  if (model->ms)
    for (int g = 0; g < granules; g++)
      for (int k = 0; k < GRANULE_SIZE; k++) {
        int64_t l = config->mdct_freq[0][g][k], r = config->mdct_freq[1][g][k];

        config->mdct_freq[0][g][k] = (int32_t)mid_side(l + r);
        config->mdct_freq[1][g][k] = (int32_t)mid_side(l - r);
      }

  for (int g = 0; g < granules; g++)
    for (int ch = 0; ch < config->wave.channels; ch++)
      allowed_distortion(model, weight, model->ms ? ch + 2 : ch,
                         model->xmin[g][ch], g);
}

/* §ath 4.1: eql_w ∝ 10^(-ATH(f_k)/10), normalised to 2^32. */
static void loudness_weights(psy_model_t *model, const int32_t *ath_line) {
  int32_t loudest = ath_line[0];
  uint64_t total = 0, weight[GRANULE_SIZE];

  for (int k = 1; k < GRANULE_SIZE; k++)
    loudest = min32(loudest, ath_line[k]);
  for (int k = 0; k < GRANULE_SIZE; k++)
    total += weight[k] = exp2_q30(loudest - ath_line[k]);
  for (int k = 0; k < GRANULE_SIZE; k++)
    model->loudness_weight[k] = (uint32_t)((weight[k] << 32) / total);
}

int shine_psy_initialise(shine_global_config *config) {
  psy_model_t *model;
  const int rate = config->mpeg.samplerate_index;
  const int *sfb = shine_scale_fact_band_index[rate];
  const int16_t *start = psy_part_start[rate];
  int32_t ath_line[GRANULE_SIZE];
  int row = 0;

  config->psy = NULL;
  if (!config->quality)
    return 0;
  model = calloc(1, sizeof(*model));
  if (!model)
    return -1;

  model->rate = rate;
  model->npart = psy_npart[rate];
  model->channels =
      config->wave.channels == 2 && config->mpeg.mode == SHINE_JOINT_STEREO
          ? 4
          : config->wave.channels;
  model->adapt_ath = 1;
  model->adjust_factor = PSY_ADJUST_INITIAL;
  model->adjust_limit = PSY_ADJUST_ONE;

  /* Nearest table bitrate, ties to the higher one. */
  for (int i = 1; i < PSY_BITRATE_ROWS; i++)
    if (2 * config->mpeg.bitrate >=
        psy_bitrate_rows[i - 1].kbps + psy_bitrate_rows[i].kbps)
      row = i;
  model->row = &psy_bitrate_rows[row];

  for (int k = 0; k < GRANULE_SIZE; k++)
    ath_line[k] = psy_ath_line[rate][k] +
                  (int32_t)(((int64_t)psy_ath_line_curve[rate][k] *
                             model->row->ath_curve) >>
                            16);
  loudness_weights(model, ath_line);

  for (int b = 0, sb = 0; b < model->npart; b++) {
    int lines = start[b + 1] - start[b];
    /* §masking 3.3, long: x = 20 (bval/10 - 1) dB. */
    int32_t x = 2 * psy_part_bval[rate][b] - 20 * ONE;
    int32_t ath = ZERO;

    if (x > 6 * ONE)
      x = 30 * ONE;
    if (x < -model->row->minval_depth)
      x = -model->row->minval_depth;
    model->minval[b] =
        (int32_t)(((int64_t)(x - 8 * ONE) * PSY_DB_TO_LOG2) >> 16) +
        shine_psy_log2((uint64_t)lines);

    for (int k = start[b]; k < start[b + 1]; k++)
      if (ath == ZERO || ath_line[k] < ath)
        ath = ath_line[k];
    model->ath_part[b] = ath + model->row->log2_ath_offset;

    while (sfb[sb] < start[b])
      sb++;
    if (sfb[sb] == start[b])
      model->band_part[sb] = b;
  }
  model->band_part[PSY_LONG_BANDS] = model->npart;

  /* §ath 2, per band; §allowed-distortion 2: bands 0..20, 0..16 at 8 kHz. */
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
    int32_t ath = ath_line[sfb[sb]];

    for (int k = sfb[sb] + 1; k < sfb[sb + 1]; k++)
      ath = min32(ath, ath_line[k]);
    model->ath_band[sb] = ath + model->row->log2_ath_offset;
  }
  model->xmin_bands = config->wave.samplerate <= 8000 ? 17 : 21;

  config->psy = model;
  return 0;
}

/* Noise allocation (doc/psy/noise-allocation.md). */

/* Inner loops per granule: bounds the encoding time (parameters.md §2). */
#define OUTER_ATTEMPTS 8

/* The lowest step size the quantizer represents exactly. */
#define STEP_MIN -120

typedef struct {
  int sf[PSY_LONG_BANDS];
  int scale; /* scalefac_scale */
} scalefactors_t;

/* §5: both formats code up to 15 in bands 0..10 and 7 in bands 11..20. */
static int largest_scalefactor(int sb) { return sb <= 10 ? 15 : 7; }

static int bits_for(int value) {
  int bits = 0;

  for (; value; value >>= 1)
    bits++;
  return bits;
}

static int group_max(const int *sf, int first, int last) {
  int max = 0;

  for (int sb = first; sb <= last; sb++)
    if (sf[sb] > max)
      max = sf[sb];
  return max;
}

/* §5: the cheapest coding of the scalefactors. Returns part2_length. */
static int scalefactor_coding(const scalefactors_t *s, int mpeg1,
                              unsigned *compress) {
  if (mpeg1) {
    int need1 = bits_for(group_max(s->sf, 0, 10)),
        need2 = bits_for(group_max(s->sf, 11, 20)), best = -1;

    for (unsigned c = 0; c < 16; c++)
      if (shine_slen1_tab[c] >= need1 && shine_slen2_tab[c] >= need2) {
        int bits = 11 * shine_slen1_tab[c] + 10 * shine_slen2_tab[c];
        if (best < 0 || bits < best) {
          best = bits;
          *compress = c;
        }
      }
    return best;
  } else {
    int slen[4] = {
        bits_for(group_max(s->sf, 0, 5)), bits_for(group_max(s->sf, 6, 10)),
        bits_for(group_max(s->sf, 11, 15)), bits_for(group_max(s->sf, 16, 20))};

    *compress =
        (unsigned)(((5 * slen[0] + slen[1]) << 4) + (slen[2] << 2) + slen[3]);
    return 6 * slen[0] + 5 * (slen[1] + slen[2] + slen[3]);
  }
}

static int band_step(int q, const scalefactors_t *s, int sb) {
  return q - 2 * (1 + s->scale) * s->sf[sb];
}

/* §1: every band at its own step. Returns the largest ix, above IX_MAX when
 * a step is too small or below the exact range. */
static int quantize_granule(shine_global_config *config, int *ix, int q,
                            const scalefactors_t *s) {
  const int *sfb = shine_scale_fact_band_index[config->mpeg.samplerate_index];
  int max = 0;

  for (int sb = 0; sb < PSY_LONG_BANDS; sb++) {
    int step = band_step(q, s, sb), band_max;

    if (step < STEP_MIN)
      return PSY_IX_MAX + 1;
    band_max = shine_quantize_band(ix, sfb[sb], sfb[sb + 1], step, config);
    if (band_max > max)
      max = band_max;
  }
  return max;
}

/* §3: the smallest step size, found by bisection, whose granule fits the
 * budget. Returns the Huffman bits, or -1 if it does not fit even at the
 * largest step size. */
static int fit(shine_global_config *config, int *ix, gr_info *gi, int budget,
               const scalefactors_t *s, int *q) {
  int low = STEP_MIN - 1, high = MAX_STEPSIZE, bits;

  if (quantize_granule(config, ix, high, s) > PSY_IX_MAX ||
      shine_count_bits(ix, gi, config) > budget)
    return -1;
  while (high - low > 1) {
    int middle = low + (high - low) / 2;

    if (quantize_granule(config, ix, middle, s) <= PSY_IX_MAX &&
        shine_count_bits(ix, gi, config) <= budget)
      high = middle;
    else
      low = middle;
  }
  quantize_granule(config, ix, high, s);
  bits = shine_count_bits(ix, gi, config);
  *q = high;
  return bits;
}

/* x̂ = ix^(4/3) · 2^(step/4) in the coefficient scale of quantize: the
 * product of the Q14 power and the Q30 quarter step is scaled by
 * 2^(step/4 rounded down + 31 - 44). */
static uint64_t reconstruct(int ix, int step) {
  int whole = step >= 0 ? step / 4 : -((3 - step) / 4), shift;
  uint64_t product =
      (uint64_t)psy_pow43[ix] * psy_quarter_step[step - 4 * whole];

  shift = whole + 31 - 44;
  if (shift >= 0)
    return product << shift;
  return -shift < 64 ? (product + (1ULL << (-shift - 1))) >> -shift : 0;
}

/* §2: noise per band, log2 in the energy unit. The errors are below 2^32:
 * each band shifts them so that its largest is below 2^28, and its squares,
 * fewer than 256 of them, add up in 64 bits. */
static void band_noise(shine_global_config *config, const psy_model_t *model,
                       const int *ix, int q, const scalefactors_t *s,
                       int32_t *noise) {
  const int *sfb = shine_scale_fact_band_index[config->mpeg.samplerate_index];

  for (int sb = 0; sb < model->xmin_bands; sb++) {
    int step = band_step(q, s, sb), shift;
    uint64_t error[256], peak = 0, sum = 0;
    int lines = sfb[sb + 1] - sfb[sb];

    for (int k = 0; k < lines; k++) {
      int64_t difference = (int64_t)config->l3loop.xrabs[sfb[sb] + k] -
                           (int64_t)reconstruct(ix[sfb[sb] + k], step);

      error[k] = (uint64_t)(difference < 0 ? -difference : difference);
      if (error[k] > peak)
        peak = error[k];
    }
    shift = peak >> 28 ? highest_bit(peak) - 27 : 0;
    for (int k = 0; k < lines; k++)
      sum += (error[k] >> shift) * (error[k] >> shift);
    noise[sb] = scale(shine_psy_log2(sum), PSY_LOG2_K_LONG + 2 * shift * ONE);
  }
}

/* §4: raises every band that is over and can be amplified, switching to
 * scalefac_scale = 1 when one reaches its largest scalefactor. Returns the
 * number of bands raised. */
static int amplify(scalefactors_t *s, const int *over, int bands) {
  int raised = 0;

  if (!s->scale)
    for (int sb = 0; sb < bands; sb++)
      if (over[sb] && s->sf[sb] == largest_scalefactor(sb)) {
        s->scale = 1;
        for (int b = 0; b < bands; b++)
          s->sf[b] = (s->sf[b] + 1) / 2;
        break;
      }
  for (int sb = 0; sb < bands; sb++)
    if (over[sb] && s->sf[sb] < largest_scalefactor(sb)) {
      s->sf[sb]++;
      raised++;
    }
  return raised;
}

int shine_psy_allocate(shine_global_config *config, int ix[GRANULE_SIZE],
                       int gr, int ch, int max_bits) {
  const psy_model_t *model = config->psy;
  const int32_t *xmin = model->xmin[gr][ch];
  const int32_t *en = model->en[gr][model->ms ? ch + 2 : ch];
  const int bands = model->xmin_bands;
  gr_info *gi = &config->side_info.gr[gr].ch[ch].tt;
  scalefactors_t s, best_s;
  gr_info best_gi;
  int best_ix[GRANULE_SIZE], best_q, best_bits = 0, best_part2 = 0;
  unsigned best_compress = 0;
  int64_t best_distortion = -1;

  memset(&s, 0, sizeof(s));
  best_s = s;
  best_gi = *gi;
  /* Kept only if no attempt can be made: silence at the largest step. */
  memset(best_ix, 0, sizeof(best_ix));
  best_q = MAX_STEPSIZE;
  for (int attempt = 0; attempt < OUTER_ATTEMPTS; attempt++) {
    int32_t noise[PSY_LONG_BANDS];
    int over[PSY_LONG_BANDS], any_over = 0, all_amplified = 1, q, bits;
    unsigned compress;
    int part2 =
        scalefactor_coding(&s, config->mpeg.version == SHINE_MPEG_I, &compress);
    int64_t distortion = 0;

    if (part2 > max_bits)
      break;
    bits = fit(config, ix, gi, max_bits - part2, &s, &q);
    if (bits < 0)
      break;
    band_noise(config, model, ix, q, &s, noise);
    for (int sb = 0; sb < bands; sb++) {
      /* §2: a band allowed its whole energy is never over. */
      over[sb] = noise[sb] > xmin[sb] && xmin[sb] < en[sb];
      if (over[sb]) {
        distortion += noise[sb] - xmin[sb];
        any_over = 1;
      }
    }
    if (best_distortion < 0 || distortion < best_distortion) {
      best_distortion = distortion;
      best_s = s;
      best_q = q;
      best_bits = bits;
      best_part2 = part2;
      best_compress = compress;
      best_gi = *gi;
      memcpy(best_ix, ix, sizeof(best_ix));
    }
    if (!any_over || !amplify(&s, over, bands))
      break;
    for (int sb = 0; sb < bands; sb++)
      all_amplified &= s.sf[sb] > 0;
    if (all_amplified)
      break;
  }

  *gi = best_gi;
  memcpy(ix, best_ix, sizeof(best_ix));
  gi->quantizerStepSize = best_q;
  gi->scalefac_scale = (unsigned)best_s.scale;
  gi->scalefac_compress = best_compress;
  gi->part2_length = (unsigned)best_part2;
  for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
    config->scalefactor.l[gr][ch][sb] = best_s.sf[sb];
  return best_part2 + best_bits;
}
