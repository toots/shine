#include "layer3.h"
#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

size_t shine_js_int16_len() {
  return sizeof(int16_t);
}

size_t shine_js_ptr_len() {
  return sizeof(void*);
}

shine_t shine_js_init(int channels, int samplerate, int mode, int bitr) {
  shine_config_t config;
  config.wave.channels = channels;
  config.wave.samplerate = samplerate;

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.mpeg.mode = mode;
  config.mpeg.bitr = bitr;

  return shine_initialise(&config);
}
