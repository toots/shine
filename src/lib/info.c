/* The Info frame in the LAME format: a Xing header with LAME's extension, in
 * a silent frame before the audio. Field layout from LAME's VbrTag.c;
 * decoders take the encoder delay and end padding from the extension. */

#include "info.h"
#include "layer3.h"
#include "tables.h"
#include "types.h"

#include <string.h>

#define XING_FRAMES 0x1
#define XING_BYTES 0x2
#define XING_TOC 0x4
#define TOC_ENTRIES 100
/* "Info", flags, frame count, byte count, seek table. */
#define XING_BYTES_WITH_FIELDS (4 + 4 + 4 + 4 + TOC_ENTRIES)
#define LAME_EXTENSION_BYTES 36
#define FORMAT_NAME_BYTES 9
#define LAME_FIELD_MAX 4095

/* CRC-16 with polynomial 0x8005, reflected, as LAME and FFmpeg compute it. */
uint16_t shine_info_crc(uint16_t crc, const unsigned char *data, long size) {
  for (long i = 0; i < size; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++)
      crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
  }
  return crc;
}

static unsigned char *put_be(unsigned char *out, unsigned long value,
                             int bytes) {
  for (int i = bytes - 1; i >= 0; i--)
    *out++ = (unsigned char)(value >> (8 * i));
  return out;
}

static int frame_bytes(const shine_global_config *config, int bitrate_index) {
  return config->mpeg.granules_per_frame * 72000 *
         shine_bitrates[bitrate_index][config->mpeg.version] /
         config->wave.samplerate;
}

/* The stream's bitrate, or the lowest above it whose frame holds the tag:
 * low bitrates have frames shorter than it. */
static int info_bitrate_index(const shine_global_config *config) {
  const int needed =
      config->sideinfo_len / 8 + XING_BYTES_WITH_FIELDS + LAME_EXTENSION_BYTES;
  int index = config->mpeg.bitrate_index;

  while (index < 14 && frame_bytes(config, index) < needed &&
         shine_bitrates[index + 1][config->mpeg.version] > 0)
    index++;
  return index;
}

static long clamp_field(long value) {
  return value < 0 ? 0 : value > LAME_FIELD_MAX ? LAME_FIELD_MAX : value;
}

/* Samples per channel at the encoding rate that `samples` input samples
 * convert to. */
static long encoded_samples(const shine_global_config *config, long samples) {
  if (config->input_rate == config->wave.samplerate)
    return samples;
  return (long)(((int64_t)samples * config->wave.samplerate +
                 config->input_rate / 2) /
                config->input_rate);
}

unsigned char *shine_info_frame(shine_global_config *config, const char *format,
                                long samples, int *written) {
  const int index = info_bitrate_index(config);
  const int size = frame_bytes(config, index);
  const long frame_samples =
      (long)config->mpeg.granules_per_frame * GRANULE_SIZE;
  const long delay = shine_encoder_delay(config);
  const int complete = config->flushed;
  unsigned char *frame = config->info_frame, *out;
  long padding = 0;
  size_t name_length;

  if (!format)
    format = SHINE_INFO_FORMAT;
  if (samples <= 0)
    samples = config->passes * shine_samples_per_pass(config);
  if (complete)
    padding = config->frames * frame_samples - delay -
              encoded_samples(config, samples);

  memset(frame, 0, size);
  frame[0] = 0xff;
  frame[1] = 0xe0 | config->mpeg.version << 3 | LAYER_III << 1 | 1;
  frame[2] = index << 4 | (config->mpeg.samplerate_index % 3) << 2;
  frame[3] = config->mpeg.mode << 6 | config->mpeg.copyright << 3 |
             config->mpeg.original << 2 | config->mpeg.emphasis;

  /* Zero side information: the frame decodes as silence. */
  out = frame + config->sideinfo_len / 8;
  memcpy(out, "Info", 4);
  out = put_be(out + 4, complete ? XING_FRAMES | XING_BYTES | XING_TOC : 0, 4);
  if (complete) {
    out = put_be(out, config->frames, 4);
    out = put_be(out, config->bytes + size, 4);
    /* A constant bitrate: byte offsets are proportional to time. */
    for (int i = 0; i < TOC_ENTRIES; i++)
      *out++ = (unsigned char)(i * 256 / TOC_ENTRIES);
  }

  name_length = strlen(format);
  memcpy(out, format,
         name_length < FORMAT_NAME_BYTES ? name_length : FORMAT_NAME_BYTES);
  out += FORMAT_NAME_BYTES;
  *out++ = 0x01;            /* tag revision 0, constant bitrate */
  out += 1 + 4 + 2 + 2 + 1; /* lowpass, peak, two gains, flags: unknown */
  *out++ = config->mpeg.bitrate > 255 ? 255 : config->mpeg.bitrate;
  out = put_be(out, clamp_field(delay) << 12 | clamp_field(padding), 3);
  out += 1 + 1 + 2; /* misc, MP3 gain, preset: none */
  out = put_be(out, complete ? config->bytes + size : 0, 4);
  out = put_be(out, complete ? config->music_crc : 0, 2);
  put_be(out, shine_info_crc(0, frame, out - frame), 2);

  *written = size;
  return frame;
}
