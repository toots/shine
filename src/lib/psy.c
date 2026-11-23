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
    /* §3.1: the second limit is dropped after a granule that is not NORM. */
    if (model->previous_type == BLOCK_NORM && channel->granules_seen >= 2 &&
        channel->nb_2[b] != ZERO)
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

/* §perceptual-entropy 3: k[i], Q16. The centre tap is 1. */
static const int32_t pe_taps[PSY_PE_HISTORY / 2] = {
    -6812, -12400, -14171, -10218, 0, 15327, 33066, 49599, 61308};
/* Their sum T, Q16. */
#define PE_TAPS_SUM (65536 + 2 * 115699)

/* Enters the frame's PE in use and filters the history. */
static void smooth_pe(psy_model_t *model, int64_t pe_frame) {
  int64_t *history = model->pe_history;
  int64_t filtered;

  memmove(history, history + 1, (PSY_PE_HISTORY - 1) * sizeof(*history));
  history[PSY_PE_HISTORY - 1] = pe_frame;
  filtered = history[PSY_PE_HISTORY / 2] << 16;
  for (int i = 0; i < PSY_PE_HISTORY / 2; i++)
    filtered += pe_taps[i] * (history[i] + history[PSY_PE_HISTORY - 1 - i]);
  model->pe_filter = filtered;
}

static int forced_short(const psy_model_t *model, long granule) {
  return model->forced_short_period && granule > 0 &&
         granule % model->forced_short_period == 0;
}

/* §1.1: the attack filter at input[n], Q16 of a sample. */
static int64_t attack_filter(const int16_t *input, int n) {
  int64_t sum = (int64_t)input[n] << 16;

  for (int j = 0; j < 5; j++)
    sum += (int64_t)psy_attack_taps[j] *
           (input[n - 9 + 2 * j] + input[n + 9 - 2 * j]);
  return sum;
}

/* §1.2: the nine sub-block peaks of the detector granule that starts at
 * input[start], for each analysis channel. */
static void sub_block_peaks(const psy_model_t *model, int channels, int start,
                            int64_t peaks[PSY_CHANNELS][9]) {
  for (int i = 0; i < 9; i++) {
    for (int c = 0; c < model->channels; c++)
      peaks[c][i] = PSY_PEAK_FLOOR;
    for (int n = start + 64 * i; n < start + 64 * (i + 1); n++) {
      int64_t h[PSY_CHANNELS];

      h[0] = attack_filter(model->input[0], n);
      h[1] = channels == 2 ? attack_filter(model->input[1], n) : 0;
      if (model->channels == 4) {
        /* (L ± R)/√2, of sums below 2^35. */
        h[2] = (((h[0] + h[1]) >> 4) * PSY_INV_SQRT2) >> 27;
        h[3] = (((h[0] - h[1]) >> 4) * PSY_INV_SQRT2) >> 27;
      }
      for (int c = 0; c < model->channels; c++) {
        int64_t magnitude = h[c] < 0 ? -h[c] : h[c];

        if (magnitude > peaks[c][i])
          peaks[c][i] = magnitude;
      }
    }
  }
}

/* §1.3 to §1.5 for analysis channel c: the slots of the granule, and whether
 * it needs short blocks. */
static int detect_attacks(psy_model_t *model, int c, const int64_t peaks[9],
                          int slots[4]) {
  const int64_t threshold =
      c == 3 ? model->row->attack_ratio_side : model->row->attack_ratio;
  int64_t sums[4];
  int needs_short;

  slots[0] = model->next_slot0[c];
  sums[0] = model->last_window_sum[c];
  for (int s = 1; s <= 3; s++) {
    sums[s] = 0;
    slots[s] = 0;
    for (int t = 0; t < 3; t++) {
      int i = 3 * (s - 1) + t;
      int64_t p = peaks[i];
      int64_t q = i >= 2 ? peaks[i - 2] : model->last_peaks[c][i];

      sums[s] += p;
      /* A rise, or a decay of more than ATTACK_DECAY, by more than the
       * threshold. Peaks are below 2^35 and the threshold below 2^24. */
      if (!slots[s] && ((p << 16) > threshold * q ||
                        (q << 16) > threshold * PSY_ATTACK_DECAY * p))
        slots[s] = t + 1;
    }
  }
  model->last_peaks[c][0] = peaks[7];
  model->last_peaks[c][1] = peaks[8];
  model->last_window_sum[c] = sums[3];
  model->next_slot0[c] = slots[3];

  /* §1.5 rule 1: periodic signals. */
  for (int s = 1; s <= 3; s++) {
    int64_t u = sums[s - 1], v = sums[s];

    if ((u > v ? u : v) < PSY_PERIODIC_CEILING_SUM &&
        (u << 16) < PSY_PERIODIC_RATIO * v &&
        (v << 16) < PSY_PERIODIC_RATIO * u) {
      if (s == 1 && slots[0] <= slots[1])
        slots[0] = 0;
      slots[s] = 0;
    }
  }
  /* Rule 2: already handled. */
  if (slots[0] <= model->last_attacks[c])
    slots[0] = 0;
  /* Rule 3: the decision, then adjacent attacks thinned. */
  needs_short = model->last_attacks[c] == 3 || slots[0] || slots[1] ||
                slots[2] || slots[3];
  if (needs_short)
    for (int s = 1; s <= 3; s++)
      if (slots[s] && slots[s - 1])
        slots[s] = 0;
  model->last_attacks[c] = slots[2];
  return needs_short;
}

