/* wave.c
 *
 * MS Wave files store data 'little endian' sytle. These functions will only
 * work on 'little endian' machines. 09/02/01 P.Everett
 * note. both Acorn/RISC OS and PC/DOS are little endian.
 */

/* Required headers from libshine. */
#include "types.h"

/* Local header */
#include <stdint.h>
#include "wave.h"

/* RISC OS specifics */
#define WAVE  0xfb1      /* Wave filetype */
#define DATA  0xffd      /* Data filetype */

/*
 * wave_close:
 * -----------
 */
void wave_close(FILE *file)
{
  fclose(file);
}

/*
 * wave_open:
 * ----------
 * Opens and verifies the header of the Input Wave file. The file pointer is
 * left pointing to the start of the samples.
 */
FILE *wave_open(const char *fname, config_t *config, int quiet)
{
  static char *channel_mappings[] = {NULL,"mono","stereo"};
  FILE *file;

  /* Wave file headers can vary from this, but we're only intereseted in this format */
  struct wave_header
  {
    char riff[4];             /* "RIFF" */
    uint32_t size;    /* length of rest of file = size of rest of header(36) + data length */
    char wave[4];             /* "WAVE" */
    char fmt[4];              /* "fmt " */
    uint32_t fmt_len;    /* length of rest of fmt chunk = 16 */
    uint16_t tag;       /* MS PCM = 1 */
    uint16_t channels;  /* channels, mono = 1, stereo = 2 */
    uint32_t samp_rate;  /* samples per second = 44100 */
    uint32_t byte_rate;  /* bytes per second = samp_rate * byte_samp = 176400 */
    uint16_t byte_samp; /* block align (bytes per sample) = channels * bits_per_sample / 8 = 4 */
    uint16_t bit_samp;  /* bits per sample = 16 for MS PCM (format specific) */
    char data[4];             /* "data" */
    uint32_t length;     /* data length (bytes) */
  } header;

  if (!strcmp(fname, "-"))
    file = stdin;
  else
    file = fopen(fname, "rb");

  if (!file) error("Unable to open file");

  if (fread(&header, sizeof(header), 1, file) != 1) error("Invalid Header");
  if (strncmp(header.riff,"RIFF", 4) != 0) error("Not a MS-RIFF file");
  if (strncmp(header.wave,"WAVE", 4) != 0) error("Not a WAVE audio");
  if (strncmp(header.fmt, "fmt ", 4) != 0) error("Can't find format chunk");
  if (header.tag != 1)                     error("Unknown WAVE format");
  if (header.channels > 2)                 error("More than 2 channels");
  if (header.bit_samp != 16)               error("Not 16 bit");
  if (strncmp(header.data,"data", 4) != 0) error("Can't find data chunk");

  config->wave.type          = WAVE_RIFF_PCM;
  config->wave.channels      = header.channels;
  config->wave.samplerate    = header.samp_rate;
  config->wave.bits          = header.bit_samp;
  config->wave.total_samples = header.length / header.byte_samp;
  config->wave.length        = header.length / header.byte_rate;

  if (!quiet)
    printf("%s, %s %ldHz %ldbit, Length: %2ld:%2ld:%2ld\n",
           "WAV PCM DATA", channel_mappings[header.channels], (long)header.samp_rate, (long)header.bit_samp,
           (long)config->wave.length/3600, (long)(config->wave.length/60)%60, (long)config->wave.length%60);

  return file;
}

/*
 * read_samples:
 * -------------
 */
int read_samples(short *sample_buffer, int frame_size, callback_t *callback)
{
  int samples_read=0;

  switch(callback->config.wave.type)
  {
    case WAVE_RIFF_PCM :
      samples_read = fread(sample_buffer,sizeof(short),frame_size, callback->user);

      if(samples_read<frame_size && samples_read>0) /* Pad sample with zero's */
        while(samples_read<frame_size) sample_buffer[samples_read++] = 0;
      break;

    default:
      error("[read_samples], wave filetype not supported");
  }
  return samples_read;
}

/*
 * wave_get:
 * ---------
 * Expects an interleaved 16bit pcm stream from read_samples, which it
 * de-interleaves into buffer.
 */
int wave_get(short buffer[2][samp_per_frame], void *callback_in)
{
  static short temp_buf[2304];
  int          samples_read;
  int          j;
  callback_t *callback=callback_in;

  switch(callback->config.mpeg.mode)
  {
    case MODE_MONO  :
      samples_read = read_samples(temp_buf,(int)callback->config.mpeg.samples_per_frame, callback);
      for(j=0;j<samp_per_frame;j++)
      {
        buffer[0][j] = temp_buf[j];
        buffer[1][j] = 0;
      }
      break;

    default: /* stereo */
      samples_read = read_samples(temp_buf,(int)callback->config.mpeg.samples_per_frame<<1, callback);
      for(j=0;j<samp_per_frame;j++)
      {
        buffer[0][j] = temp_buf[2*j];
        buffer[1][j] = temp_buf[2*j+1];
      }
  }
  return samples_read;
}
