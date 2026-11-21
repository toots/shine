/* Output level: tones encoded by the library and decoded by FFmpeg come out
 * at the input level. At quality 1, tones across the spectrum check that
 * scalefactors are decoded with the amplification they were encoded with
 * (doc/psy/tests.md §1). Skipped when ffmpeg is not in the PATH. */

#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP 77
#define DECODED_DELAY (528 + MP3_DECODER_DELAY)
#define TOLERANCE 0.01

typedef struct {
  int samplerate, bitrate;
  enum shine_mode mode;
  int quality;
  int64_t step; /* tone frequency, step / 65536 / (2 pi) * samplerate */
} level_case_t;

/* Magic circle oscillator, as in tests/golden.c. */
static void tone(int16_t *samples, long count, int channels, int64_t step) {
  int64_t x = 16000, y = 0;

  for (long i = 0; i < count; i++) {
    x -= step * y / 65536;
    y += step * x / 65536;
    for (int c = 0; c < channels; c++)
      samples[i * channels + c] = (int16_t)y;
  }
}

/* Least squares gain of decoded against input, on the first channel, away
 * from both ends. */
static double fit_gain(const int16_t *input, long count, int channels,
                       const int16_t *decoded, long decoded_count) {
  double cross = 0, energy = 0;

  for (long i = count / 8; i < count - count / 8; i++) {
    long j = i + DECODED_DELAY;
    if (j >= decoded_count)
      break;
    cross += (double)input[i * channels] * decoded[j * channels];
    energy += (double)input[i * channels] * input[i * channels];
  }
  return energy > 0 ? cross / energy : 0;
}

static int check_level(const level_case_t *test) {
  shine_config_t config;
  shine_t encoder;
  stream_t stream = {NULL, 0, 0};
  int channels = test->mode == SHINE_MONO ? 1 : 2;
  long count = 2L * test->samplerate, decoded_count = 0, capacity;
  int16_t *samples = malloc(count * channels * sizeof(int16_t));
  int16_t *decoded = NULL;
  char name[96], command[256];
  FILE *file;
  double gain;
  int ok = 0;

  snprintf(name, sizeof(name), "%d Hz %d kbps mode %d quality %d step %d",
           test->samplerate, test->bitrate, test->mode, test->quality,
           (int)test->step);

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.samplerate = test->samplerate;
  config.wave.channels = channels;
  config.mpeg.bitrate = test->bitrate;
  config.mpeg.mode = test->mode;
  config.mpeg.quality = test->quality;

  encoder = shine_initialise(&config);
  if (!samples || !encoder) {
    printf("FAIL %s: cannot initialise\n", name);
    goto end;
  }

  /* -6 dBFS. */
  tone(samples, count, channels, test->step);
  encode_samples(encoder, channels, samples, count, &stream);

  file = fopen("level.mp3", "wb");
  if (!file || fwrite(stream.data, 1, stream.size, file) != stream.size ||
      fclose(file)) {
    printf("FAIL %s: cannot write level.mp3\n", name);
    goto end;
  }

  snprintf(command, sizeof(command),
           "ffmpeg -v error -i level.mp3 -f s16le -acodec pcm_s16le -");
  file = popen(command, "r");
  if (!file) {
    printf("FAIL %s: cannot run ffmpeg\n", name);
    goto end;
  }
  capacity = count + 4 * DECODED_DELAY;
  decoded = malloc(capacity * channels * sizeof(int16_t));
  if (decoded)
    decoded_count = fread(decoded, channels * sizeof(int16_t), capacity, file);
  if (pclose(file) != 0 || decoded_count == 0) {
    printf("FAIL %s: ffmpeg failed to decode\n", name);
    goto end;
  }

  gain = fit_gain(samples, count, channels, decoded, decoded_count);
  if (gain < 1 - TOLERANCE || gain > 1 + TOLERANCE) {
    printf("FAIL %s: decoded level is %.4f times the input\n", name, gain);
    goto end;
  }
  printf("%s: level %.4f\n", name, gain);
  ok = 1;

end:
  remove("level.mp3");
  free(samples);
  free(decoded);
  stream_free(&stream);
  if (encoder)
    shine_close(encoder);
  return ok;
}

int main(void) {
  /* A tone at about 1/40th of the samplerate at quality 0; at quality 1,
   * tones from 1% to 35% of the Nyquist frequency. */
  static const level_case_t cases[] = {
      {44100, 128, SHINE_STEREO, 0, 10000},
      {48000, 320, SHINE_JOINT_STEREO, 0, 10000},
      {32000, 64, SHINE_MONO, 0, 10000},
      {22050, 64, SHINE_MONO, 0, 10000},
      {16000, 32, SHINE_DUAL_CHANNEL, 0, 10000},
      {8000, 16, SHINE_MONO, 0, 10000},
      {44100, 128, SHINE_JOINT_STEREO, 1, 2059},
      {44100, 128, SHINE_JOINT_STEREO, 1, 10294},
      {44100, 128, SHINE_JOINT_STEREO, 1, 41175},
      {44100, 128, SHINE_JOINT_STEREO, 1, 72057},
      {22050, 64, SHINE_MONO, 1, 2059},
      {22050, 64, SHINE_MONO, 1, 10294},
      {22050, 64, SHINE_MONO, 1, 41175},
      {22050, 64, SHINE_MONO, 1, 72057},
      {8000, 16, SHINE_MONO, 1, 10294},
      {8000, 16, SHINE_MONO, 1, 41175},
  };
  const int count = sizeof(cases) / sizeof(*cases);
  int passed = 0;

  if (system("ffmpeg -version > /dev/null 2>&1") != 0) {
    printf("SKIP: ffmpeg not found\n");
    return SKIP;
  }

  for (int i = 0; i < count; i++)
    passed += check_level(&cases[i]);

  printf("%d/%d cases passed\n", passed, count);
  return passed == count ? 0 : 1;
}
