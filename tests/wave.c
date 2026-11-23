/* shineenc's WAV input: each case encodes a WAV file with shineenc and
 * compares the result with a reference encoding of the samples it should
 * have read. */

#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLERATE 44100
#define SAMPLES (3 * 1152 + 500)
#define UNKNOWN_LENGTH 0xffffffff

typedef struct {
  int channels;
  const int16_t *samples; /* interleaved, SAMPLES per channel */
  int data_length_unknown;
  int trailing_chunk_bytes;
  int zero_byte_rate;
  int zero_channels; /* in the fmt chunk only */
} wav_t;

static void put_u16(FILE *file, unsigned int value) {
  fputc(value & 0xff, file);
  fputc((value >> 8) & 0xff, file);
}

static void put_u32(FILE *file, unsigned long value) {
  put_u16(file, value & 0xffff);
  put_u16(file, (value >> 16) & 0xffff);
}

static int write_wav(const char *path, const wav_t *wav) {
  FILE *file = fopen(path, "wb");
  unsigned long data_length = SAMPLES * wav->channels * 2;
  unsigned int seed = 7;

  if (!file)
    return 0;

  fwrite("RIFF", 1, 4, file);
  put_u32(file, wav->data_length_unknown
                    ? UNKNOWN_LENGTH
                    : 4 + 24 + 8 + data_length + 8 + wav->trailing_chunk_bytes);
  fwrite("WAVEfmt ", 1, 8, file);
  put_u32(file, 16);
  put_u16(file, 1);
  put_u16(file, wav->zero_channels ? 0 : wav->channels);
  put_u32(file, SAMPLERATE);
  put_u32(file, wav->zero_byte_rate ? 0 : SAMPLERATE * wav->channels * 2);
  put_u16(file, wav->channels * 2);
  put_u16(file, 16);

  fwrite("data", 1, 4, file);
  put_u32(file, wav->data_length_unknown ? UNKNOWN_LENGTH : data_length);
  for (long i = 0; i < SAMPLES * wav->channels; i++)
    put_u16(file, (uint16_t)wav->samples[i]);

  if (wav->trailing_chunk_bytes) {
    fwrite("LIST", 1, 4, file);
    put_u32(file, wav->trailing_chunk_bytes);
    for (int i = 0; i < wav->trailing_chunk_bytes; i++) {
      seed = seed * 1103515245 + 12345;
      fputc(seed >> 24, file);
    }
  }

  return fclose(file) == 0;
}

static int read_file(const char *path, stream_t *stream) {
  FILE *file = fopen(path, "rb");
  long size;

  if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET)) {
    if (file)
      fclose(file);
    return 0;
  }

  stream->data = malloc(size ? size : 1);
  stream->size = stream->capacity = size;
  if (!stream->data || fread(stream->data, 1, size, file) != (size_t)size) {
    fclose(file);
    return 0;
  }
  return fclose(file) == 0;
}

/* Encodes `wav` with shineenc `options`. Returns 1 on success, 0 after
 * printing why. */
static int shineenc(const char *name, const char *options, const wav_t *wav,
                    stream_t *stream) {
  char input[128], output[128], arguments[512];
  int ok = 0, status;

  snprintf(input, sizeof(input), "wave-%s.wav", name);
  snprintf(output, sizeof(output), "wave-%s.mp3", name);
  snprintf(arguments, sizeof(arguments), "-q %s '%s' '%s'", options, input,
           output);

  if (!write_wav(input, wav))
    printf("FAIL %s: cannot write %s\n", name, input);
  else if ((status = run_shineenc(name, arguments)) != 0) {
    if (status > 0)
      printf("FAIL %s: shineenc %s exited with %d\n", name, arguments, status);
  } else if (!read_file(output, stream))
    printf("FAIL %s: cannot read %s\n", name, output);
  else
    ok = 1;

  remove(input);
  remove(output);
  return ok;
}

