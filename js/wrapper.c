#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "layer3.h"

shine_t shine_js_init(int channels, int samplerate, int mode, int bitr) {
  shine_config_t config;
  config.wave.channels = channels;
  config.wave.samplerate = samplerate;

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.mpeg.mode = mode;
  config.mpeg.bitr = bitr;

  return shine_initialise(&config);
} 

unsigned char *shine_js_encode_float_buffer(shine_t s, float **data, long *written) {
  int16_t *buffer[2];
  int16_t  chan1[SHINE_MAX_SAMPLES], chan2[SHINE_MAX_SAMPLES];
  buffer[0] = chan1, buffer[1] = chan2;
  int chan, i;
  for (chan=0;chan<s->wave.channels;chan++)
    for (i=0;i<shine_samples_per_pass(s);i++)
      buffer[chan][i] = (int16_t)INT16_MAX*data[chan][i];

  return shine_encode_buffer(s, buffer, written);
}
