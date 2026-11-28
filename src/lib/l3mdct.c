/* L3mdct */

#include "l3mdct.h"
#include "l3subband.h"
#include "psy.h"
#include "tables.h"
#include "types.h"

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

/* The analysis filter inverts the odd samples of the odd subbands. */
static void restore_inverted_samples(int32_t s[18][SBLIMIT]) {
  for (int k = 1; k < 18; k += 2)
    for (int band = 1; band < SBLIMIT; band += 2)
      s[k][band] *= -1;
}

static void alias_reduction(int32_t lines[SBLIMIT][18]) {
  for (int band = 1; band < SBLIMIT; band++)
    for (int k = 0; k < 8; k++)
      cmuls(lines[band][k], lines[band - 1][17 - k], lines[band][k],
            lines[band - 1][17 - k], shine_mdct_alias_cs[k],
            shine_mdct_alias_ca[k]);
}

void shine_mdct_sub(shine_global_config *config, int stride) {
  for (int ch = config->wave.channels; ch--;) {
    for (int gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
      int32_t (*mdct_enc)[18] = (int32_t (*)[18])config->mdct_freq[ch][gr];
      unsigned block_type = config->side_info.gr[gr].ch[ch].tt.block_type;
      /* A short granule also gets a long MDCT, for the model. */
      int32_t (*long_enc)[18] =
          block_type == BLOCK_SHORT
              ? (int32_t (*)[18])config->psy->analysis_long[ch][gr]
              : mdct_enc;
      /* The model analyses the short spectra of every granule
       * (doc/psy/spectral-analysis.md §2). */
      int32_t (*short_lines)[192] =
          config->psy ? config->psy->short_lines[ch][gr] : NULL;

      shine_window_filter_subband(config, ch, stride,
                                  config->l3_sb_sample[ch][gr + 1]);
      restore_inverted_samples(config->l3_sb_sample[ch][gr + 1]);
      config->kernels.mdct(
          config->l3_sb_sample[ch][gr], config->l3_sb_sample[ch][gr + 1],
          shine_mdct_window[block_type], long_enc, short_lines);
      alias_reduction(long_enc);
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
