/* wave.c
 *
 * MS Wave files store data 'little endian' sytle. These functions will only
 * work on 'little endian' machines. 09/02/01 P.Everett
 * note. both Acorn/RISC OS and PC/DOS are little endian.
 */

/* Required headers from libshine. */
#include "layer3.h"

/* Local header */
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "wave.h"

/* RISC OS specifics */
#define WAVE  0xfb1      /* Wave filetype */
#define DATA  0xffd      /* Data filetype */

#define MODE_MONO 3

typedef struct {
	char id[4];
	uint32_t length;
} riff_chunk_header_t;

typedef struct {
	riff_chunk_header_t header;
	char wave[4];
} wave_chunk_t;

typedef struct {
	riff_chunk_header_t header;
	uint16_t format;       /* MS PCM = 1 */
	uint16_t channels;     /* channels, mono = 1, stereo = 2 */
	uint32_t sample_rate;  /* samples per second = 44100 */
	uint32_t byte_rate;    /* bytes per second = samp_rate * byte_samp = 176400 */
	uint16_t frame_size;   /* block align (bytes per sample) = channels * bits_per_sample / 8 = 4 */
	uint16_t depth;        /* bits per sample = 16 for MS PCM (format specific) */
	uint16_t extra_length; /* optional extra data */
} fmt_chunk_t;

struct data_chunk {
	riff_chunk_header_t header;
	char data[];
}

bool wave_get_chunk_header(FILE *file, const char* id, riff_chunk_header_t *header)
{
	bool found;

	while(!found) {
		if (fread(header, sizeof(header), 1, file) != 1) {
			if (feof(file))
				return false;
			else
				error("Read error");

		if (strncmp(header.id, id, 4) == 0)
			return true;

		for (long i = 0; i < (header.length - sizeof(header), i++)
			getc(file);

	}
}


void wave_close(wave_t *wave)
{
  fclose(wave->file);
}


/*
 * wave_open:
 * ----------
 * Opens and verifies the header of the Input Wave file. The file pointer is
 * left pointing to the start of the samples.
 */
bool wave_open(const char *fname, wave_t *wave, shine_config_t *config, int quiet, long *data_length)
{
	static char *channel_mappings[] = { NULL, "mono", "stereo" };
	uint32_t duration;
	wave_chunk_t wave_chunk;
	fmt_chunk_t fmt_chunk;
	riff_chunk_header_t data_chunk;

	if (!strcmp(fname, "-"))
		 /* TODO: support raw PCM stream with commandline parameters specifying format */
		wave->file = stdin;
	else
		wave->file = fopen(fname, "rb");

	if (!wave->file)
		error("Unable to open file");

	if (fread(&wave_chunk, sizeof(wave_chunk), 1, wave->file) != 1)
		error("Invalid header");

	if (strncmp(wave_chunk.header.id, "RIFF", 4) != 0)
		error("Not a MS-RIFF file");

	if (strncmp(wave_chunk.wave, "WAVE", 4) != 0)
		error("Not a WAVE audio file");

	/* Check the fmt chunk */
	if (!wave_get_chunk_header(wave->file, 'fmt ', &fmt_chunk))
		error("WAVE fmt chunk not found");

	if(fread(&fmt_chunk + sizeof(riff_chunk_header_t),
		sizeof(fmt_chunk) - sizeof(riff_chunk_header_t), 1, wave->file) != 1)
		error("Read error");

	if (fmt_chunk.format != 1)
		error("Unknown WAVE format");

	if (fmt_chunk.channels > 2)
		error("More than 2 channels");

	if (fmt_chunk.depth != 16)
		error("Unsupported PCM bit depth");

	/* Position the file pointer at the data chunk */
	if (!wave_get_chunk_header(wave->file, 'data', &data_chunk))
		error("WAVE data chunk not found");

	config->wave.channels   = fmt_chunk.channels;
	config->wave.samplerate = fmt_chunk.sample_rate;
  	duration = data_chunk.length / fmt_chunk.byte_rate;

	if (!quiet)
		printf("%s, %s %ldHz %ldbit, Length: %2ld:%2ld:%2ld\n",
			"WAV PCM DATA", channel_mappings[fmt_chunk.channels], (long)fmt_chunk.sample_rate, (long)fmt_chunk.depth,
			(long)duration / 3600, (long)(duration / 60) % 60, (long)duration % 60);
	return true;
}

/*
 * read_samples:
 * -------------
 */

/* TODO: respect data chunk length */
int read_samples(int16_t *sample_buffer, int frame_size, FILE *file)
{
  int samples_read=0;

  samples_read = fread(sample_buffer,sizeof(int16_t),frame_size, file);

  if(samples_read<frame_size && samples_read>0) /* Pad sample with zero's */
    while(samples_read<frame_size) sample_buffer[samples_read++] = 0;

  return samples_read;
}

/*
 * wave_get:
 * ---------
 * Expects an interleaved 16bit pcm stream from read_samples, which it
 * de-interleaves into buffer.
 */
int wave_get(int16_t buffer[2][samp_per_frame], wave_t *wave, void *config_in)
{
	FILE *file = wave->file;

  static int16_t temp_buf[2304];
  int            samples_read;
  int            j;
  shine_config_t      *config=config_in;

  switch(config->mpeg.mode)
  {
    case MODE_MONO  :
      samples_read = read_samples(temp_buf,(int)samp_per_frame, file);
      for(j=0;j<samp_per_frame;j++)
      {
        buffer[0][j] = temp_buf[j];
        buffer[1][j] = 0;
      }
      break;

    default: /* stereo */
      samples_read = read_samples(temp_buf,(int)samp_per_frame<<1, file);
      for(j=0;j<samp_per_frame;j++)
      {
        buffer[0][j] = temp_buf[2*j];
        buffer[1][j] = temp_buf[2*j+1];
      }
  }
  return samples_read;
}

