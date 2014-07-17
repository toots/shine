#ifndef LAYER3_H
#define LAYER3_H

#include <stdint.h>

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

/* Abtract type for the shine encoder handle. */
typedef struct shine_global_flags *shine_t;

/* Fill in a `mpeg_t` structure with default values. */
void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg);

/* Check if a given bitrate and samplerate is supported by the encoder (see `samplerates` 
 * and `bitrates` above for a list of acceptable values). 
 *
 * Returns -1 on error, mpeg_version on success. */
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

/* Maximun possible value for the function below. */
#define SHINE_MAX_SAMPLES 1152

/* Returns audio samples expected in each frame. */
int shine_samples_per_pass(shine_t s);

/* Encode audio data. Source data must have `shine_samples_per_pass(s)` audio samples per
 * channels. Mono encoder only expect one channel. 
 *
 * Returns a pointer to freshly encoded data while `written` contains the size of
 * available data. This pointer's memory is handled by the library and is only valid 
 * until the next call to `shine_encode_buffer` or `shine_close` and may be NULL if no data
 * was written. */
unsigned char *shine_encode_buffer(shine_t s, int16_t **data, long *written);

/* Encode audio data. Source data must have `shine_samples_per_pass(s)` audio samples per
 * channels. Mono encoder only expect one channel. 
 *
 * Returns a pointer to freshly encoded data while `written` contains the size of
 * available data. This pointer's memory is handled by the library and is only valid 
 * until the next call to `shine_encode_buffer` or `shine_close` and may be NULL if no data
 * was written. */
unsigned char *shine_encode_buffer_interlaced(shine_t s, int16_t *data, long *written);

/* Flush all data currently in the encoding buffer. Should be used before closing
 * the encoder, to make all encoded data has been written. */
unsigned char *shine_flush(shine_t s, long *written);

/* Close an encoder, freeing all associated memory. Encoder handler is not
 * valid after this call. */
void shine_close(shine_t s);

#endif
