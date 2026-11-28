/* The header tables are duplicated here on purpose, as an oracle independent
 * of the library's. */

#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static const int samplerates[3][3] = {
    {11025, 12000, 8000},  /* MPEG-2.5 */
    {22050, 24000, 16000}, /* MPEG-II */
    {44100, 48000, 32000}, /* MPEG-I */
};

static const int bitrates[2][15] = {
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
};

static const int all_bitrates[] = {8,  16,  24,  32,  40,  48,  56,  64,  80,
                                   96, 112, 128, 144, 160, 192, 224, 256, 320};

static void *checked_realloc(void *pointer, size_t size) {
  pointer = realloc(pointer, size);
  if (!pointer) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  return pointer;
}

static void append(stream_t *stream, const unsigned char *data, int size) {
  /* A call may return no frame (doc/psy/reservoir.md §4). */
  if (!size)
    return;
  if (stream->size + size > stream->capacity) {
    stream->capacity = 2 * (stream->size + size);
    stream->data = checked_realloc(stream->data, stream->capacity);
  }
  memcpy(stream->data + stream->size, data, size);
  stream->size += size;
}

void stream_free(stream_t *stream) {
  free(stream->data);
  stream->data = NULL;
  stream->size = stream->capacity = 0;
}

int run_shineenc(const char *name, const char *arguments) {
  const char *binary = getenv("SHINEENC");
  char command[1024];
  int status;

  if (!binary) {
    printf("FAIL %s: SHINEENC is not set\n", name);
    return -1;
  }

  snprintf(command, sizeof(command), "'%s' %s", binary, arguments);
  status = system(command);
  if (status == -1 || !WIFEXITED(status)) {
    printf("FAIL %s: %s did not exit normally\n", name, command);
    return -1;
  }
  return WEXITSTATUS(status);
}

/* Loud, so that the encoder runs out of bits. */
int16_t noise(unsigned int *seed) {
  *seed = *seed * 1103515245 + 12345;
  return (int16_t)(*seed >> 16);
}

void encode_samples(shine_t encoder, int channels, const int16_t *samples,
                    int input_samples, stream_t *stream) {
  int samples_per_pass = shine_samples_per_pass(encoder);
  size_t buffer_size = samples_per_pass * channels * sizeof(int16_t);
  int16_t *buffer = checked_realloc(NULL, buffer_size);
  unsigned char *data;
  int written;

  for (int done = 0; done < input_samples; done += samples_per_pass) {
    int pass = input_samples - done < samples_per_pass ? input_samples - done
                                                       : samples_per_pass;
    memset(buffer, 0, buffer_size);
    memcpy(buffer, samples + (size_t)done * channels,
           pass * channels * sizeof(int16_t));
    data = shine_encode_buffer_interleaved(encoder, buffer, &written);
    append(stream, data, written);
  }
  free(buffer);

  data = shine_flush(encoder, &written);
  append(stream, data, written);
}

void encode_noise(shine_t encoder, int channels, int input_samples,
                  stream_t *stream) {
  int16_t *samples =
      checked_realloc(NULL, (size_t)input_samples * channels * sizeof(int16_t));
  unsigned int seed = 1;

  for (long i = 0; i < (long)input_samples * channels; i++)
    samples[i] = noise(&seed);
  encode_samples(encoder, channels, samples, input_samples, stream);
  free(samples);
}

static int version_index(int version_bits) {
  switch (version_bits) {
  case SHINE_MPEG_25:
    return 0;
  case SHINE_MPEG_II:
    return 1;
  case SHINE_MPEG_I:
    return 2;
  default:
    return -1;
  }
}

