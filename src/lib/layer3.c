/* layer3.c */

#include "layer3.h"
#include "bitstream.h"
#include "l3bitstream.h"
#include "l3loop.h"
#include "l3mdct.h"
#include "l3subband.h"
#include "psy.h"
#include "resample.h"
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
  mpeg->quality = 0;
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

static int frame_samples(const shine_global_config *config) {
  return config->mpeg.granules_per_frame * GRANULE_SIZE;
}

int shine_samples_per_pass(shine_t s) {
  return s->resampler ? s->input_samples : frame_samples(s);
}

int shine_samplerate(shine_t s) { return s->wave.samplerate; }

/* doc/psy/resampling.md §1: the rate the configuration is encoded at. */
static int encoding_rate(const shine_config_t *config) {
  if (config->mpeg.quality < 1)
    return config->wave.samplerate;
  return shine_resample_rate(
      config->wave.samplerate, config->mpeg.bitrate,
      shine_psy_cutoff(config->mpeg.bitrate, config->wave.channels,
                       config->mpeg.mode == SHINE_JOINT_STEREO));
}

/* Sets up the conversion from `input_rate`, when it is not the encoding
 * rate. Returns -1 when out of memory. */
static int resampling_initialise(shine_global_config *config, int input_rate) {
  if (input_rate == config->wave.samplerate)
    return 0;

  /* A pass is a frame at the input rate, as without conversion. */
  config->input_samples = granules_per_frame[shine_mpeg_version(
                              shine_find_samplerate_index(input_rate))] *
                          GRANULE_SIZE;
  config->resampler = shine_resampler_open(input_rate, config->wave.samplerate,
                                           config->wave.channels);
  if (!config->resampler)
    return -1;
  config->resampled_capacity =
      frame_samples(config) +
      shine_resample_bound(config->resampler, config->input_samples);
  for (int ch = 0; ch < config->wave.channels; ch++) {
    config->resampled[ch] =
        malloc(config->resampled_capacity * sizeof(int16_t));
    if (!config->resampled[ch])
      return -1;
  }
  return 0;
}

static void resampling_close(shine_global_config *config) {
  shine_resampler_close(config->resampler);
  free(config->resampled[0]);
  free(config->resampled[1]);
}

shine_global_config *shine_initialise(const shine_config_t *pub_config) {
  if (shine_check_config(pub_config->wave.samplerate,
                         pub_config->mpeg.bitrate) < 0 ||
      pub_config->wave.channels < 1 || pub_config->wave.channels > 2)
    return NULL;
  return shine_initialise_at(pub_config, encoding_rate(pub_config));
}

