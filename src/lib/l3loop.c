/* l3loop.c */

#include "l3loop.h"
#include "bitstream.h"
#include "huffman.h"
#include "l3bitstream.h"
#include "layer3.h"
#include "reservoir.h"
#include "tables.h"
#include "types.h"

#include <limits.h>

#define SFB_LMAX 22

static int part2_length(int gr, int ch, shine_global_config *config);
static int bin_search_StepSize(int desired_rate, int ix[GRANULE_SIZE],
                               gr_info *cod_info, shine_global_config *config);
static int new_choose_table(int ix[GRANULE_SIZE], unsigned int begin,
                            unsigned int end, int *bits);
static int bigv_tab_select(int ix[GRANULE_SIZE], gr_info *cod_info, int limit);
static void subdivide(gr_info *cod_info, shine_global_config *config);
static int count1_bitcount(int ix[GRANULE_SIZE], gr_info *cod_info);
static void calc_runlen(int ix[GRANULE_SIZE], gr_info *cod_info);
static int quantize(int ix[GRANULE_SIZE], int stepsize,
                    shine_global_config *config);

/*
 * shine_inner_loop:
 * ----------
 * The code selects the best quantizerStepSize for a particular set
 * of scalefacs.
 */
int shine_inner_loop(int ix[GRANULE_SIZE], int max_bits, gr_info *cod_info,
                     shine_global_config *config) {
  int bits;

  if (max_bits < 0)
    cod_info->quantizerStepSize--;
  do {
    while (quantize(ix, ++cod_info->quantizerStepSize, config) > 8192)
      ; /* within table range? */

    /* The last step size keeps its count whatever it is. */
    bits = shine_count_bits_up_to(
        ix, cod_info, config,
        cod_info->quantizerStepSize < MAX_STEPSIZE ? max_bits : INT_MAX);
    /* All coefficients quantize to 0 well before MAX_STEPSIZE. */
  } while (bits > max_bits && cod_info->quantizerStepSize < MAX_STEPSIZE);
  return bits;
}

/*
 * shine_outer_loop:
 * -----------
 *  Function: The outer iteration loop controls the masking conditions
 *  of all scalefactorbands. It computes the best scalefac and
 *  global gain. This module calls the inner iteration loop.
 */

int shine_outer_loop(int max_bits,
                     int ix[GRANULE_SIZE], /* quantized values ix(0..575) */
                     int gr, int ch, shine_global_config *config) {
  int bits, huff_bits;
  shine_side_info_t *side_info = &config->side_info;
  gr_info *cod_info = &side_info->gr[gr].ch[ch].tt;

  cod_info->quantizerStepSize =
      bin_search_StepSize(max_bits, ix, cod_info, config);

  cod_info->part2_length = part2_length(gr, ch, config);
  huff_bits = max_bits - cod_info->part2_length;

  bits = shine_inner_loop(ix, huff_bits, cod_info, config);
  cod_info->part2_3_length = cod_info->part2_length + bits;

  return cod_info->part2_3_length;
}

/*
 * shine_iteration_loop:
 * ------------------
 */
/* Fills xrabs with the magnitudes of xr. Returns the largest. */
static int32_t absolute_values(const int32_t *xr, int32_t *xrabs) {
  int32_t max = 0;

  for (int i = 0; i < GRANULE_SIZE; i++) {
    xrabs[i] = labs(xr[i]);
    max = xrabs[i] > max ? xrabs[i] : max;
  }
  return max;
}

