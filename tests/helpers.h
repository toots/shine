#ifndef TESTS_HELPERS_H
#define TESTS_HELPERS_H

#include <layer3.h>
#include <stddef.h>

#define MP3_DECODER_DELAY 529

typedef struct {
  unsigned char *data;
  size_t size;
  size_t capacity;
} stream_t;

/* A test for one configuration: returns 1 on success, 0 after printing a
 * line starting with "FAIL <name>:". */
typedef int (*config_test_t)(const shine_config_t *config, const char *name);

/* Runs `test` on every supported samplerate, bitrate and mode. Returns the
 * process exit status: failure if any configuration failed or if fewer
 * configurations than expected were tested. */
int run_all_configs(config_test_t test);

/* Encodes `input_samples` samples per channel of loud white noise, followed
 * by `shine_flush`, into `stream`. The last pass is zero padded, as callers
 * do with real input. */
void encode_noise(shine_t encoder, int channels, int input_samples,
                  stream_t *stream);

void stream_free(stream_t *stream);

/* Walks the frame headers of `stream`, checking that each one is where the
 * previous frame ends, matches `config`, and that the last frame ends with
 * the stream. Returns the number of frames, or -1 after printing why. */
int walk_frames(const stream_t *stream, const shine_config_t *config,
                const char *name);

#endif
