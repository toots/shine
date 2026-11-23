/* layer3.c */

#include "layer3.h"
#include "bitstream.h"
#include "l3bitstream.h"
#include "l3loop.h"
#include "l3mdct.h"
#include "l3subband.h"
#include "tables.h"
#include "types.h"

static const int granules_per_frame[4] = {
    1,  /* MPEG 2.5 */
    -1, /* Reserved */
    1,  /* MPEG II */
    2   /* MPEG I */
};

/* Set default values for important vars */
void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg) {
  mpeg->bitrate = 128;
  mpeg->emphasis = SHINE_EMPHASIS_NONE;
  mpeg->copyright = 0;
  mpeg->original = 1;
}

int shine_mpeg_version(int samplerate_index) {
  /* Pick mpeg version according to samplerate index. */
  if (samplerate_index < 3)
    /* First 3 samplerates are for MPEG-I */
    return SHINE_MPEG_I;
  else if (samplerate_index < 6)
    /* Then it's MPEG-II */
    return SHINE_MPEG_II;
  else
    /* Finally, MPEG-2.5 */
    return SHINE_MPEG_25;
}

int shine_find_samplerate_index(int samplerate) {
  int i;

  for (i = 0; i < 9; i++)
    if (samplerate == shine_samplerates[i])
      return i;

  return -1; /* error - not a valid samplerate for encoder */
}

int shine_find_bitrate_index(int bitrate, int mpeg_version) {
  int i;

  for (i = 0; i < 16; i++)
    if (bitrate == shine_bitrates[i][mpeg_version])
      return i;

  return -1; /* error - not a valid samplerate for encoder */
}

int shine_check_config(int samplerate, int bitrate) {
  int samplerate_index, bitrate_index, mpeg_version;

  samplerate_index = shine_find_samplerate_index(samplerate);
  if (samplerate_index < 0)
    return -1;

  mpeg_version = shine_mpeg_version(samplerate_index);

  bitrate_index = shine_find_bitrate_index(bitrate, mpeg_version);
  if (bitrate_index < 0)
    return -1;

  return mpeg_version;
}

int shine_samples_per_pass(shine_t s) {
  return s->mpeg.granules_per_frame * GRANULE_SIZE;
}

/* Compute default encoding values. */
shine_global_config *shine_initialise(const shine_config_t *pub_config) {
  long slots_numerator;
  shine_global_config *config;

  if (shine_check_config(pub_config->wave.samplerate,
                         pub_config->mpeg.bitrate) < 0)
    return NULL;

  /* The channel count sizes the per-channel state, and the mode written in
   * the header must describe the same number of channels. */
  if (pub_config->wave.channels < 1 || pub_config->wave.channels > 2 ||
      pub_config->mpeg.mode < SHINE_STEREO ||
      pub_config->mpeg.mode > SHINE_MONO ||
      (pub_config->wave.channels == 1) != (pub_config->mpeg.mode == SHINE_MONO))
    return NULL;

  config = calloc(1, sizeof(shine_global_config));
  if (config == NULL)
    return config;

  /* Copy public config. */
  config->wave.channels = pub_config->wave.channels;
  config->wave.samplerate = pub_config->wave.samplerate;
  config->mpeg.mode = pub_config->mpeg.mode;
  config->mpeg.bitrate = pub_config->mpeg.bitrate;
  config->mpeg.emphasis = pub_config->mpeg.emphasis;
  config->mpeg.copyright = pub_config->mpeg.copyright;
  config->mpeg.original = pub_config->mpeg.original;

  /* Set default values. */
  config->ResvMax = 0;
  config->ResvSize = 0;
  config->mpeg.layer = LAYER_III;
  config->mpeg.crc = 0;
  config->mpeg.ext = 0;
  config->mpeg.mode_ext = 0;
  config->mpeg.bits_per_slot = 8;

  config->mpeg.samplerate_index =
      shine_find_samplerate_index(config->wave.samplerate);
  config->mpeg.version = shine_mpeg_version(config->mpeg.samplerate_index);
  config->mpeg.bitrate_index =
      shine_find_bitrate_index(config->mpeg.bitrate, config->mpeg.version);
  config->mpeg.granules_per_frame = granules_per_frame[config->mpeg.version];

  /* Integer arithmetic: floating point yields 215.999... instead of 216
   * slots for e.g. 8 kHz at 24 kbps, which sets the padding bit on frames
   * that are not padded. */
  slots_numerator = (long)config->mpeg.granules_per_frame * GRANULE_SIZE *
                    1000 * config->mpeg.bitrate / config->mpeg.bits_per_slot;

  config->mpeg.whole_slots_per_frame =
      slots_numerator / config->wave.samplerate;

  config->mpeg.frac_slots_per_frame = slots_numerator % config->wave.samplerate;
  config->mpeg.slot_lag = -config->mpeg.frac_slots_per_frame;

  if (shine_open_bit_stream(&config->bs, BUFFER_SIZE) < 0) {
    free(config);
    return NULL;
  }

  memset((char *)&config->side_info, 0, sizeof(shine_side_info_t));

  /* determine the mean bitrate for main data */
  if (config->mpeg.granules_per_frame == 2) /* MPEG 1 */
    config->sideinfo_len = 8 * ((config->wave.channels == 1) ? 4 + 17 : 4 + 32);
  else /* MPEG 2 */
    config->sideinfo_len = 8 * ((config->wave.channels == 1) ? 4 + 9 : 4 + 17);

  return config;
}