/* §1.7, §2: whether the detector granule at input[start] makes its coded
 * granule short, on any analysis channel. Its slots go to attacks. */
static int detect_granule(psy_model_t *model, int channels, int start,
                          int attacks[PSY_CHANNELS][4]) {
  int64_t peaks[PSY_CHANNELS][9];
  int needs_short = 0;

  sub_block_peaks(model, channels, start, peaks);
  for (int c = 0; c < model->channels; c++)
    needs_short |= detect_attacks(model, c, peaks[c], attacks[c]);
  return needs_short;
}

int shine_psy_block_types(shine_global_config *config, int stride) {
  psy_model_t *model = config->psy;
  const int granules = config->mpeg.granules_per_frame;
  const int channels = config->wave.channels;
  const int samples = granules * GRANULE_SIZE;
  const int first = model->granule_index == 0 ? 0 : 1;

  for (int ch = 0; ch < channels; ch++) {
    int16_t *input = model->input[ch];

    memmove(input, input + samples, PSY_INPUT_HISTORY * sizeof(*input));
    for (int i = 0; i < samples; i++)
      input[PSY_INPUT_HISTORY + i] = config->buffer[ch][i * stride];
    config->buffer[ch] = input + PSY_INPUT_HISTORY - PSY_LOOKAHEAD;
  }

  /* The granule after the previous frame's last is this frame's first. */
  model->is_short[0] = model->is_short[granules];
  memcpy(model->attacks[0], model->attacks[granules],
         sizeof(model->attacks[0]));
  for (int g = first; g <= granules; g++)
    model->is_short[g] =
        detect_granule(model, channels,
                       PSY_INPUT_HISTORY - PSY_LOOKAHEAD - PSY_DETECTOR_OFFSET +
                           g * GRANULE_SIZE,
                       model->attacks[g]);
  if (model->forced_short_period)
    for (int g = 0; g <= granules; g++)
      model->is_short[g] = forced_short(model, model->granule_index + g);

  for (int g = 0; g < granules; g++) {
    /* §3: the type a granule ends with, knowing whether the next is short.
     * A granule between two short ones is short: a stop window cannot
     * precede a short one. */
    int before = model->was_short, after = model->is_short[g + 1];
    unsigned type = model->is_short[g] || (before && after) ? BLOCK_SHORT
                    : after                                 ? BLOCK_START
                    : before                                ? BLOCK_STOP
                                                            : BLOCK_NORM;

    /* §2: both channels have the same type. */
    for (int ch = 0; ch < channels; ch++)
      config->side_info.gr[g].ch[ch].tt.block_type = type;
    model->was_short = model->is_short[g];
  }
  model->granule_index += granules;
  return 1;
}