void shine_iteration_loop(shine_global_config *config) {
  gr_info *cod_info;
  int max_bits;
  int ch, gr, i;
  int *ix;

  for (gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
    for (ch = 0; ch < config->wave.channels; ch++) {
      /* setup pointers */
      ix = config->l3_enc[ch][gr];
      config->l3loop.xr = config->mdct_freq[ch][gr];

      config->l3loop.xrmax =
          absolute_values(config->l3loop.xr, config->l3loop.xrabs);

      cod_info = (gr_info *)&(config->side_info.gr[gr].ch[ch]);
      cod_info->sfb_lmax = SFB_LMAX - 1; /* gr_deco */

      /* calculation of number of available bit( per granule ) */
      if (config->psy && ch == 0)
        shine_psy_granule_bits(config, gr);
      max_bits = config->psy ? shine_psy_channel_bits(config, gr, ch)
                             : shine_max_reservoir_bits(config);

      /* reset of iteration variables */
      memset(config->scalefactor.l[gr][ch], 0,
             sizeof(config->scalefactor.l[gr][ch]));
      memset(config->scalefactor.s[gr][ch], 0,
             sizeof(config->scalefactor.s[gr][ch]));

      for (i = 4; i--;)
        cod_info->slen[i] = 0;

      cod_info->part2_3_length = 0;
      cod_info->big_values = 0;
      cod_info->count1 = 0;
      cod_info->scalefac_compress = 0;
      cod_info->table_select[0] = 0;
      cod_info->table_select[1] = 0;
      cod_info->table_select[2] = 0;
      cod_info->region0_count = 0;
      cod_info->region1_count = 0;
      cod_info->part2_length = 0;
      cod_info->preflag = 0;
      cod_info->scalefac_scale = 0;
      cod_info->count1table_select = 0;

      if (!config->psy)
        for (int run = 0; run < GRANULE_SIZE / QUANT_RUN_LINES; run++)
          config->l3loop.run_peak[run] = shine_band_peak(
              run * QUANT_RUN_LINES, (run + 1) * QUANT_RUN_LINES, config);

      /* all spectral values zero ? */
      if (config->psy) {
        cod_info->quantizerStepSize = 0;
        memset(ix, 0, GRANULE_SIZE * sizeof(*ix));
        if (config->l3loop.xrmax)
          cod_info->part2_3_length =
              shine_psy_allocate(config, ix, gr, ch, max_bits);
      } else if (config->l3loop.xrmax)
        cod_info->part2_3_length =
            shine_outer_loop(max_bits, ix, gr, ch, config);

      shine_ResvAdjust(cod_info, config);
      cod_info->global_gain =
          cod_info->quantizerStepSize + 210 + MDCT_HEADROOM_STEPS +
          (cod_info->block_type == BLOCK_SHORT ? SHORT_HEADROOM_STEPS : 0);
    }
  }

  shine_ResvFrameEnd(config);
}

/*
 * part2_length:
 * -------------
 * calculates the number of bits needed to encode the scalefacs in the
 * main data block.
 */
int part2_length(int gr, int ch, shine_global_config *config) {
  int slen1, slen2, bits;
  gr_info *gi = &config->side_info.gr[gr].ch[ch].tt;

  bits = 0;

  {
    slen1 = shine_slen1_tab[gi->scalefac_compress];
    slen2 = shine_slen2_tab[gi->scalefac_compress];

    if (!gr || !(config->side_info.scfsi[ch][0]))
      bits += (6 * slen1);

    if (!gr || !(config->side_info.scfsi[ch][1]))
      bits += (5 * slen1);

    if (!gr || !(config->side_info.scfsi[ch][2]))
      bits += (5 * slen2);

    if (!gr || !(config->side_info.scfsi[ch][3]))
      bits += (5 * slen2);
  }
  return bits;
}

/*
 * quantize:
 * ---------
 * Function: Quantization of the vector xr ( -> ix).
 * Returns maximum value of ix.
 */
/* Step sizes above 0 only occur for full scale coefficients at low bitrates,
 * where steptabi would round 2**(-stepsize/4) to an integer. */
static int pow34(uint32_t x);

/* nint(x^(3/4) - 0.0946) of a coefficient x given in Q8, and ln, x
 * rounded to an integer: rounding first would move the boundaries of small
 * values, the one between 1 and 2 from 1.86 to 1.5. */
static int exact_index(int ln, int64_t x_q8) {
  if (ln >= QUANT_SPLIT_LINES)
    return ln < 10000 ? shine_int2idx[ln] : pow34((uint32_t)ln);
  return shine_quant_base[ln] + (x_q8 >= shine_quant_split[ln]);
}

static int coarse_index(int32_t xrabs, uint64_t fraction) {
  /* Rounded xrabs * 2**(-stepsize/4) / 2**31: at most 1. */
  int ln = (int)(((uint64_t)xrabs * fraction + (1ULL << 62)) >> 63);

  return exact_index(
      ln, (int64_t)(((uint64_t)xrabs * fraction + (1ULL << 54)) >> 55));
}

/* The quantiser does not decrease with the magnitude: lines whose peak
 * quantises to 0 all do. */
static int quantize_coarse(int ix[GRANULE_SIZE], int begin, int end,
                           int stepsize, int32_t peak,
                           shine_global_config *config) {
  uint64_t fraction = shine_steptab_fraction[stepsize];
  int max = 0;

  if (!coarse_index(peak, fraction)) {
    memset(&ix[begin], 0, (size_t)(end - begin) * sizeof(*ix));
    return 0;
  }
  for (int i = begin; i < end; i++) {
    ix[i] = coarse_index(config->l3loop.xrabs[i], fraction);
    if (max < ix[i])
      max = ix[i];
  }

  return max;
}

/* floor(x**(3/4)) for x <= 165140, i.e. results up to 8192: the largest y
 * with y**4 <= x**3, both below 2**53. */
