/* wave.c
 *
 * WAVE files are little endian. Reading them byte by byte works on any host.
 */

/* Required headers from libshine. */
#include "layer3.h"

/* Local header */
#include "main.h"
#include "wave.h"
#include <stdint.h>
#include <string.h>

typedef struct {
  char id[4];
  uint32_t length;
} riff_chunk_header_t;

typedef struct {
  uint32_t length;
  uint16_t format;      /* MS PCM = 1 */
  uint16_t channels;    /* channels, mono = 1, stereo = 2 */
  uint32_t sample_rate; /* samples per second = 44100 */
  uint32_t byte_rate;   /* bytes per second = samp_rate * byte_samp = 176400 */
  uint16_t frame_size;  /* block align (bytes per sample) = channels *
                           bits_per_sample / 8 = 4 */
  uint16_t depth;       /* bits per sample = 16 for MS PCM (format specific) */
} fmt_chunk_t;

#define FMT_CHUNK_SIZE 16

static uint16_t read_le16(const unsigned char *bytes) {
  return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint32_t read_le32(const unsigned char *bytes) {
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
         (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

void wave_seek(FILE *file, int has_seek, uint32_t bytes) {
  uint32_t i;
  if (has_seek == 1)
    fseek(file, bytes, SEEK_CUR);
  else {
    for (i = 0; i < bytes; i++)
      getc(file);
  }
}

unsigned char wave_get_chunk_header(FILE *file, int has_seek, const char id[4],
                                    riff_chunk_header_t *header) {
  unsigned char found = 0, bytes[8];
  uint32_t chunk_length;

  if (verbose())
    fprintf(stderr, "Looking for chunk '%s'\n", id);

  while (!found) {
    if (fread(bytes, sizeof(bytes), 1, file) != 1) {
      if (feof(file))
        return 0;
      else
        error("Read error");
    }
    memcpy(header->id, bytes, 4);
    header->length = read_le32(bytes + 4);

    /* chunks must be word-aligned, chunk data doesn't need to */
    chunk_length = header->length + header->length % 2;
    if (verbose()) {
      fprintf(stderr, "Found chunk '%.4s', length: %u\n", header->id,
              header->length);
    }

    if (strncmp(header->id, id, 4) == 0)
      return 1;

    wave_seek(file, has_seek, chunk_length);
  }

  return 1;
}

void wave_close(wave_t *wave) { fclose(wave->file); }

/*
 * wave_open:
 * ----------
 * Opens and verifies the header of the Input Wave file. The file pointer is
 * left pointing to the start of the samples.
 */
unsigned char wave_open(const char *fname, wave_t *wave, shine_config_t *config,
                        int quiet) {
  static char *channel_mappings[] = {NULL, "mono", "stereo"};
  unsigned char riff[12], fmt[FMT_CHUNK_SIZE];
  fmt_chunk_t fmt_chunk;
  riff_chunk_header_t fmt_header, data_chunk;
  uint32_t fmt_length;

  if (!strcmp(fname, "-")) {
    /* TODO: support raw PCM stream with commandline parameters specifying
     * format */
    wave->file = stdin;
    wave->has_seek = 0;
  } else {
    wave->file = fopen(fname, "rb");
    wave->has_seek = 1;
  }

  if (!wave->file)
    error("Unable to open file");

  if (fread(riff, sizeof(riff), 1, wave->file) != 1)
    error("Invalid header");

  if (memcmp(riff, "RIFF", 4) != 0)
    error("Not a MS-RIFF file");

  if (memcmp(riff + 8, "WAVE", 4) != 0)
    error("Not a WAVE audio file");

  /* Check the fmt chunk */
  if (!wave_get_chunk_header(wave->file, wave->has_seek, "fmt ", &fmt_header))
    error("WAVE fmt chunk not found");

  if (fmt_header.length < FMT_CHUNK_SIZE ||
      fread(fmt, sizeof(fmt), 1, wave->file) != 1)
    error("Read error");

  fmt_chunk.length = fmt_header.length;
  fmt_chunk.format = read_le16(fmt);
  fmt_chunk.channels = read_le16(fmt + 2);
  fmt_chunk.sample_rate = read_le32(fmt + 4);
  fmt_chunk.byte_rate = read_le32(fmt + 8);
  fmt_chunk.frame_size = read_le16(fmt + 12);
  fmt_chunk.depth = read_le16(fmt + 14);

  if (verbose())
    fprintf(stderr, "WAVE format: %u\n", fmt_chunk.format);

  if (fmt_chunk.format != 1)
    error("Unknown WAVE format");

  if (fmt_chunk.channels < 1 || fmt_chunk.channels > 2)
    error("Unsupported number of channels");

  if (fmt_chunk.depth != 16)
    error("Unsupported PCM bit depth");

  // Skip remaining data.
  fmt_length = fmt_chunk.length + fmt_chunk.length % 2;
  if (fmt_length > FMT_CHUNK_SIZE)
    wave_seek(wave->file, wave->has_seek, fmt_length - FMT_CHUNK_SIZE);

  /* Position the file pointer at the data chunk */
  if (!wave_get_chunk_header(wave->file, wave->has_seek, "data", &data_chunk))
    error("WAVE data chunk not found");

  config->wave.channels = fmt_chunk.channels;
  config->wave.samplerate = fmt_chunk.sample_rate;

  wave->channels = fmt_chunk.channels;
  /* Streaming writers cannot know the length in advance. */
  wave->data_unbounded = data_chunk.length == 0xffffffff;
  wave->data_remaining = data_chunk.length;
  /* Informative only: do not reject files over it. */
  wave->duration =
      fmt_chunk.byte_rate ? data_chunk.length / fmt_chunk.byte_rate : 0;

  if (!quiet)
    printf("%s, %s %ldHz %ldbit, duration: %02ld:%02ld:%02ld\n",
           "WAVE PCM Data", channel_mappings[fmt_chunk.channels],
           (long)fmt_chunk.sample_rate, (long)fmt_chunk.depth,
           (long)wave->duration / 3600, (long)(wave->duration / 60) % 60,
           (long)wave->duration % 60);
  return 1;
}

/*
 * read_samples:
 * -------------
 */

int read_samples(int16_t *sample_buffer, int frame_size, wave_t *wave) {
  int samples_wanted = frame_size, samples_read;

  if (!wave->data_unbounded &&
      wave->data_remaining / sizeof(int16_t) < (uint32_t)samples_wanted)
    samples_wanted = wave->data_remaining / sizeof(int16_t);

  samples_read =
      fread(sample_buffer, sizeof(int16_t), samples_wanted, wave->file);
  wave->data_remaining -= samples_read * sizeof(int16_t);

  /* In place: sample i only reads bytes 2i and 2i + 1. */
  for (int i = 0; i < samples_read; i++)
    sample_buffer[i] =
        (int16_t)read_le16((const unsigned char *)sample_buffer + 2 * i);

  if (samples_read < frame_size &&
      samples_read > 0) { /* Pad sample with zero's */
    memset(sample_buffer + samples_read, 0,
           (frame_size - samples_read) * sizeof(int16_t));
    samples_read = frame_size;
  }

  return samples_read;
}

/*
 * wave_get:
 * ---------
 * Expects an interleaved 16bit pcm stream from read_samples, which it
 * reads into the given buffer.
 */
int wave_get(int16_t *buffer, wave_t *wave, int samp_per_pass) {
  return read_samples(buffer, samp_per_pass * wave->channels, wave);
}
