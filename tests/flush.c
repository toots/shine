/* `shine_flush` drains the filterbank: the stream holds enough frames for a
 * decoder to output the last input sample. */

#include "helpers.h"

#include <stdio.h>

static int check_flush(const shine_config_t *config, const char *name) {
  shine_config_t copy = *config;
  stream_t stream = {NULL, 0, 0};
  shine_t encoder = shine_initialise(&copy);
  int samples_per_pass, input_samples, delay, frames, ok = 0;

  if (!encoder) {
    printf("FAIL %s: shine_initialise failed\n", name);
    return 0;
  }

  delay = shine_encoder_delay(encoder);
  if (delay != 528) {
    printf("FAIL %s: encoder delay %d, expected 528\n", name, delay);
    goto end;
  }

  samples_per_pass = shine_samples_per_pass(encoder);
  /* Ends short of a frame boundary, so that zero padding up to the next one
   * does not hide a missing drain. */
  input_samples = 21 * samples_per_pass - 100;

  encode_noise(encoder, config->wave.channels, input_samples, &stream);

  frames = walk_frames(&stream, config, name);
  if (frames < 0)
    goto end;

  if ((long)frames * samples_per_pass <
      input_samples + delay + MP3_DECODER_DELAY) {
    printf("FAIL %s: %d frames of %d samples cannot hold %d input samples "
           "plus encoder and decoder delay\n",
           name, frames, samples_per_pass, input_samples);
    goto end;
  }

  ok = 1;

end:
  stream_free(&stream);
  shine_close(encoder);
  return ok;
}

int main(void) { return run_all_configs(check_flush); }