/* Compute default encoding values. */
shine_global_config *shine_initialise_at(const shine_config_t *pub_config,
                                         int samplerate) {
  long slots_numerator;
  shine_global_config *config;

  if (shine_check_config(pub_config->wave.samplerate,
                         pub_config->mpeg.bitrate) < 0 ||
      shine_check_config(samplerate, pub_config->mpeg.bitrate) < 0 ||
      samplerate > pub_config->wave.samplerate)
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
  shine_kernels_select(&config->kernels);

  /* Copy public config. */
  config->wave.channels = pub_config->wave.channels;
  config->wave.samplerate = samplerate;
  config->mpeg.mode = pub_config->mpeg.mode;
  config->mpeg.bitrate = pub_config->mpeg.bitrate;
  config->mpeg.emphasis = pub_config->mpeg.emphasis;
  config->mpeg.copyright = pub_config->mpeg.copyright;
  config->mpeg.original = pub_config->mpeg.original;
  config->quality = pub_config->mpeg.quality < 0 ? 0
                    : pub_config->mpeg.quality > SHINE_QUALITY_MAX
                        ? SHINE_QUALITY_MAX
                        : pub_config->mpeg.quality;

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

  shine_quantize_initialise(config);

  if (shine_psy_initialise(config) < 0) {
    free(config);
    return NULL;
  }

  if (resampling_initialise(config, pub_config->wave.samplerate) < 0 ||
      shine_open_bit_stream(&config->bs, BUFFER_SIZE) < 0) {
    resampling_close(config);
    free(config->psy);
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

  /* doc/psy/reservoir.md §2. Quality 0 has no reservoir. */
  if (config->psy) {
    int field = config->mpeg.granules_per_frame == 2 ? 511 : 255;
    int capacity = config->mpeg.bits_per_frame - config->sideinfo_len;
    /* The next frame draws from the reservoir, and may be padded. */
    int longest = 8 * (config->mpeg.whole_slots_per_frame +
                       (config->mpeg.frac_slots_per_frame != 0));
    int limit = MIN(8 * field, 7680 - longest);

    limit = MIN(limit, (SLOT_QUEUE - 2) * capacity);
    config->ResvMax = limit > 0 ? limit & ~7 : 0;
  }

  if (config->psy)
    stride = shine_psy_block_types(config, stride);

  /* apply mdct to the polyphase output */
  shine_mdct_sub(config, stride);

  if (config->psy)
    shine_psy_frame(config);

  /* bit and noise allocation */
  config->side_info.main_data_begin = config->ResvSize / 8;
  shine_iteration_loop(config);

  /* write the frame, then lay it out in the stream */
  shine_format_bitstream(config);
  shine_flush_bit_stream(&config->bs);
  /* A failure stops the stream: shine_stream_take returns nothing more. */
  shine_stream_frame(&config->stream, config->bs.data, config->sideinfo_len / 8,
                     config->bs.data_position - config->sideinfo_len / 8,
                     config->mpeg.bits_per_frame / 8);
  config->bs.data_position = 0;
}

static unsigned char *shine_take_data(shine_global_config *config,
                                      int *written) {
  return shine_stream_take(&config->stream, 0, written);
}

/* doc/psy/resampling.md §5: converts `count` samples per channel and
 * encodes the frames they complete. */
static void encode_resampled(shine_global_config *config,
                             const int16_t *const *data, int stride,
                             int count) {
  const int samples = frame_samples(config);
  int16_t *end[MAX_CHANNELS];
  int start = 0;

  for (int ch = 0; ch < config->wave.channels; ch++)
    end[ch] = config->resampled[ch] + config->resampled_count;
  config->resampled_count +=
      shine_resample(config->resampler, data, stride, count, end);

  for (; config->resampled_count - start >= samples; start += samples) {
    for (int ch = 0; ch < config->wave.channels; ch++)
      config->buffer[ch] = config->resampled[ch] + start;
    shine_encode_frame(config, 1);
  }
  config->resampled_count -= start;
  for (int ch = 0; ch < config->wave.channels; ch++)
    memmove(config->resampled[ch], config->resampled[ch] + start,
            config->resampled_count * sizeof(int16_t));
}

unsigned char *shine_encode_buffer(shine_global_config *config,
                                   int16_t *const *data, int *written) {
  if (config->resampler) {
    const int16_t *channels[MAX_CHANNELS] = {
        data[0], config->wave.channels == 2 ? data[1] : NULL};

    encode_resampled(config, channels, 1, config->input_samples);
    return shine_take_data(config, written);
  }

  config->buffer[0] = data[0];
  if (config->wave.channels == 2)
    config->buffer[1] = data[1];

  shine_encode_frame(config, 1);
  return shine_take_data(config, written);
}

unsigned char *shine_encode_buffer_interleaved(shine_global_config *config,
                                               const int16_t *data,
                                               int *written) {
  if (config->resampler) {
    const int16_t *channels[MAX_CHANNELS] = {data, data + 1};

    encode_resampled(config, channels, config->wave.channels,
                     config->input_samples);
    return shine_take_data(config, written);
  }

  config->buffer[0] = data;
  if (config->wave.channels == 2)
    config->buffer[1] = data + 1;

  shine_encode_frame(config, config->wave.channels);
  return shine_take_data(config, written);
}

int shine_encoder_delay(shine_global_config *config) {
  return SHINE_ENCODER_DELAY + (config->psy ? PSY_LOOKAHEAD : 0) +
         (config->resampler ? RESAMPLE_ZEROS : 0);
}

/* Converts silence until `pending` more samples at the encoding rate are in
 * frames. */
static void flush_resampled(shine_global_config *config, int pending) {
  static const int16_t silence[2 * GRANULE_SIZE];
  const int16_t *channels[MAX_CHANNELS] = {silence, silence};
  const int samples = frame_samples(config);

  pending += config->resampled_count;
  for (int frames = (pending + samples - 1) / samples; frames > 0;) {
    int before = config->resampled_count;
    int16_t *end[MAX_CHANNELS];

    for (int ch = 0; ch < config->wave.channels; ch++)
      end[ch] = config->resampled[ch] + before;
    config->resampled_count += shine_resample(config->resampler, channels, 1,
                                              config->input_samples, end);
    while (frames > 0 && config->resampled_count >= samples) {
      for (int ch = 0; ch < config->wave.channels; ch++)
        config->buffer[ch] = config->resampled[ch];
      shine_encode_frame(config, 1);
      config->resampled_count -= samples;
      for (int ch = 0; ch < config->wave.channels; ch++)
        memmove(config->resampled[ch], config->resampled[ch] + samples,
                config->resampled_count * sizeof(int16_t));
      frames--;
    }
  }
}

/* Encode enough silence for a decoder to output the last input sample. */
unsigned char *shine_flush(shine_global_config *config, int *written) {
  static const int16_t silence[2 * 2 * GRANULE_SIZE];
  int samples_per_pass = shine_samples_per_pass(config);
  int frames =
      (shine_encoder_delay(config) + MP3_DECODER_DELAY + samples_per_pass - 1) /
      samples_per_pass;

  if (config->resampler) {
    flush_resampled(config, shine_encoder_delay(config) + MP3_DECODER_DELAY);
    return shine_stream_take(&config->stream, 1, written);
  }

  for (int i = 0; i < frames; i++) {
    config->buffer[0] = silence;
    if (config->wave.channels == 2)
      config->buffer[1] = silence + 1;
    shine_encode_frame(config, config->wave.channels);
  }

  return shine_stream_take(&config->stream, 1, written);
}

void shine_close(shine_global_config *config) {
  shine_close_bit_stream(&config->bs);
  free(config->stream.data);
  resampling_close(config);
  free(config->psy);
  free(config);
}
