#ifndef LAYER3_H
#define LAYER3_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MPEG versions, as returned by shine_mpeg_version() and
 * shine_check_config(). */
enum shine_mpeg_version {
  SHINE_MPEG_I = 3,
  SHINE_MPEG_II = 2,
  SHINE_MPEG_25 = 0
};

/* The input PCM. */
typedef struct {
  int channels; /* 1 or 2 */
  int samplerate;
} shine_wave_t;

enum shine_mode {
  SHINE_STEREO = 0,
  SHINE_JOINT_STEREO = 1,
  SHINE_DUAL_CHANNEL = 2,
  SHINE_MONO = 3
};

enum shine_emphasis {
  SHINE_EMPHASIS_NONE = 0,
  SHINE_EMPHASIS_50_15 = 1, /* 50/15 microseconds */
  SHINE_EMPHASIS_CCITT = 3  /* CCITT J.17 */
};

/* The output MP3. */
typedef struct {
  enum shine_mode mode; /* SHINE_MONO for one channel */
  int bitrate;          /* in kbps, see the table below */
  enum shine_emphasis emphasis;
  int copyright;
  int original;
} shine_mpeg_t;

typedef struct {
  shine_wave_t wave;
  shine_mpeg_t mpeg;
} shine_config_t;

/* Tables of supported audio parameters & format.
 *
 * Valid samplerates and bitrates.
 * const int samplerates[9] = {
 *   44100, 48000, 32000, // MPEG-I
 *   22050, 24000, 16000, // MPEG-II
 *   11025, 12000, 8000   // MPEG-2.5
 * };
 *
 * const int bitrates[16][4] = {
 * //  MPEG version:
 * //  2.5, reserved,  II,  I
 *   { -1,  -1,        -1,  -1},
 *   { 8,   -1,         8,  32},
 *   { 16,  -1,        16,  40},
 *   { 24,  -1,        24,  48},
 *   { 32,  -1,        32,  56},
 *   { 40,  -1,        40,  64},
 *   { 48,  -1,        48,  80},
 *   { 56,  -1,        56,  96},
 *   { 64,  -1,        64, 112},
 *   { 80,  -1,        80, 128},
 *   { 96,  -1,        96, 160},
 *   {112,  -1,       112, 192},
 *   {128,  -1,       128, 224},
 *   {144,  -1,       144, 256},
 *   {160,  -1,       160, 320},
 *   { -1,  -1,        -1,  -1}
 *  };
 *
 */

/* Abstract type for the shine encoder handle. */
typedef struct shine_global_flags *shine_t;

/* Fill in a `shine_mpeg_t` structure with default values. */
void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg);

/* Check if a given bitrate is supported by the encoder (see `bitrates` above
 * for a list of acceptable values. */
int shine_find_bitrate_index(int bitrate, int mpeg_version);

/* Check if a given samplerate is supported by the encoder (see `samplerates`
 * above for a list of acceptable values. */
int shine_find_samplerate_index(int samplerate);

/* Returns the MPEG version used for the given samplerate index. See above
 * `shine_mpeg_version` for a list of possible values. */
int shine_mpeg_version(int samplerate_index);

/* Check if a given bitrate and samplerate is supported by the encoder (see
 * `samplerates` and `bitrates` above for a list of acceptable values).
 *
 * Returns -1 on error, mpeg_version on success. */
int shine_check_config(int samplerate, int bitrate);

/* Pass a pointer to a `shine_config_t` structure and returns an initialized
 * encoder.
 *
 * Configuration data is copied over to the encoder. It is not possible
 * to change its values after initializing the encoder at the moment.
 *
 * Checking for valid configuration values is left for the application to
 * implement. You can use the `shine_find_bitrate_index` and
 * `shine_find_samplerate_index` functions or the `bitrates` and
 * `samplerates` arrays above to check those parameters. `wave.channels` must
 * be 1 or 2, with `mpeg.mode` SHINE_MONO exactly when it is 1.
 *
 * This function returns NULL on an invalid configuration, or if it was not
 * able to allocate memory data for the encoder. */
shine_t shine_initialise(const shine_config_t *config);

/* Maximum possible value for the function below. */
#define SHINE_MAX_SAMPLES 1152

/* Returns audio samples expected in each frame. */
int shine_samples_per_pass(shine_t s);

/* Encode audio data. Source data must have `shine_samples_per_pass(s)` audio
 * samples per channels. Mono encoder only expect one channel.
 *
 * Returns a pointer to freshly encoded data while `written` contains the size
 * of available data. This pointer's memory is handled by the library and is
 * only valid until the next call to `shine_encode_buffer` or `shine_close` and
 * may be NULL if no data was written. The samples are not modified. */
unsigned char *shine_encode_buffer(shine_t s, int16_t *const *data,
                                   int *written);

/* Encode interleaved audio data. Source data must have
 * `shine_samples_per_pass(s)` audio samples per channels. Mono encoder only
 * expect one channel.
 *
 * Returns a pointer to freshly encoded data while `written` contains the size
 * of available data. This pointer's memory is handled by the library and is
 * only valid until the next call to `shine_encode_buffer` or `shine_close` and
 * may be NULL if no data was written. */
unsigned char *shine_encode_buffer_interleaved(shine_t s, const int16_t *data,
                                               int *written);

/* Number of samples the encoded stream lags the input by, before any decoder
 * delay. */
int shine_encoder_delay(shine_t s);

/* Drain the encoder and return all remaining data. Encodes silence until the
 * last input sample has passed through the filterbank and the decoder delay,
 * so callers tracking the real input length should trim the excess at the
 * end. Should be called once, before closing the encoder.
 *
 * The returned pointer follows the same rules as `shine_encode_buffer`. */
unsigned char *shine_flush(shine_t s, int *written);

/* Close an encoder, freeing all associated memory. Encoder handler is not
 * valid after this call. */
void shine_close(shine_t s);

#ifdef __cplusplus
}
#endif

#endif
