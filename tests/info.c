/* `shine_info_frame`: a frame holding a Xing header and LAME's extension
 * that describe the stream, at both quality levels. */

#include "helpers.h"

#include <stdio.h>
#include <string.h>

#define INPUT_SAMPLES 12345

static unsigned long get_be(const unsigned char *data, int bytes) {
  unsigned long value = 0;

  for (int i = 0; i < bytes; i++)
    value = value << 8 | data[i];
  return value;
}

/* CRC-16/ARC, as decoders check it, from a table built on the fly. */
static unsigned crc16(const unsigned char *data, size_t size) {
  static unsigned table[256];
  unsigned crc = 0;

  if (!table[1])
    for (unsigned i = 0; i < 256; i++) {
      unsigned value = i;
      for (int bit = 0; bit < 8; bit++)
        value = value & 1 ? (value >> 1) ^ 0xa001 : value >> 1;
      table[i] = value;
    }
  for (size_t i = 0; i < size; i++)
    crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xff];
  return crc;
}

/* Header and side information bytes before the Xing header. */
static int xing_offset(const unsigned char *header) {
  int mpeg1 = ((header[1] >> 3) & 3) == SHINE_MPEG_I;
  int mono = header[3] >> 6 == SHINE_MONO;

  return 4 + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
}

typedef struct {
  long frames, bytes, delay, padding;
  unsigned music_crc;
} expected_t;

static int check_frame(const unsigned char *frame, int size, const char *format,
                       const expected_t *expected, int complete,
                       const char *name) {
  const unsigned char *xing = frame + xing_offset(frame), *lame;
  unsigned long flags = get_be(xing + 4, 4);
  const unsigned long all_flags = 0x7; /* frames, bytes, seek table */
  char found[10] = {0};

  if (frame[0] != 0xff || (frame[1] & 0xe0) != 0xe0 || memcmp(xing, "Info", 4))
    return printf("FAIL %s: no Info header\n", name), 0;
  if (flags != (complete ? all_flags : 0))
    return printf("FAIL %s: flags %lx\n", name, flags), 0;

  lame = xing + 8;
  if (complete) {
    if ((long)get_be(xing + 8, 4) != expected->frames ||
        (long)get_be(xing + 12, 4) != expected->bytes + size)
      return printf("FAIL %s: %lu frames %lu bytes, expected %ld and %ld\n",
                    name, get_be(xing + 8, 4), get_be(xing + 12, 4),
                    expected->frames, expected->bytes + size),
             0;
    lame += 8 + 100;
  }

  memcpy(found, lame, 9);
  if (strncmp(found, format, 9))
    return printf("FAIL %s: format \"%s\"\n", name, found), 0;

  if ((long)(get_be(lame + 21, 3) >> 12) != expected->delay ||
      (long)(get_be(lame + 21, 3) & 0xfff) !=
          (complete ? expected->padding : 0))
    return printf("FAIL %s: delay %lu padding %lu, expected %ld and %ld\n",
                  name, get_be(lame + 21, 3) >> 12,
                  get_be(lame + 21, 3) & 0xfff, expected->delay,
                  complete ? expected->padding : 0),
           0;

  if (complete &&
      (get_be(lame + 28, 4) != (unsigned long)expected->bytes + size ||
       get_be(lame + 32, 2) != expected->music_crc))
    return printf("FAIL %s: music length or CRC\n", name), 0;

  if (get_be(lame + 34, 2) != crc16(frame, lame + 34 - frame))
    return printf("FAIL %s: tag CRC\n", name), 0;

  return 1;
}

static int check_quality(const shine_config_t *config, int quality,
                         const char *name) {
  shine_config_t copy = *config, encoded;
  stream_t stream = {NULL, 0, 0};
  shine_t encoder;
  expected_t expected;
  unsigned char *frame;
  int size, placeholder_size, frames, ok = 0;
  char label[96];

  snprintf(label, sizeof(label), "%s quality %d", name, quality);
  copy.mpeg.quality = quality;
  encoder = shine_initialise(&copy);
  if (!encoder)
    return printf("FAIL %s: shine_initialise failed\n", label), 0;

  encoded = copy;
  encoded.wave.samplerate = shine_samplerate(encoder);
  expected.delay = shine_encoder_delay(encoder);

  frame = shine_info_frame(encoder, NULL, 0, &placeholder_size);
  if (!check_frame(frame, placeholder_size, SHINE_INFO_FORMAT, &expected, 0,
                   label))
    goto end;

  encode_noise(encoder, copy.wave.channels, INPUT_SAMPLES, &stream);
  frames = walk_frames(&stream, &encoded, label);
  if (frames < 0)
    goto end;

  expected.frames = frames;
  expected.bytes = stream.size;
  expected.music_crc = crc16(stream.data, stream.size);
  /* Rounded like the encoder's own conversion. */
  expected.padding = (long)frames * shine_samples_per_pass(encoder) *
                         encoded.wave.samplerate / copy.wave.samplerate -
                     expected.delay -
                     ((long)INPUT_SAMPLES * encoded.wave.samplerate +
                      copy.wave.samplerate / 2) /
                         copy.wave.samplerate;
  if (encoded.wave.samplerate != copy.wave.samplerate)
    expected.padding =
        (long)frames * (encoded.wave.samplerate >= 32000 ? 1152 : 576) -
        expected.delay -
        ((long)INPUT_SAMPLES * encoded.wave.samplerate +
         copy.wave.samplerate / 2) /
            copy.wave.samplerate;

  frame = shine_info_frame(encoder, NULL, INPUT_SAMPLES, &size);
  if (size != placeholder_size) {
    printf("FAIL %s: %d bytes, %d before the stream\n", label, size,
           placeholder_size);
    goto end;
  }
  if (!check_frame(frame, size, SHINE_INFO_FORMAT, &expected, 1, label))
    goto end;

  /* Names are cut at 9 characters. */
  frame = shine_info_frame(encoder, "shine4.0.0-long", INPUT_SAMPLES, &size);
  if (!check_frame(frame, size, "shine4.0.", &expected, 1, label))
    goto end;

  ok = 1;

end:
  stream_free(&stream);
  shine_close(encoder);
  return ok;
}

static int check_info(const shine_config_t *config, const char *name) {
  return check_quality(config, 0, name) & check_quality(config, 1, name);
}

int main(void) { return run_all_configs(check_info); }
