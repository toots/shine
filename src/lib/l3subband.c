/* L3SubBand */

#include "l3subband.h"
#include "tables.h"
#include "types.h"

/* The 32 newest samples of the channel replace the 32 oldest of its window
 * buffer, scaled to Q31. */
static void push_samples(shine_global_config *config, int ch, int stride) {
  const int16_t *ptr = config->buffer[ch];
  int32_t *x = &config->subband.x[ch][config->subband.off[ch]];

  for (int i = 32; i--;) {
    x[i] = (int32_t)*ptr * 65536;
    ptr += stride;
  }
  config->buffer[ch] = ptr;
}

void shine_window_filter_subband(shine_global_config *config, int ch,
                                 int stride, int32_t s[18][SBLIMIT]) {
  int32_t folded[32][SHINE_FOLDED_STEPS] = {{0}};

  for (int step = 0; step < 18; step++) {
    push_samples(config, ch, stride);
    config->kernels.filter_fold(config->subband.x[ch], config->subband.off[ch],
                                step, folded);
    config->subband.off[ch] = (config->subband.off[ch] + 480) & (HAN_SIZE - 1);
  }
  config->kernels.filter_dct3(folded, s);
}