static int pow34(uint32_t x) {
  uint64_t cube = (uint64_t)x * x * x;
  int low = 0, high = 8192;

  while (low < high) {
    uint64_t middle = (low + high + 1) / 2;
    if (middle * middle * middle * middle <= cube)
      low = (int)middle;
    else
      high = (int)middle - 1;
  }
  return low;
}

/* The quantiser of exact_index as a function of x_q8 alone is a staircase:
 * the smallest x_q8 it takes to level, which is below the table split. */
static int64_t level_start(int level) {
  for (int ln = 0; ln < QUANT_SPLIT_LINES; ln++) {
    int64_t first = ln ? ln * 256 - 128 : 0;

    if (shine_quant_base[ln] >= level)
      return first;
    if (shine_quant_base[ln] + 1 >= level &&
        shine_quant_split[ln] < ln * 256 + 128)
      return shine_quant_split[ln] > first ? shine_quant_split[ln] : first;
  }
  return INT64_MAX >> 24;
}

void shine_quantize_initialise(shine_global_config *config) {
  for (int level = 1; level <= QUANT_DIRECT_LEVELS + 1; level++) {
    /* x_q8 >= start when xrabs * scalei + 2^23 >= start * 2^24. */
    int64_t product = (level_start(level) << 24) - (1LL << 23);

    for (int step = 0; step < 128; step++) {
      int64_t scalei = shine_steptabi[step];
      int64_t threshold = product <= 0 ? 0 : (product + scalei - 1) / scalei;

      /* No magnitude reaches 2^31. */
      config->l3loop.quant_threshold[step][level - 1] =
          threshold > INT32_MAX ? 1u << 31 : (uint32_t)threshold;
    }
  }
}

/* Quantizes lines begin..end - 1, all below threshold[QUANT_DIRECT_LEVELS]:
 * each line is the number of thresholds it reaches. Returns the largest. */
static int quantize_direct(int ix[GRANULE_SIZE], int begin, int end,
                           uint32_t peak, const uint32_t *threshold,
                           const int32_t *xrabs) {
  int max = 0;

  while (max < QUANT_DIRECT_LEVELS && peak >= threshold[max])
    max++;
  if (!max) {
    memset(&ix[begin], 0, (size_t)(end - begin) * sizeof(*ix));
    return 0;
  }
  for (int i = begin; i < end; i++) {
    int count = 0;

    for (int level = 0; level < QUANT_DIRECT_LEVELS; level++)
      count += (uint32_t)xrabs[i] >= threshold[level];
    ix[i] = count;
  }
  return max;
}

/* Quantizes lines begin..end - 1 with one step size, whose largest
 * coefficient is xrmax. Returns the largest ix, above 8192 when the step is
 * too small for the table range. */
static int quantize_range(int ix[GRANULE_SIZE], int begin, int end,
                          int stepsize, int32_t xrmax,
                          shine_global_config *config) {
  int max = 0;
  int32_t scalei;
  const uint32_t *threshold;

  if (stepsize > 0)
    return quantize_coarse(ix, begin, end, stepsize, xrmax, config);

  threshold = config->l3loop.quant_threshold[stepsize + 127];
  if ((uint32_t)xrmax < threshold[QUANT_DIRECT_LEVELS])
    return quantize_direct(ix, begin, end, (uint32_t)xrmax, threshold,
                           config->l3loop.xrabs);

  scalei = shine_steptabi[stepsize + 127]; /* 2**(-stepsize/4) */

  /* a quick check to see if ixmax will be less than 8192 */
  /* this speeds up the early calls to bin_search_StepSize */
  if ((mulr(xrmax, scalei)) > 165140) /* 8192**(4/3) */
    return 16384; /* no point in continuing, stepsize not big enough */

  /* Whether every line is below the table split of exact_index. */
  if (((((int64_t)xrmax * scalei + (1LL << 23)) >> 24) + 128) >> 8 <
      QUANT_SPLIT_LINES) {
    for (int i = begin; i < end; i++) {
      int64_t x_q8 =
          ((int64_t)config->l3loop.xrabs[i] * scalei + (1LL << 23)) >> 24;
      int ln = (int)((x_q8 + 128) >> 8);

      ix[i] = shine_quant_base[ln] + (x_q8 >= shine_quant_split[ln]);
      if (max < ix[i])
        max = ix[i];
    }
    return max;
  }

  for (int i = begin; i < end; i++) {
    /* The multiply must round its result: truncating it moves the
     * boundaries between quantised values. */
    int64_t x_q8 =
        ((int64_t)config->l3loop.xrabs[i] * scalei + (1LL << 23)) >> 24;
    int ln = (int)((x_q8 + 128) >> 8);

    /* The table covers about all values; the quick check above bounds the
     * rest by 165140. */
    ix[i] = exact_index(ln, x_q8);
    if (max < ix[i])
      max = ix[i];
  }

  return max;
}

