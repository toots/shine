/* Every frame header is valid, matches the configuration and starts where the
 * previous frame ends, and the frames tile the stream exactly. */

#include "helpers.h"

#include <stdio.h>

static int check_framing(const shine_config_t *config, const char *name) {
  shine_config_t copy = *config;
  stream_t stream = {NULL, 0, 0};
  shine_t encoder = shine_initialise(&copy);
  int frames;

  if (!encoder) {
    printf("FAIL %s: shine_initialise failed\n", name);
    return 0;
  }

  encode_noise(encoder, config->wave.channels,
               20 * shine_samples_per_pass(encoder) + 1, &stream);
  frames = walk_frames(&stream, config, name);

  stream_free(&stream);
  shine_close(encoder);
  return frames > 0;
}

int main(void) { return run_all_configs(check_framing); }
