/* The bit reservoir, read back from the stream (doc/psy/reservoir.md): every
 * frame's main_data_begin is what earlier frames left unused, within the
 * limit, no frame uses more than it has, every call returns whole frames,
 * and the lowest quality level has no reservoir. */

#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>

#define FRAMES 60

typedef struct {
  const unsigned char *data;
  size_t bit;
} reader_t;

static unsigned read_bits(reader_t *reader, int count) {
  unsigned value = 0;

  for (; count--; reader->bit++)
    value = value << 1 |
            ((reader->data[reader->bit >> 3] >> (7 - (reader->bit & 7))) & 1);
  return value;
}

/* Quiet tone, with full-scale noise two frames in nine: granules that need
 * few bits, then granules that need many. */
static void fill(int16_t *samples, int count, int channels, int frame,
                 unsigned *seed) {
  for (int i = 0; i < count; i++)
    for (int ch = 0; ch < channels; ch++)
      samples[i * channels + ch] = frame % 9 >= 7
                                       ? noise(seed)
                                       : (int16_t)(((i + 7 * ch) % 32 - 16) * 20);
}

static int used_reservoir;

static int check_stream(const shine_config_t *config, const char *name,
                        int quality) {
  shine_config_t copy = *config;
  const int mpeg1 = config->wave.samplerate >= 32000;
  const int channels = config->wave.channels;
  const int padded = (mpeg1 ? 144 : 72) * config->mpeg.bitrate * 1000 %
                         config->wave.samplerate != 0;
  const int side_bytes =
      4 + (mpeg1 ? (channels == 1 ? 17 : 32) : (channels == 1 ? 9 : 17));
  stream_t stream = {NULL, 0, 0};
  shine_t encoder;
  int16_t *samples;
  unsigned seed = 1;
  size_t position = 0;
  long reservoir = 0;
  int samples_per_pass, frames = 0, ok = 0;

  copy.mpeg.quality = quality;
  encoder = shine_initialise(&copy);
  if (!encoder) {
    printf("FAIL %s quality %d: shine_initialise failed\n", name, quality);
    return 0;
  }
  samples_per_pass = shine_samples_per_pass(encoder);
  samples = malloc(sizeof(*samples) * samples_per_pass * channels);
  if (!samples)
    exit(2);

  for (int frame = 0; frame <= FRAMES; frame++) {
    unsigned char *data;
    int written;

    if (frame < FRAMES) {
      fill(samples, samples_per_pass, channels, frame, &seed);
      data = shine_encode_buffer_interleaved(encoder, samples, &written);
    } else
      data = shine_flush(encoder, &written);
    if (written && (data[0] != 0xff || (data[1] & 0xe0) != 0xe0)) {
      printf("FAIL %s quality %d: call %d does not return whole frames\n",
             name, quality, frame);
      goto end;
    }
    if (written) {
      stream.data = realloc(stream.data, stream.size + written);
      if (!stream.data)
        exit(2);
      for (int i = 0; i < written; i++)
        stream.data[stream.size + i] = data[i];
      stream.size += written;
    }
  }

  if (walk_frames(&stream, config, name) < FRAMES)
    goto end;

  while (position < stream.size) {
    const unsigned char *header = stream.data + position;
    const int padding = (header[2] >> 1) & 1;
    const int frame_bytes = (mpeg1 ? 144 : 72) * config->mpeg.bitrate * 1000 /
                                config->wave.samplerate +
                            padding;
    const long capacity = 8L * (frame_bytes - side_bytes);
    long limit = 8L * (mpeg1 ? 511 : 255), used = 0;
    reader_t reader = {header + 4, 0};
    unsigned main_data_begin = read_bits(&reader, mpeg1 ? 9 : 8);

    /* The longest frame of the stream. */
    if (limit > 7680 - 8L * (frame_bytes - padding + padded))
      limit = 7680 - 8L * (frame_bytes - padding + padded);
    if (limit < 0 || quality == 0)
      limit = 0;

    read_bits(&reader, mpeg1 ? (channels == 1 ? 5 : 3) : channels);
    if (mpeg1)
      read_bits(&reader, 4 * channels);
    for (int gr = 0; gr < (mpeg1 ? 2 : 1); gr++)
      for (int ch = 0; ch < channels; ch++) {
        used += read_bits(&reader, 12);
        reader.bit += mpeg1 ? 47 : 51;
      }

    if (8L * main_data_begin != reservoir) {
      printf("FAIL %s quality %d frame %d: main_data_begin %u, %ld bits were "
             "left\n",
             name, quality, frames, main_data_begin, reservoir);
      goto end;
    }
    if (reservoir > limit) {
      printf("FAIL %s quality %d frame %d: reservoir of %ld bits, limit %ld\n",
             name, quality, frames, reservoir, limit);
      goto end;
    }
    if (used > reservoir + capacity) {
      printf("FAIL %s quality %d frame %d: uses %ld bits of %ld\n", name,
             quality, frames, used, reservoir + capacity);
      goto end;
    }
    /* Bits a frame leaves beyond the limit are ancillary. */
    reservoir += capacity - used;
    if (reservoir > limit)
      reservoir = limit & ~7L;
    reservoir &= ~7L;
    used_reservoir += main_data_begin > 0;
    position += frame_bytes;
    frames++;
  }
  ok = 1;

end:
  free(samples);
  stream_free(&stream);
  shine_close(encoder);
  return ok;
}

static int check_reservoir(const shine_config_t *config, const char *name) {
  int before = used_reservoir, ok;

  ok = check_stream(config, name, 0);
  if (used_reservoir != before) {
    printf("FAIL %s: quality 0 has a reservoir\n", name);
    ok = 0;
  }
  return check_stream(config, name, 1) && ok;
}

int main(void) {
  int status = run_all_configs(check_reservoir);

  /* The checks say nothing if no frame ever drew on a reservoir. */
  printf("%d frames began in an earlier frame\n", used_reservoir);
  if (used_reservoir < 1000) {
    printf("FAIL: the reservoir was hardly used\n");
    return 1;
  }
  return status;
}
