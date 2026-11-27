/* L3SubBand */

#include "l3subband.h"
#include "tables.h"
#include "types.h"

/* The next granule of the channel goes in front of the samples kept from
 * the previous one. */
static void push_granule(shine_global_config *config, int ch, int stride) {
  const int16_t *pcm = config->buffer[ch];
  int32_t *x = config->subband.x[ch];

  memcpy(&x[GRANULE_SIZE], x, SUBBAND_HISTORY * sizeof(*x));
  for (int i = 0; i < GRANULE_SIZE; i++)
    x[GRANULE_SIZE - 1 - i] = (int32_t)pcm[i * stride] * 65536;
  config->buffer[ch] = pcm + GRANULE_SIZE * stride;
}

void shine_window_filter_subband(shine_global_config *config, int ch,
                                 int stride, int32_t s[18][SBLIMIT]) {
  push_granule(config, ch, stride);
  config->kernels.filter(config->subband.x[ch], s);
}