static int expect_frames(const char *name, const stream_t *stream,
                         enum modes mode) {
  shine_config_t config;
  int frames;
  /* Input frames, zero padded, plus the drain. */
  int expected =
      (SAMPLES + 1151) / 1152 + (528 + MP3_DECODER_DELAY + 1151) / 1152;

  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.samplerate = SAMPLERATE;
  config.wave.channels = mode == MONO ? PCM_MONO : PCM_STEREO;
  config.mpeg.mode = mode;

  frames = walk_frames(stream, &config, name);
  if (frames < 0)
    return 0;
  if (frames != expected) {
    printf("FAIL %s: %d frames, expected %d\n", name, frames, expected);
    return 0;
  }
  return 1;
}

static int expect_same(const char *name, const stream_t *stream,
                       const stream_t *reference) {
  if (stream->size != reference->size ||
      memcmp(stream->data, reference->data, stream->size)) {
    printf("FAIL %s: output differs from the reference encoding\n", name);
    return 0;
  }
  return 1;
}

/* Expects shineenc to reject `wav` with exit status 1. */
static int check_rejected(const char *name, const wav_t *wav) {
  char input[128], arguments[512];
  int status;

  snprintf(input, sizeof(input), "wave-%s.wav", name);
  snprintf(arguments, sizeof(arguments), "-q '%s' wave-%s.mp3 2> /dev/null",
           input, name);

  if (!write_wav(input, wav)) {
    printf("FAIL %s: cannot write %s\n", name, input);
    return 0;
  }
  status = run_shineenc(name, arguments);
  remove(input);
  snprintf(input, sizeof(input), "wave-%s.mp3", name);
  remove(input);

  if (status >= 0 && status != 1)
    printf("FAIL %s: exited with %d, expected 1\n", name, status);
  return status == 1;
}

static int16_t mono[SAMPLES];
static int16_t stereo[2 * SAMPLES];

/* Runs `wav` with `options` against the encoding of `reference`. */
static int check(const char *name, const char *options, const wav_t *wav,
                 const wav_t *reference, enum modes mode) {
  stream_t stream = {NULL, 0, 0}, expected = {NULL, 0, 0};
  int ok = shineenc(name, options, wav, &stream) &&
           expect_frames(name, &stream, mode) &&
           shineenc("reference", "", reference, &expected) &&
           expect_same(name, &stream, &expected);

  stream_free(&stream);
  stream_free(&expected);
  return ok;
}

int main(void) {
  const wav_t plain = {.channels = 1, .samples = mono};
  const wav_t trailing_chunk = {
      .channels = 1, .samples = mono, .trailing_chunk_bytes = 8000};
  const wav_t unknown_length = {
      .channels = 1, .samples = mono, .data_length_unknown = 1};
  const wav_t dual_mono = {.channels = 2, .samples = stereo};
  const wav_t zero_byte_rate = {
      .channels = 1, .samples = mono, .zero_byte_rate = 1};
  const wav_t zero_channels = {
      .channels = 1, .samples = mono, .zero_channels = 1};
  int passed = 0, checked = 0;
  unsigned int seed = 1;

  for (int i = 0; i < SAMPLES; i++) {
    seed = seed * 1103515245 + 12345;
    mono[i] = (int16_t)(seed >> 16);
    stereo[2 * i] = stereo[2 * i + 1] = mono[i];
  }

  checked++;
  passed += check("trailing-chunk", "", &trailing_chunk, &plain, MONO);
  checked++;
  passed += check("unknown-length", "", &unknown_length, &plain, MONO);

  /* L = R, so the downmix is the mono signal itself. */
  checked++;
  passed += check("force-mono", "-m", &dual_mono, &plain, MONO);

  /* The byte rate is redundant: encode as if it were right. */
  checked++;
  passed += check("zero-byte-rate", "", &zero_byte_rate, &plain, MONO);
  checked++;
  passed += check_rejected("zero-channels", &zero_channels);

  printf("%d/%d cases passed\n", passed, checked);
  return passed == checked && checked > 0 ? 0 : 1;
}