int quantize(int ix[GRANULE_SIZE], int stepsize, shine_global_config *config) {
  int max = 0;

  /* Whether the step is too small, as quantize_range finds for the run of
   * the largest line. */
  if (stepsize <= 0 &&
      mulr(config->l3loop.xrmax, shine_steptabi[stepsize + 127]) > 165140)
    return 16384;
  for (int run = 0; run < GRANULE_SIZE / QUANT_RUN_LINES; run++) {
    int run_max =
        quantize_range(ix, run * QUANT_RUN_LINES, (run + 1) * QUANT_RUN_LINES,
                       stepsize, config->l3loop.run_peak[run], config);

    if (run_max > max)
      max = run_max;
  }
  return max;
}

/*
 * ix_max:
 * -------
 * Function: Calculate the maximum of ix from 0 to 575
 */
static inline int ix_max(int ix[GRANULE_SIZE], unsigned int begin,
                         unsigned int end) {
  int max = 0;

  for (unsigned int i = begin; i < end; i++)
    if (max < ix[i])
      max = ix[i];
  return max;
}

/*
 * calc_runlen:
 * ------------
 * Function: Calculation of rzero, count1, big_values
 * (Partitions ix into big values, quadruples and zeros).
 */
/* The end of ix without its trailing pairs of zeros. The values are not
 * negative: a group is zero when its bitwise or is. */
static int nonzero_end(const int ix[GRANULE_SIZE]) {
  int i = GRANULE_SIZE;

  while (i >= 8 && !(ix[i - 1] | ix[i - 2] | ix[i - 3] | ix[i - 4] | ix[i - 5] |
                     ix[i - 6] | ix[i - 7] | ix[i - 8]))
    i -= 8;
  while (i > 1 && !(ix[i - 1] | ix[i - 2]))
    i -= 2;
  return i;
}

void calc_runlen(int ix[GRANULE_SIZE], gr_info *cod_info) {
  int i = nonzero_end(ix);

  cod_info->count1 = 0;
  for (; i > 3; i -= 4)
    if (ix[i - 1] <= 1 && ix[i - 2] <= 1 && ix[i - 3] <= 1 && ix[i - 4] <= 1)
      cod_info->count1++;
    else
      break;

  cod_info->big_values = i >> 1;
}

/*
 * count1_bitcount:
 * ----------------
 * Determines the number of bits to encode the quadruples.
 */
/* The bits of quadruple p = v + 2w + 4x + 8y, its sign bits included: table
 * 32 (shine_huffman_table[32].hlen) in the low half, table 33, whose codes
 * all have 4 bits, in the high half. A granule has at most 144 quadruples
 * of at most 10 bits. */
#define COUNT1_COST(hlen32, signs)                                             \
  ((uint32_t)((hlen32) + (signs)) | (uint32_t)(4 + (signs)) << 16)
static const uint32_t count1_cost[16] = {
    COUNT1_COST(1, 0), COUNT1_COST(4, 1), COUNT1_COST(4, 1), COUNT1_COST(5, 2),
    COUNT1_COST(4, 1), COUNT1_COST(6, 2), COUNT1_COST(5, 2), COUNT1_COST(6, 3),
    COUNT1_COST(4, 1), COUNT1_COST(5, 2), COUNT1_COST(5, 2), COUNT1_COST(6, 3),
    COUNT1_COST(5, 2), COUNT1_COST(6, 3), COUNT1_COST(6, 3), COUNT1_COST(6, 4)};

int count1_bitcount(int ix[GRANULE_SIZE], gr_info *cod_info) {
  const int *quad = ix + (cod_info->big_values << 1);
  uint32_t sums = 0;
  int sum0, sum1;

  for (unsigned int k = 0; k < cod_info->count1; k++, quad += 4)
    sums +=
        count1_cost[quad[0] + (quad[1] << 1) + (quad[2] << 2) + (quad[3] << 3)];
  sum0 = (int)(sums & 0xffff);
  sum1 = (int)(sums >> 16);
  if (sum0 < sum1) {
    cod_info->count1table_select = 0;
    return sum0;
  } else {
    cod_info->count1table_select = 1;
    return sum1;
  }
}

/*
 * subdivide:
 * ----------
 * presumable subdivides the bigvalue region which will use separate Huffman
 * tables.
 */
