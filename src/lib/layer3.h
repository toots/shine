#ifndef LAYER3_H
#define LAYER3_H

#include <stdint.h>

#define samp_per_frame  1152

/* Valid samplerates and bitrates. */
static long samplerates[9] = {
  44100, 48000, 32000, /* MPEG-I */
  22050, 24000, 16000, /* MPEG-II */
  11025, 12000, 8000   /* MPEG-2.5 */
};

static int bitrates[16][3] = {
  /* MPEG version:
   * I,  II,  2.5 */
   {-1,  -1, -1},
   { 32,  8,  8},
   { 40, 16, 16},
   { 48, 24, 24},
   { 56, 32, 32},
   { 64, 40, 40},
   { 80, 48, 48},
   { 96, 56, 56},
   {112, 64, 64},
   {128, 80, 80},
   {160, 96, 96},
   {192,112,112},
   {224,128,128},
   {256,144,144},
   {320,160,160},
   {-1,  -1, -1}
};

/* This is the struct used to tell the encoder about the input PCM */

enum channels {
  PCM_MONO   = 1,
  PCM_STEREO = 2
};

enum mpeg_versions {
  MPEG_I  = 3,
  MPEG_II = 2,
  MPEG_25 = 0
};

/* Only Layer III currently implemented. */
enum mpeg_layers {
  LAYER_I   = 3,
  LAYER_II  = 2,
  LAYER_III = 1
};

typedef struct {
    enum channels channels;
    long          samplerate;
} shine_wave_t;

/* This is the struct the encoder uses to tell the encoder about the output MP3 */

enum modes {
  STEREO       = 0,
  JOINT_STEREO = 1,
  DUAL_CHANNEL = 2,
  MONO         = 3 
};

enum emph {
  NONE    = 0,
  MU50_15 = 1,
  CITT    = 3
};

typedef struct {
    enum modes mode;      /* Stereo mode */
    int        bitr;      /* Must conform to known bitrate */
    enum emph  emph;      /* De-emphasis */
    int        copyright;
    int        original;
} shine_mpeg_t;

typedef struct {
  shine_wave_t wave;
  shine_mpeg_t mpeg;
} shine_config_t;

/* Set of read-only variables. */
typedef struct {
  enum mpeg_versions mpeg_version;
  enum mpeg_layers   mpeg_layer; /* Always III for now. */
} shine_read_only_config_t;

/* Abtract type for the shine encoder handle. */
typedef struct shine_global_flags *shine_t;

/* Fill in a `mpeg_t` structure with default values. */
void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg);

/* Check if a given bitrate and samplerate is supported by the encoder (see `samplerates` 
 * and `bitrates` above for a list of acceptable values). 
 *
 * Returns -1 on error, 0 on success. */
int shine_check_config(long freq, int bitr);

/* Pass a pointer to a `config_t` structure and returns an initialized
 * encoder. 
 *
 * Configuration data is copied over to the encoder. It is not possible
 * to change its values after initializing the encoder at the moment.
 *
 * Checking for valid configuration values is left for the application to 
 * implement. You can use the `shine_find_bitrate_index` and 
 * `shine_find_samplerate_index` functions or the `bitrates` and 
 * `samplerates` arrays above to check those parameters. Mone and stereo 
 * mode for wave and mpeg should also be consistent with each other.
 *
 * This function returns NULL if it was not able to allocate memory data for 
 * the encoder. */
shine_t shine_initialise(shine_config_t *config);

/* Fill up argument with read-only configuration variables. */
void shine_read_only_config(shine_t s, shine_read_only_config_t *config);

/* Encode audio data. Source data must have `samp_per_frames` audio samples per
 * channels. Mono encoder only expect one channel. 
 *
 * Returns a pointer to freshly encoded data while `written` contains the size of
 * available data. This pointer's memory is handled by the library and is only valid 
 * until the next call to `shine_encode_frame` or `shine_close` and may be NULL if no data
 * was written. */
unsigned char *shine_encode_frame(shine_t s, int16_t data[2][samp_per_frame], long *written);

/* Flush all data currently in the encoding buffer. Should be used before closing
 * the encoder, to make all encoded data has been written. */
unsigned char *shine_flush(shine_t s, long *written);

/* Close an encoder, freeing all associated memory. Encoder handler is not
 * valid after this call. */
void shine_close(shine_t s);

#endif
