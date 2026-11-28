/* Gapless decoding: FFmpeg trims a stream that starts with the Info frame to
 * the input, sample for sample. Skipped when ffmpeg is not in the PATH. */

#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP 77
#define MAX_LAG 2

typedef struct {
  int samplerate, bitrate, quality;
  enum shine_mode mode;
} gapless_case_t;

/* Magic circle oscillator, as in tests/golden.c. */
static void tone(int16_t *samples, long count, int channels) {
  int64_t x = 16000, y = 0;

  for (long i = 0; i < count; i++) {
    x -= 3000 * y / 65536;
    y += 3000 * x / 65536;
    for (int c = 0; c < channels; c++)
      samples[i * channels + c] = (int16_t)y;
  }
}

/* Correlation of the first channel of `decoded`, shifted by `lag`, with the
 * input. */
static double correlation(const int16_t *input, const int16_t *decoded,
                          long count, int channels, int lag) {
  double cross = 0, energy = 0;

  for (long i = MAX_LAG; i < count - MAX_LAG; i++) {
    cross += (double)input[i * channels] * decoded[(i + lag) * channels];
    energy += (double)input[i * channels] * input[i * channels];
  }
  return cross / energy;
}

static int check_gapless(const gapless_case_t *test) {
  shine_config_t config;
  shine_t encoder;
  stream_t stream = {NULL, 0, 0};
  const int channels = test->mode == SHINE_MONO ? 1 : 2;
  /* Not a whole number of frames. */
  const long count = test->samplerate + 123;
  long expected, capacity, decoded_count = 0;
  int16_t *samples = malloc(count * channels * sizeof(int16_t));
  int16_t *decoded = NULL;
  unsigned char *info;
  int info_size, ok = 0;
  char name[64];
  FILE *file;

  snprintf(name, sizeof(name), "%d Hz %d kbps mode %d quality %d",
           test->samplerate, test->bitrate, test->mode, test->quality);

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

  tone(samples, count, channels);
  encode_samples(encoder, channels, samples, count, &stream);
  info = shine_info_frame(encoder, NULL, count, &info_size);

  file = fopen("gapless.mp3", "wb");
  if (!file || fwrite(info, 1, info_size, file) != (size_t)info_size ||
      fwrite(stream.data, 1, stream.size, file) != stream.size ||
      fclose(file)) {
    printf("FAIL %s: cannot write gapless.mp3\n", name);
    goto end;
  }

  file = popen("ffmpeg -nostdin -v error -i gapless.mp3 -f s16le "
               "-acodec pcm_s16le -",
               "r");
  if (!file) {
    printf("FAIL %s: cannot run ffmpeg\n", name);
    goto end;
  }
  /* Room for every sample a wrong trim could add, read to the end so that
   * ffmpeg exits normally. */
  capacity = count + 8 * 1152;
  decoded = malloc(capacity * channels * sizeof(int16_t));
  if (decoded)
    while (!feof(file) && !ferror(file) && decoded_count < capacity)
      decoded_count +=
          fread(decoded + decoded_count * channels, channels * sizeof(int16_t),
                capacity - decoded_count, file);
  for (long extra = 0; fgetc(file) != EOF;)
    if (++extra % (channels * sizeof(int16_t)) == 0)
      decoded_count++;
  if (pclose(file) != 0 || !decoded) {
    printf("FAIL %s: ffmpeg failed to decode\n", name);
    goto end;
  }

  /* At the rate of the stream, rounded as the encoder converts. */
  expected = (long)(((int64_t)count * shine_samplerate(encoder) +
                     test->samplerate / 2) /
                    test->samplerate);
  if (decoded_count != expected) {
    printf("FAIL %s: decoded %ld samples, expected %ld\n", name, decoded_count,
           expected);
    goto end;
  }

  if (shine_samplerate(encoder) == test->samplerate) {
    double at_zero = correlation(samples, decoded, count, channels, 0);

    for (int lag = -MAX_LAG; lag <= MAX_LAG; lag++)
      if (lag &&
          correlation(samples, decoded, count, channels, lag) >= at_zero) {
        printf("FAIL %s: decoded samples line up at lag %d\n", name, lag);
        goto end;
      }
  }

  printf("%s: %ld samples\n", name, decoded_count);
  ok = 1;

end:
  remove("gapless.mp3");
  free(samples);
  free(decoded);
  stream_free(&stream);
  if (encoder)
    shine_close(encoder);
  return ok;
}

int main(void) {
  static const gapless_case_t cases[] = {
      {44100, 128, 0, SHINE_JOINT_STEREO}, {44100, 128, 1, SHINE_JOINT_STEREO},
      {48000, 64, 1, SHINE_MONO},          {32000, 96, 0, SHINE_STEREO},
      {22050, 64, 1, SHINE_JOINT_STEREO},  {16000, 32, 0, SHINE_MONO},
      {11025, 16, 1, SHINE_MONO},          {44100, 32, 1, SHINE_JOINT_STEREO},
  };
  const int count = sizeof(cases) / sizeof(*cases);
  int passed = 0;

  if (system("ffmpeg -version > /dev/null 2>&1") != 0) {
    printf("SKIP: ffmpeg not found\n");
    return SKIP;
  }

  for (int i = 0; i < count; i++)
    passed += check_gapless(&cases[i]);

  printf("%d/%d cases passed\n", passed, count);
  return passed == count ? 0 : 1;
}
