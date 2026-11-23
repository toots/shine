/* `shine_initialise` rejects configurations whose channel count or mode it
 * cannot encode. */

#include "helpers.h"

#include <stdio.h>

typedef struct {
  int channels;
  int mode;
  int valid;
} config_case_t;

int main(void) {
  static const config_case_t cases[] = {
      {1, SHINE_MONO, 1},
      {2, SHINE_STEREO, 1},
      {2, SHINE_JOINT_STEREO, 1},
      {2, SHINE_DUAL_CHANNEL, 1},
      {0, SHINE_MONO, 0},
      {3, SHINE_STEREO, 0},
      {-1, SHINE_STEREO, 0},
      {1, SHINE_STEREO, 0},
      {2, SHINE_MONO, 0},
      {2, 4, 0},
      {2, -1, 0},
  };
  const int count = sizeof(cases) / sizeof(*cases);
  int passed = 0;

  for (int i = 0; i < count; i++) {
    shine_config_t config;
    shine_t encoder;

    shine_set_config_mpeg_defaults(&config.mpeg);
    config.wave.samplerate = 44100;
    config.wave.channels = cases[i].channels;
    config.mpeg.mode = (enum shine_mode)cases[i].mode;

    encoder = shine_initialise(&config);
    if (!encoder != !cases[i].valid)
      printf("FAIL %d channels mode %d: %s\n", cases[i].channels, cases[i].mode,
             encoder ? "accepted" : "rejected");
    else
      passed++;
    if (encoder)
      shine_close(encoder);
  }

  printf("%d/%d cases passed\n", passed, count);
  return passed == count ? 0 : 1;
}
