/* L3mdct */

#include "l3mdct.h"
#include "l3subband.h"
#include "psy.h"
#include "tables.h"
#include "types.h"

/* The 18 coefficients of a long block, with the window in `table`. With the
 * headroom of the tables, the sum of the absolute values of the terms is at
 * most 0.705 * 2**31 for any input. */
static void mdct_long(const int32_t in[36], const int32_t table[18][36],
                      int32_t out[18]) {
  for (int k = 18; k--;) {
    int32_t sum = 0;

    for (int j = 0; j < 36; j++)
      sum += mul(in[j], table[k][j]);
    out[k] = sum;
  }
}

/* The 6 coefficients of each of the three short windows of a block, which
 * cover samples 6..17, 12..23 and 18..29. */
static void mdct_short(const int32_t in[36], int32_t out[3][6]) {
  for (int window = 0; window < 3; window++)
    for (int k = 0; k < 6; k++) {
      int32_t sum = 0;

      for (int j = 0; j < 12; j++)
        sum += mul(in[6 + 6 * window + j], shine_mdct_cosine_short[k][j]);
      out[window][k] = sum;
    }
}

/* Short coefficients in coded order: by scalefactor band, then window. */
static void reorder_short(const int32_t lines[3][192], const int *sfb,
                          int32_t *out) {
  for (int band = 0; band < 13; band++) {
    int start = sfb[band], width = sfb[band + 1] - start;

    for (int window = 0; window < 3; window++)
      for (int i = 0; i < width; i++)
        out[3 * start + window * width + i] = lines[window][start + i];
  }
}

void shine_mdct_sub(shine_global_config *config, int stride) {
  int32_t mdct_in[36];

  for (int ch = config->wave.channels; ch--;) {
    for (int gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
      int32_t (*mdct_enc)[18] = (int32_t (*)[18])config->mdct_freq[ch][gr];
      unsigned block_type = config->side_info.gr[gr].ch[ch].tt.block_type;
      const int32_t (*table)[36] =
          block_type == BLOCK_START  ? shine_mdct_cosine_start
          : block_type == BLOCK_STOP ? shine_mdct_cosine_stop
                                     : shine_mdct_cosine;
      /* A short granule also gets a long MDCT, for the model. */
      int32_t (*long_enc)[18] =
          block_type == BLOCK_SHORT
              ? (int32_t (*)[18])config->psy->analysis_long[ch][gr]
              : mdct_enc;
      int32_t scratch_lines[3][192];
      /* The model analyses the short spectra of every granule
       * (doc/psy/spectral-analysis.md §2). */
      int32_t (*short_lines)[192] =
          config->psy ? config->psy->short_lines[ch][gr] : scratch_lines;

      /* polyphase filtering */
      for (int k = 0; k < 18; k += 2) {
        shine_window_filter_subband(&config->buffer[ch],
                                    &config->l3_sb_sample[ch][gr + 1][k][0], ch,
                                    config, stride);
        shine_window_filter_subband(&config->buffer[ch],
                                    &config->l3_sb_sample[ch][gr + 1][k + 1][0],
                                    ch, config, stride);
        /* Compensate for inversion in the analysis filter
         * (every odd index of band AND k)
         */
        for (int band = 1; band < 32; band += 2)
          config->l3_sb_sample[ch][gr + 1][k + 1][band] *= -1;
      }

      /* MDCT of the 18 previous and the 18 current samples of each subband */
      for (int band = 0; band < 32; band++) {
        for (int k = 18; k--;) {
          mdct_in[k] = config->l3_sb_sample[ch][gr][k][band];
          mdct_in[k + 18] = config->l3_sb_sample[ch][gr + 1][k][band];
        }

        if (config->psy) {
          int32_t out[3][6];

          mdct_short(mdct_in, out);
          for (int window = 0; window < 3; window++)
            for (int k = 0; k < 6; k++)
              short_lines[window][6 * band + k] = out[window][k];
        }
        mdct_long(mdct_in, table, long_enc[band]);

        /* Perform aliasing reduction butterfly */
        if (band != 0) {
          for (int k = 0; k < 8; k++)
            cmuls(long_enc[band][k], long_enc[band - 1][17 - k],
                  long_enc[band][k], long_enc[band - 1][17 - k],
                  shine_mdct_alias_cs[k], shine_mdct_alias_ca[k]);
        }
      }
      if (config->psy)
        shine_psy_lowpass(config->psy, (int32_t *)long_enc, short_lines);
      if (block_type == BLOCK_SHORT)
        reorder_short(
            short_lines,
            shine_scale_fact_band_index_short[config->mpeg.samplerate_index],
            config->mdct_freq[ch][gr]);
    }

    /* Save latest granule's subband samples to be used in the next mdct call */
    memcpy(config->l3_sb_sample[ch][0],
           config->l3_sb_sample[ch][config->mpeg.granules_per_frame],
           sizeof(config->l3_sb_sample[0][0]));
  }
}