/* Analysis filterbank delay: polyphase 240 + MDCT 288. */
#define SHINE_ENCODER_DELAY 528
#define MP3_DECODER_DELAY 529

static void shine_encode_frame(shine_global_config *config, int stride) {
  if (config->mpeg.frac_slots_per_frame) {
    config->mpeg.padding =
        config->mpeg.slot_lag <=
        config->mpeg.frac_slots_per_frame - config->wave.samplerate;
    config->mpeg.slot_lag += config->mpeg.padding * config->wave.samplerate -
                             config->mpeg.frac_slots_per_frame;
  }

  config->mpeg.bits_per_frame =
      8 * (config->mpeg.whole_slots_per_frame + config->mpeg.padding);
  config->mean_bits = (config->mpeg.bits_per_frame - config->sideinfo_len) /
                      config->mpeg.granules_per_frame;

  /* apply mdct to the polyphase output */
  shine_mdct_sub(config, stride);

  /* bit and noise allocation */
  shine_iteration_loop(config);

  /* write the frame to the bitstream */
  shine_format_bitstream(config);
}

static unsigned char *shine_take_data(shine_global_config *config,
                                      int *written) {
  *written = config->bs.data_position;
  config->bs.data_position = 0;

  return config->bs.data;
}

unsigned char *shine_encode_buffer(shine_global_config *config,
                                   int16_t *const *data, int *written) {
  config->buffer[0] = data[0];
  if (config->wave.channels == 2)
    config->buffer[1] = data[1];

  shine_encode_frame(config, 1);
  return shine_take_data(config, written);
}

unsigned char *shine_encode_buffer_interleaved(shine_global_config *config,
                                               const int16_t *data,
                                               int *written) {
  config->buffer[0] = data;
  if (config->wave.channels == 2)
    config->buffer[1] = data + 1;

  shine_encode_frame(config, config->wave.channels);
  return shine_take_data(config, written);
}

int shine_encoder_delay(shine_global_config *config) {
  (void)config;
  return SHINE_ENCODER_DELAY;
}

/* Encode enough silence for a decoder to output the last input sample. */
unsigned char *shine_flush(shine_global_config *config, int *written) {
  static const int16_t silence[2 * 2 * GRANULE_SIZE];
  int samples_per_pass = shine_samples_per_pass(config);
  int frames =
      (SHINE_ENCODER_DELAY + MP3_DECODER_DELAY + samples_per_pass - 1) /
      samples_per_pass;

  for (int i = 0; i < frames; i++) {
    config->buffer[0] = silence;
    if (config->wave.channels == 2)
      config->buffer[1] = silence + 1;
    shine_encode_frame(config, config->wave.channels);
  }
  shine_flush_bit_stream(&config->bs);

  return shine_take_data(config, written);
}

void shine_close(shine_global_config *config) {
  shine_close_bit_stream(&config->bs);
  free(config);
}