size_t info_frame_bytes(const stream_t *stream) {
  const unsigned char *header = stream->data;
  int version, index, bitrate, side_info;

  if (stream->size < 4 || header[0] != 0xff || (header[1] & 0xe0) != 0xe0)
    return 0;
  version = (header[1] >> 3) & 3;
  index = version_index(version);
  if (index < 0 || (header[2] >> 4) == 0 || (header[2] >> 4) == 15 ||
      ((header[2] >> 2) & 3) == 3)
    return 0;
  side_info = version == SHINE_MPEG_I ? (header[3] >> 6 == SHINE_MONO ? 17 : 32)
                                      : (header[3] >> 6 == SHINE_MONO ? 9 : 17);
  if (stream->size < (size_t)4 + side_info + 4 ||
      memcmp(header + 4 + side_info, "Info", 4))
    return 0;
  bitrate = bitrates[index == 2][header[2] >> 4];
  return (version == SHINE_MPEG_I ? 144 : 72) * bitrate * 1000 /
             samplerates[index][(header[2] >> 2) & 3] +
         ((header[2] >> 1) & 1);
}

int walk_frames(const stream_t *stream, const shine_config_t *config,
                const char *name) {
  size_t position = 0;
  int frames = 0;

  while (position < stream->size) {
    const unsigned char *header = stream->data + position;
    int version, index, bitrate, samplerate, padding, mode;

    if (stream->size - position < 4 || header[0] != 0xff ||
        (header[1] & 0xe0) != 0xe0) {
      printf("FAIL %s: no frame header at byte %zu (frame %d, size %zu)\n",
             name, position, frames, stream->size);
      return -1;
    }

    version = (header[1] >> 3) & 3;
    index = version_index(version);
    if (index < 0 || ((header[1] >> 1) & 3) != 1 /* Layer III */ ||
        (header[2] >> 4) == 0 || (header[2] >> 4) == 15 ||
        ((header[2] >> 2) & 3) == 3) {
      printf("FAIL %s: invalid frame header at byte %zu\n", name, position);
      return -1;
    }

    bitrate = bitrates[index == 2][header[2] >> 4];
    samplerate = samplerates[index][(header[2] >> 2) & 3];
    padding = (header[2] >> 1) & 1;
    mode = header[3] >> 6;

    if (bitrate != config->mpeg.bitrate ||
        samplerate != config->wave.samplerate ||
        mode != (int)config->mpeg.mode) {
      printf("FAIL %s: frame %d header says %d Hz %d kbps mode %d\n", name,
             frames, samplerate, bitrate, mode);
      return -1;
    }

    position +=
        (version == SHINE_MPEG_I ? 144 : 72) * bitrate * 1000 / samplerate +
        padding;
    frames++;
  }

  if (position != stream->size) {
    printf("FAIL %s: last frame ends at byte %zu, stream has %zu\n", name,
           position, stream->size);
    return -1;
  }

  return frames;
}

static int run_config(config_test_t test, int samplerate, int bitrate,
                      enum shine_mode mode) {
  shine_config_t config;
  char name[64];

  snprintf(name, sizeof(name), "%d Hz %d kbps mode %d", samplerate, bitrate,
           mode);

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.samplerate = samplerate;
  config.wave.channels = mode == SHINE_MONO ? 1 : 2;
  config.mpeg.bitrate = bitrate;
  config.mpeg.mode = mode;
  /* Tests of the stream at the input rate, one frame per call. */
  config.mpeg.quality = 0;

  return test(&config, name);
}

int run_all_configs(config_test_t test) {
  static const enum shine_mode modes[] = {
      SHINE_MONO, SHINE_STEREO, SHINE_JOINT_STEREO, SHINE_DUAL_CHANNEL};
  const int mode_count = sizeof(modes) / sizeof(*modes);
  /* Valid samplerate/bitrate pairs, each in every mode. */
  const int expected = 108 * mode_count;
  int checked = 0, passed = 0;

  for (int v = 0; v < 3; v++)
    for (int s = 0; s < 3; s++)
      for (size_t b = 0; b < sizeof(all_bitrates) / sizeof(*all_bitrates);
           b++) {
        if (shine_check_config(samplerates[v][s], all_bitrates[b]) < 0)
          continue;

        for (int m = 0; m < mode_count; m++) {
          checked++;
          passed +=
              run_config(test, samplerates[v][s], all_bitrates[b], modes[m]);
        }
      }

  printf("%d/%d configurations passed\n", passed, checked);

  if (checked != expected) {
    printf("FAIL: checked %d configurations, expected %d\n", checked, expected);
    return 1;
  }

  return passed == checked ? 0 : 1;
}
