/* L3SubBand */

#include "l3subband.h"
#include "tables.h"
#include "types.h"

/*
 * shine_window_filter_subband:
 * -------------------------
 * Overlapping window on PCM samples
 * 32 16-bit pcm samples are scaled to fractional 2's complement and
 * concatenated to the end of the window buffer #x#. The updated window
 * buffer #x# is then windowed by the analysis window #shine_enwindow# to
 * produce the windowed sample #z# Calculates the analysis filter bank
 * coefficients The windowed samples #z# is filtered by the digital filter
 * matrix #filter# to produce the subband samples #s#. This done by first
 * selectively picking out values from the windowed samples, and then
 * multiplying them by the filter matrix, producing 32 subband samples.
 */
void shine_window_filter_subband(const int16_t **buffer, int32_t s[SBLIMIT],
                                 int ch, shine_global_config *config,
                                 int stride) {
  int i;
  const int16_t *ptr = *buffer;

  /* replace 32 oldest samples with 32 new samples */
  for (i = 32; i--;) {
    config->subband.x[ch][i + config->subband.off[ch]] = (int32_t)*ptr * 65536;
    ptr += stride;
  }
  *buffer = ptr;

  config->kernels.filter(config->subband.x[ch], config->subband.off[ch], s);

  config->subband.off[ch] = (config->subband.off[ch] + 480) &
                            (HAN_SIZE - 1); /* offset is modulo (HAN_SIZE)*/
}
