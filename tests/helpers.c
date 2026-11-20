/* The header tables are duplicated here on purpose, as an oracle independent
 * of the library's. */

#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int samplerates[3][3] = {
    {11025, 12000, 8000},  /* MPEG-2.5 */
    {22050, 24000, 16000}, /* MPEG-II */
    {44100, 48000, 32000}, /* MPEG-I */
};

static const int bitrates[2][15] = {
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
};

static const int all_bitrates[] = {8,   16,  24,  32,  40,  48,  56,
                                   64,  80,  96,  112, 128, 144, 160,
                                   192, 224, 256, 320};

static void *checked_realloc(void *pointer, size_t size) {
  pointer = realloc(pointer, size);
  if (!pointer) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  return pointer;
}

static void append(stream_t *stream, const unsigned char *data, int size) {
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

/* Loud, so that the encoder runs out of bits. */
static int16_t noise(unsigned int *seed) {
  *seed = *seed * 1103515245 + 12345;
  return (int16_t)(*seed >> 16);
}

void encode_noise(shine_t encoder, int channels, int input_samples,
                  stream_t *stream) {
  int samples_per_pass = shine_samples_per_pass(encoder);
  size_t buffer_size = samples_per_pass * channels * sizeof(int16_t);
  int16_t *buffer = checked_realloc(NULL, buffer_size);
  unsigned int seed = 1;
  unsigned char *data;
  int written;

  for (int done = 0; done < input_samples; done += samples_per_pass) {
    int samples = input_samples - done < samples_per_pass
                      ? input_samples - done
                      : samples_per_pass;
    memset(buffer, 0, buffer_size);
    for (int i = 0; i < samples * channels; i++)
      buffer[i] = noise(&seed);
    data = shine_encode_buffer_interleaved(encoder, buffer, &written);
    append(stream, data, written);
  }
  free(buffer);

  data = shine_flush(encoder, &written);
  append(stream, data, written);
}

static int version_index(int version_bits) {
  switch (version_bits) {
  case MPEG_25:
    return 0;
  case MPEG_II:
    return 1;
  case MPEG_I:
    return 2;
  default:
    return -1;
  }
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
    if (index < 0 || ((header[1] >> 1) & 3) != LAYER_III ||
        (header[2] >> 4) == 0 || (header[2] >> 4) == 15 ||
        ((header[2] >> 2) & 3) == 3) {
      printf("FAIL %s: invalid frame header at byte %zu\n", name, position);
      return -1;
    }

    bitrate = bitrates[index == 2][header[2] >> 4];
    samplerate = samplerates[index][(header[2] >> 2) & 3];
    padding = (header[2] >> 1) & 1;
    mode = header[3] >> 6;

    if (bitrate != config->mpeg.bitr ||
        samplerate != config->wave.samplerate || mode != config->mpeg.mode) {
      printf("FAIL %s: frame %d header says %d Hz %d kbps mode %d\n", name,
             frames, samplerate, bitrate, mode);
      return -1;
    }

    position +=
        (version == MPEG_I ? 144 : 72) * bitrate * 1000 / samplerate + padding;
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
                      enum modes mode) {
  shine_config_t config;
  char name[64];

  snprintf(name, sizeof(name), "%d Hz %d kbps mode %d", samplerate, bitrate,
           mode);

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.samplerate = samplerate;
  config.wave.channels = mode == MONO ? PCM_MONO : PCM_STEREO;
  config.mpeg.bitr = bitrate;
  config.mpeg.mode = mode;

  return test(&config, name);
}

int run_all_configs(config_test_t test) {
  static const enum modes modes[] = {MONO, STEREO, JOINT_STEREO,
                                     DUAL_CHANNEL};
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
    printf("FAIL: checked %d configurations, expected %d\n", checked,
           expected);
    return 1;
  }

  return passed == checked ? 0 : 1;
}