void shine_psy_frame(shine_global_config *config) {
  psy_model_t *model = config->psy;
  const int granules = config->mpeg.granules_per_frame;
  const int stereo = config->wave.channels == 2;
  int32_t frame_loudness[MAX_GRANULES];
  int32_t pe_lr = 0, pe_ms = 0, weight;
  int64_t pe_frame = 0;

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

    const unsigned type = config->side_info.gr[g].ch[0].tt.block_type;
    /* §spectral-analysis 1.1: the coded MDCT, or the long MDCT of a short
     * granule. */
    const int32_t (*spectrum)[MAX_GRANULES][GRANULE_SIZE] =
        type == BLOCK_SHORT ? model->analysis_long : config->mdct_freq;

    for (int k = 0; k < GRANULE_SIZE; k++) {
      x[0][k] = spectrum[0][g][k];
      if (stereo)
        x[1][k] = spectrum[1][g][k];
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
    model->previous_type = type;
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
    for (int ch = 0; ch < config->wave.channels; ch++) {
      allowed_distortion(model, weight, model->ms ? ch + 2 : ch,
                         model->xmin[g][ch], g);
      pe_frame += model->pe[g][model->ms ? ch + 2 : ch];
    }
  smooth_pe(model, pe_frame);
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
  for (int c = 0; c < PSY_CHANNELS; c++) {
    model->last_peaks[c][0] = model->last_peaks[c][1] = PSY_PEAK_FLOOR;
    model->last_window_sum[c] = 3 * PSY_PEAK_FLOOR;
  }
  for (int i = 0; i < PSY_PE_HISTORY; i++)
    model->pe_history[i] =
        (700LL << 8) * config->mpeg.granules_per_frame * config->wave.channels;
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
  /* A short block's global_gain is SHORT_HEADROOM_STEPS above its step. */
  int low = STEP_MIN - 1, bits;
  int high =
      MAX_STEPSIZE - (gi->block_type == BLOCK_SHORT ? SHORT_HEADROOM_STEPS : 0);

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

/* The largest part2_3_length, 12 bits. */
#define PART2_3_MAX 4095

/* §reservoir 3: a granule takes at most 1/2 of the reservoir. */
#define RESV_SPEND_SHIFT 1

void shine_psy_granule_bits(shine_global_config *config, int gr) {
  psy_model_t *model = config->psy;
  const int channels = config->wave.channels;
  const int *pe = &model->pe[gr][model->ms ? 2 : 0];
  const int64_t mean = config->mean_bits;
  const int reservoir = config->ResvSize;
  int64_t pe_granule = pe[0] + (channels == 2 ? pe[1] : 0);
  int64_t change = 0, limit;

  /* demand - mean, with demand = mean * T * mode_gr * pe / F. */
  if (model->pe_filter > 0)
    change = mean * PE_TAPS_SUM * config->mpeg.granules_per_frame * pe_granule /
                 model->pe_filter -
             mean;
  limit = reservoir >> RESV_SPEND_SHIFT;
  change = change < 0 ? 0 : change < limit ? change : limit;
  model->reservoir[gr] = reservoir;
  model->granule_bits[gr] = (int)(mean + change);
  if (model->granule_bits[gr] > channels * PART2_3_MAX)
    model->granule_bits[gr] = channels * PART2_3_MAX;
}

/* §3.1: the granule's bits, shared by perceptual entropy. */
int shine_psy_channel_bits(const shine_global_config *config, int gr, int ch) {
  const psy_model_t *model = config->psy;
  const int *pe = &model->pe[gr][model->ms ? 2 : 0];
  int granule_bits = model->granule_bits[gr], first;

  if (config->wave.channels == 1)
    return granule_bits;
  first = (int)((int64_t)granule_bits * pe[0] / ((int64_t)pe[0] + pe[1]));
  if (first > PART2_3_MAX)
    first = PART2_3_MAX;
  if (granule_bits - first > PART2_3_MAX)
    first = granule_bits - PART2_3_MAX;
  return ch ? granule_bits - first : first;
}

/* §4.1: whether the granule at step size q, with scalefactors s, has no
 * band over and uses floor_bits to ceiling_bits Huffman bits. Leaves it
 * quantised in ix and counted in gi. */
static int under_at(shine_global_config *config, int *ix, gr_info *gi, int q,
                    const scalefactors_t *s, const int32_t *xmin,
                    const int32_t *en, int floor_bits, int ceiling_bits,
                    int *bits) {
  const psy_model_t *model = config->psy;
  int32_t noise[PSY_LONG_BANDS];

  if (quantize_granule(config, ix, q, s) > PSY_IX_MAX)
    return 0;
  *bits = shine_count_bits(ix, gi, config);
  /* A larger step size does not always count fewer bits. */
  if (*bits < floor_bits || *bits > ceiling_bits)
    return 0;
  band_noise(config, model, ix, q, s, noise);
  for (int sb = 0; sb < model->xmin_bands; sb++)
    if (noise[sb] > xmin[sb] && xmin[sb] < en[sb])
      return 0;
  return 1;
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
  /* ponytail: a short block gets a plain step size search until the short
   * thresholds exist (doc/psy/masking.md §4). */
  if (gi->block_type == BLOCK_SHORT) {
    int bits = fit(config, ix, gi, max_bits, &s, &best_q);

    memset(config->scalefactor.s[gr][ch], 0,
           sizeof(config->scalefactor.s[gr][ch]));
    for (int sb = 0; sb < PSY_LONG_BANDS; sb++)
      config->scalefactor.l[gr][ch][sb] = 0;
    if (bits < 0) {
      memset(ix, 0, GRANULE_SIZE * sizeof(*ix));
      best_q = MAX_STEPSIZE - SHORT_HEADROOM_STEPS;
      bits = 0;
    }
    gi->quantizerStepSize = best_q;
    gi->scalefac_scale = gi->scalefac_compress = gi->part2_length = 0;
    return bits;
  }
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

  /* §4.1: with no band over, a larger step size that keeps it so, as far
   * as the reservoir has room for what it leaves. */
  if (best_distortion == 0 && config->ResvSize < config->ResvMax) {
    int floor_bits = best_bits - (config->ResvMax - config->ResvSize);
    int low = best_q, high = MAX_STEPSIZE + 1, bits;

    while (high - low > 1) {
      int middle = low + (high - low) / 2;

      if (under_at(config, ix, gi, middle, &best_s, xmin, en, floor_bits,
                   best_bits, &bits))
        low = middle;
      else
        high = middle;
    }
    if (low > best_q && under_at(config, ix, gi, low, &best_s, xmin, en,
                                 floor_bits, best_bits, &bits)) {
      best_q = low;
      best_bits = bits;
      best_gi = *gi;
      memcpy(best_ix, ix, sizeof(best_ix));
    }
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