void subdivide(gr_info *cod_info, shine_global_config *config) {
  static const struct {
    unsigned region0_count;
    unsigned region1_count;
  } subdv_table[23] = {
      {0, 0}, /* 0 bands */
      {0, 0}, /* 1 bands */
      {0, 0}, /* 2 bands */
      {0, 0}, /* 3 bands */
      {0, 0}, /* 4 bands */
      {0, 1}, /* 5 bands */
      {1, 1}, /* 6 bands */
      {1, 1}, /* 7 bands */
      {1, 2}, /* 8 bands */
      {2, 2}, /* 9 bands */
      {2, 3}, /* 10 bands */
      {2, 3}, /* 11 bands */
      {3, 4}, /* 12 bands */
      {3, 4}, /* 13 bands */
      {3, 4}, /* 14 bands */
      {4, 5}, /* 15 bands */
      {4, 5}, /* 16 bands */
      {4, 6}, /* 17 bands */
      {5, 6}, /* 18 bands */
      {5, 6}, /* 19 bands */
      {5, 7}, /* 20 bands */
      {6, 7}, /* 21 bands */
      {6, 7}, /* 22 bands */
  };

  if (!cod_info->big_values) { /* no big_values region */
    cod_info->region0_count = 0;
    cod_info->region1_count = 0;
    cod_info->address1 = cod_info->address2 = cod_info->address3 = 0;
  } else if (cod_info->block_type) {
    /* Two regions at a fixed boundary (ISO 11172-3 2.4.2.7): after 3 short
     * bands, or 8 long ones. */
    unsigned boundary =
        cod_info->block_type == BLOCK_SHORT
            ? 3 * (unsigned)shine_scale_fact_band_index_short
                      [config->mpeg.samplerate_index][3]
            : (unsigned)
                  shine_scale_fact_band_index[config->mpeg.samplerate_index][8];
    unsigned bigvalues_region = 2 * cod_info->big_values;

    cod_info->address1 =
        boundary < bigvalues_region ? boundary : bigvalues_region;
    cod_info->address2 = cod_info->address3 = bigvalues_region;
  } else {
    const int *scalefac_band_long =
        &shine_scale_fact_band_index[config->mpeg.samplerate_index][0];
    int bigvalues_region, scfb_anz, thiscount;

    bigvalues_region = 2 * cod_info->big_values;

    /* Calculate scfb_anz */
    scfb_anz = 0;
    while (scalefac_band_long[scfb_anz] < bigvalues_region)
      scfb_anz++;

    for (thiscount = subdv_table[scfb_anz].region0_count; thiscount;
         thiscount--) {
      if (scalefac_band_long[thiscount + 1] <= bigvalues_region)
        break;
    }
    cod_info->region0_count = thiscount;
    cod_info->address1 = scalefac_band_long[thiscount + 1];

    scalefac_band_long += cod_info->region0_count + 1;

    for (thiscount = subdv_table[scfb_anz].region1_count; thiscount;
         thiscount--) {
      if (scalefac_band_long[thiscount + 1] <= bigvalues_region)
        break;
    }
    cod_info->region1_count = thiscount;
    cod_info->address2 = scalefac_band_long[thiscount + 1];

    cod_info->address3 = bigvalues_region;
  }
}

/*
 * bigv_tab_select:
 * ----------------
 * Function: Select huffman code tables for bigvalues regions
 */
/* Stops with the bits so far once they are above limit. */
int bigv_tab_select(int ix[GRANULE_SIZE], gr_info *cod_info, int limit) {
  int bits = 0;

  cod_info->table_select[0] = 0;
  cod_info->table_select[1] = 0;
  cod_info->table_select[2] = 0;

  if (cod_info->address1 > 0)
    cod_info->table_select[0] =
        new_choose_table(ix, 0, cod_info->address1, &bits);

  if (bits > limit)
    return bits;
  if (cod_info->address2 > cod_info->address1)
    cod_info->table_select[1] =
        new_choose_table(ix, cod_info->address1, cod_info->address2, &bits);

  if (bits > limit)
    return bits;
  if (cod_info->big_values << 1 > cod_info->address2)
    cod_info->table_select[2] = new_choose_table(
        ix, cod_info->address2, cod_info->big_values << 1, &bits);
  return bits;
}

/*
 * new_choose_table:
 * -----------------
 * Choose the Huffman table that will encode ix[begin..end] with
 * the fewest bits.
 * Note: This code contains knowledge about the sizes and characteristics
 * of the Huffman tables as defined in the IS (Table B.7), and will not work
 * with any arbitrary tables.
 */
/* The first table of [first, first + 8), which share their codes, with
 * enough linbits for max. */
static int escape_table(int first, unsigned int max) {
  int table = first;

  while (max > 15 + shine_huffman_table[table].linmax)
    table++;
  return table;
}

/* The cheapest table for a region whose largest value is above 15: the
 * first one that codes max in each of the two families. */
