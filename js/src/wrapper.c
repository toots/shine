/* Builds an encoder from plain arguments, so that JavaScript need not lay out
 * shine_config_t in memory. */

#include "layer3.h"

shine_t shine_js_init(int channels, int samplerate, int mode, int bitrate) {
  shine_config_t config;

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.channels = channels;
  config.wave.samplerate = samplerate;
  config.mpeg.mode = mode;
  config.mpeg.bitr = bitrate;

  return shine_initialise(&config);
}
