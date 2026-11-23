/* L3mdct */

#include "l3mdct.h"
#include "l3subband.h"
#include "tables.h"
#include "types.h"

/*
 * shine_mdct_sub:
 * ------------
 */
void shine_mdct_sub(shine_global_config *config, int stride) {
  /* note. we wish to access the array 'config->mdct_freq[2][2][576]' as
   * [2][2][32][18]. (32*18=576),
   */
  int32_t (*mdct_enc)[18];

  int ch, gr, band, j, k;
  int32_t mdct_in[36];

  for (ch = config->wave.channels; ch--;) {
    for (gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
      /* set up pointer to the part of config->mdct_freq we're using */
      mdct_enc = (int32_t (*)[18])config->mdct_freq[ch][gr];

      /* polyphase filtering */
      for (k = 0; k < 18; k += 2) {
        shine_window_filter_subband(&config->buffer[ch],
                                    &config->l3_sb_sample[ch][gr + 1][k][0], ch,
                                    config, stride);
        shine_window_filter_subband(&config->buffer[ch],
                                    &config->l3_sb_sample[ch][gr + 1][k + 1][0],
                                    ch, config, stride);
        /* Compensate for inversion in the analysis filter
         * (every odd index of band AND k)
         */
        for (band = 1; band < 32; band += 2)
          config->l3_sb_sample[ch][gr + 1][k + 1][band] *= -1;
      }

      /* Perform imdct of 18 previous subband samples + 18 current subband
       * samples */
      for (band = 0; band < 32; band++) {
        for (k = 18; k--;) {
          mdct_in[k] = config->l3_sb_sample[ch][gr][k][band];
          mdct_in[k + 18] = config->l3_sb_sample[ch][gr + 1][k][band];
        }

        /* Calculation of the MDCT
         * In the case of long blocks ( block_type 0,1,3 ) there are
         * 36 coefficients in the time domain and 18 in the frequency
         * domain.
         */
        for (k = 18; k--;) {
          /* With the headroom in shine_mdct_cosine, the sum of the absolute
           * values of the terms is at most 0.705 * 2**31 for any input. */
          int32_t sum = 0;

          for (j = 0; j < 36; j++)
            sum += mul(mdct_in[j], shine_mdct_cosine[k][j]);
          mdct_enc[band][k] = sum;
        }

        /* Perform aliasing reduction butterfly */
        if (band != 0) {
          for (k = 0; k < 8; k++)
            cmuls(mdct_enc[band][k], mdct_enc[band - 1][17 - k],
                  mdct_enc[band][k], mdct_enc[band - 1][17 - k],
                  shine_mdct_alias_cs[k], shine_mdct_alias_ca[k]);
        }
      }
    }

    /* Save latest granule's subband samples to be used in the next mdct call */
    memcpy(config->l3_sb_sample[ch][0],
           config->l3_sb_sample[ch][config->mpeg.granules_per_frame],
           sizeof(config->l3_sb_sample[0][0]));
  }
}