static int choose_escape_table(int ix[GRANULE_SIZE], unsigned int begin,
                               unsigned int end, unsigned int max, int *bits) {
  int low = escape_table(16, max), high = escape_table(24, max);
  uint32_t codes = 0, escapes = 0, low_bits, high_bits;

  for (unsigned int i = begin; i < end; i += 2) {
    unsigned int x = ix[i], y = ix[i + 1];

    if (x > 14) {
      x = 15;
      escapes++;
    }
    if (y > 14) {
      y = 15;
      escapes++;
    }
    codes += shine_huffman_escape_cost[16 * x + y];
  }
  high_bits = (codes >> 16) + escapes * shine_huffman_table[high].linbits;
  low_bits = (codes & 0xffff) + escapes * shine_huffman_table[low].linbits;
  *bits += (int)(high_bits < low_bits ? high_bits : low_bits);
  return high_bits < low_bits ? high : low;
}

/* The cheapest table that codes [begin, end), the lowest one on a tie.
 * Each 16-bit field of a sum holds the bits of one candidate: a region has
 * at most 288 pairs of at most 27 bits. */
int new_choose_table(int ix[GRANULE_SIZE], unsigned int begin, unsigned int end,
                     int *bits) {
  unsigned int max = 0;
  int signs = 0;
  const shine_huffman_choice_t *choice;
  const uint64_t *cost;
  unsigned int stride;
  uint64_t sums[2] = {0, 0};
  int best = 0;

  /* The sign bits, which the costs leave out. */
  for (unsigned int i = begin; i < end; i++) {
    max = (unsigned int)ix[i] > max ? (unsigned int)ix[i] : max;
    signs += ix[i] != 0;
  }
  if (!max)
    return 0;
  *bits += signs;
  choice = &shine_huffman_choice[max & 15];
  cost = shine_huffman_choice_cost + choice->offset;
  stride = choice->stride;
  if (max > 15)
    return choose_escape_table(ix, begin, end, max, bits);

  if (choice->count <= 4)
    for (unsigned int i = begin; i < end; i += 2)
      sums[0] += cost[ix[i] * stride + ix[i + 1]];
  else
    for (unsigned int i = begin; i < end; i += 2) {
      const uint64_t *pair = cost + 2 * (ix[i] * stride + ix[i + 1]);

      sums[0] += pair[0];
      sums[1] += pair[1];
    }

  for (int i = 1; i < choice->count; i++)
    if (((sums[i >> 2] >> (16 * (i & 3))) & 0xffff) <
        ((sums[best >> 2] >> (16 * (best & 3))) & 0xffff))
      best = i;
  *bits += (int)((sums[best >> 2] >> (16 * (best & 3))) & 0xffff);
  return choice->tables[best];
}

/*
 * bin_search_StepSize:
 * --------------------
 * Succesive approximation approach to obtaining a initial quantizer
 * step size.
 * The following optional code written by Seymour Shlien
 * will speed up the shine_outer_loop code which is called
 * by iteration_loop. When BIN_SEARCH is defined, the
 * shine_outer_loop function precedes the call to the function shine_inner_loop
 * with a call to bin_search gain defined below, which
 * returns a good starting quantizerStepSize.
 */
int bin_search_StepSize(int desired_rate, int ix[GRANULE_SIZE],
                        gr_info *cod_info, shine_global_config *config) {
  int bit, next, count;

  next = -120;
  count = 120;

  do {
    int half = count / 2;

    if (quantize(ix, next + half, config) > 8192)
      bit = 100000; /* fail */
    else
      bit = shine_count_bits_up_to(ix, cod_info, config, desired_rate);

    if (bit < desired_rate)
      count = half;
    else {
      next += half;
      count -= half;
    }
  } while (count > 1);

  return next;
}

int32_t shine_band_peak(int begin, int end, const shine_global_config *config) {
  int32_t peak = 0;

  for (int i = begin; i < end; i++)
    if (config->l3loop.xrabs[i] > peak)
      peak = config->l3loop.xrabs[i];
  return peak;
}

int shine_quantize_band(int ix[GRANULE_SIZE], int begin, int end, int stepsize,
                        int32_t peak, shine_global_config *config) {
  return quantize_range(ix, begin, end, stepsize, peak, config);
}

int shine_count_bits_up_to(int ix[GRANULE_SIZE], gr_info *cod_info,
                           shine_global_config *config, int limit) {
  int bits;

  calc_runlen(ix, cod_info);
  bits = count1_bitcount(ix, cod_info);
  if (bits > limit)
    return bits;
  subdivide(cod_info, config);
  return bits + bigv_tab_select(ix, cod_info, limit - bits);
}

int shine_count_bits(int ix[GRANULE_SIZE], gr_info *cod_info,
                     shine_global_config *config) {
  return shine_count_bits_up_to(ix, cod_info, config, INT_MAX);
}

/* Classes of big-value regions: a region whose largest value is max chooses
 * among the tables of shine_huffman_choice[max], which several values of
 * max share, or among the escape tables above 15. A class is named by the
 * smallest max that has its tables. */
#define ESCAPE_CLASS 16

static int region_class(int max) {
  if (max > 15)
    return ESCAPE_CLASS;
  while (max > 1 && shine_huffman_choice[max - 1].offset ==
                        shine_huffman_choice[max].offset)
    max--;
  return max;
}

/* The class of every max up to ESCAPE_CLASS, which stands for all above. */
typedef struct {
  uint8_t of[ESCAPE_CLASS + 1];
} region_classes_t;

static region_classes_t all_region_classes(void) {
  region_classes_t classes;

  for (int max = 0; max <= ESCAPE_CLASS; max++)
    classes.of[max] = (uint8_t)region_class(max);
  return classes;
}

static int class_of(const region_classes_t *classes, int max) {
  return classes->of[max > ESCAPE_CLASS ? ESCAPE_CLASS : max];
}

/* The bits of the pairs of [begin, end) in every table of a class, added
 * to sum. Each 16-bit field holds one table, as in new_choose_table: a
 * granule has at most 288 pairs of at most 27 bits. */
static void add_class_bits(const int *ix, int begin, int end, int class,
                           shine_region_bits_t *sum) {
  const shine_huffman_choice_t *choice = &shine_huffman_choice[class & 15];
  const uint64_t *cost = shine_huffman_choice_cost + choice->offset;

  if (class == ESCAPE_CLASS)
    for (int i = begin; i < end; i += 2) {
      unsigned int x = ix[i], y = ix[i + 1];

      if (x > 14) {
        x = 15;
        sum->escapes++;
      }
      if (y > 14) {
        y = 15;
        sum->escapes++;
      }
      sum->codes[0] += shine_huffman_escape_cost[16 * x + y];
    }
  else if (choice->count <= 4)
    for (int i = begin; i < end; i += 2)
      sum->codes[0] += cost[ix[i] * choice->stride + ix[i + 1]];
  else
    for (int i = begin; i < end; i += 2) {
      const uint64_t *pair = cost + 2 * (ix[i] * choice->stride + ix[i + 1]);

      sum->codes[0] += pair[0];
      sum->codes[1] += pair[1];
    }
}

/* The cheapest table of a class for the pairs whose bits are `to - from`,
 * with largest value max, and its bits without the signs. */
static int cheapest_in_class(const shine_region_bits_t *from,
                             const shine_region_bits_t *to, int max,
                             int *bits) {
  const uint64_t codes[2] = {to->codes[0] - from->codes[0],
                             to->codes[1] - from->codes[1]};
  const shine_huffman_choice_t *choice = &shine_huffman_choice[max & 15];
  int best = 0;

  if (max > 15) {
    const int low = escape_table(16, max), high = escape_table(24, max);
    const uint32_t escapes = to->escapes - from->escapes;
    const uint32_t high_bits = (uint32_t)((codes[0] >> 16) & 0xffff) +
                               escapes * shine_huffman_table[high].linbits,
                   low_bits = (uint32_t)(codes[0] & 0xffff) +
                              escapes * shine_huffman_table[low].linbits;

    *bits = (int)(high_bits < low_bits ? high_bits : low_bits);
    return high_bits < low_bits ? high : low;
  }
  for (int i = 1; i < choice->count; i++)
    if (((codes[i >> 2] >> (16 * (i & 3))) & 0xffff) <
        ((codes[best >> 2] >> (16 * (best & 3))) & 0xffff))
      best = i;
  *bits = (int)((codes[best >> 2] >> (16 * (best & 3))) & 0xffff);
  return choice->tables[best];
}

/* The bands of the big values: the scalefactor bands, the last one cut at
 * the end of the big values. Returns their number. */
static int big_value_bands(const int *sfb, int bigvalues, int edge[23]) {
  int bands = 0;

  for (edge[0] = 0; edge[bands] < bigvalues; bands++)
    edge[bands + 1] = sfb[bands + 1] < bigvalues ? sfb[bands + 1] : bigvalues;
  return bands;
}

/* Running sums over the bands of their bits in the tables of every class a
 * region can have: those of the bands' largest values. A band adds nothing
 * to a class too small for it, which no region holding it has; a band of
 * zeros has bits in every class. */
static void sum_class_bits(const int *ix, const int *edge, int bands,
                           const int *band_max, const region_classes_t *classes,
                           shine_region_bits_t (*sums)[23]) {
  int present[ESCAPE_CLASS + 1] = {0}, band_class[22];

  for (int band = 0; band < bands; band++) {
    band_class[band] = class_of(classes, band_max[band]);
    present[band_class[band]] = 1;
  }
  for (int class = 1; class <= ESCAPE_CLASS; class++) {
    if (!present[class])
      continue;
    memset(&sums[class][0], 0, sizeof(sums[class][0]));
    for (int band = 0; band < bands; band++) {
      sums[class][band + 1] = sums[class][band];
      if (band_class[band] <= class)
        add_class_bits(ix, edge[band], edge[band + 1], class,
                       &sums[class][band + 1]);
    }
  }
}

/* The cheapest table for bands [first, last), whose largest value is max,
 * and its bits without the signs; table 0 and no bits for zeros. */
static int region_table(const shine_region_bits_t (*sums)[23],
                        const region_classes_t *classes, int max, int first,
                        int last, int *bits) {
  const int class = class_of(classes, max);

  *bits = 0;
  if (!max)
    return 0;
  return cheapest_in_class(&sums[class][first], &sums[class][last], max, bits);
}

/* A split of the big-value bands into [0, first), [first, second) and the
 * rest. */
typedef struct {
  int first, second, bits;
  int tables[3];
} region_split_t;

/* The cheapest split among every pair of region counts, the first one
 * found on a tie. Each distinct region is evaluated once: region 0 depends
 * on `first` alone, region 2 on `second` alone. */
static region_split_t cheapest_split(const shine_region_bits_t (*sums)[23],
                                     const region_classes_t *classes,
                                     const int *band_max, int bands,
                                     unsigned *count0_out,
                                     unsigned *count1_out) {
  int head_bits[23], head_table[23], tail_bits[23], tail_table[23];
  region_split_t best = {0, 0, -1, {0, 0, 0}};

  for (int last = 1, max = 0; last <= bands; last++) {
    max = band_max[last - 1] > max ? band_max[last - 1] : max;
    head_table[last] =
        region_table(sums, classes, max, 0, last, &head_bits[last]);
  }
  tail_table[bands] = tail_bits[bands] = 0;
  for (int first = bands - 1, max = 0; first >= 0; first--) {
    max = band_max[first] > max ? band_max[first] : max;
    tail_table[first] =
        region_table(sums, classes, max, first, bands, &tail_bits[first]);
  }

  for (int count0 = 0; count0 < 16; count0++) {
    const int first = count0 + 1 < bands ? count0 + 1 : bands;

    for (int count1 = 0, max = 0, reached = first;
         count1 < 8 && count0 + count1 + 2 <= 22; count1++) {
      const int second =
          first + count1 + 1 < bands ? first + count1 + 1 : bands;
      int middle_bits, middle_table, bits;

      for (; reached < second; reached++)
        max = band_max[reached] > max ? band_max[reached] : max;
      middle_table =
          region_table(sums, classes, max, first, second, &middle_bits);
      bits = head_bits[first] + middle_bits + tail_bits[second];
      if (best.bits >= 0 && bits >= best.bits)
        continue;
      best = (region_split_t){
          first,
          second,
          bits,
          {head_table[first], middle_table, tail_table[second]}};
      *count0_out = (unsigned)count0;
      *count1_out = (unsigned)count1;
    }
    /* Every later pair gives this same split. */
    if (first == bands)
      break;
  }
  return best;
}

int shine_count_bits_split(int ix[GRANULE_SIZE], gr_info *gi,
                           shine_global_config *config,
                           shine_region_bits_t (*sums)[23]) {
  const int *sfb = shine_scale_fact_band_index[config->mpeg.samplerate_index];
  int edge[23], band_max[22], bands, bigvalues, bits, signs = 0;
  region_split_t split;
  region_classes_t classes;

  calc_runlen(ix, gi);
  bits = count1_bitcount(ix, gi);
  bigvalues = (int)gi->big_values << 1;
  if (gi->block_type || !bigvalues) {
    subdivide(gi, config);
    return bits + bigv_tab_select(ix, gi, INT_MAX);
  }
  classes = all_region_classes();
  bands = big_value_bands(sfb, bigvalues, edge);
  for (int band = 0; band < bands; band++)
    band_max[band] = ix_max(ix, edge[band], edge[band + 1]);
  for (int i = 0; i < bigvalues; i++)
    signs += ix[i] != 0;
  sum_class_bits(ix, edge, bands, band_max, &classes, sums);

  split = cheapest_split(sums, &classes, band_max, bands, &gi->region0_count,
                         &gi->region1_count);
  gi->address1 = edge[split.first];
  gi->address2 = edge[split.second];
  gi->address3 = bigvalues;
  for (int region = 0; region < 3; region++)
    gi->table_select[region] = (unsigned)split.tables[region];
  return bits + signs + split.bits;
}
