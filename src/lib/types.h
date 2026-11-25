#ifndef PRIV_TYPES_H
#define PRIV_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRANULE_SIZE 576

#include "bitstream.h"

#include "fixed_point.h"
#include "kernels.h"

#define HAN_SIZE 512 /* for loop unrolling, require that HAN_SIZE%8==0 */
#define SBLIMIT 32

/* The MDCT scales its output by 1/4 for headroom, which global_gain
 * compensates exactly: 8 steps of 2**(1/4). */
#define MDCT_HEADROOM_STEPS 8

/* Only Layer III is implemented. */
#define LAYER_III 1

/* Largest quantizer step size: global_gain = step size + 210 +
 * MDCT_HEADROOM_STEPS has 8 bits. */
#define MAX_STEPSIZE (45 - MDCT_HEADROOM_STEPS)

/* Short blocks carry a further headroom of 4 (shine_mdct_cosine_short):
 * their global_gain is 8 steps higher again. */
#define SHORT_HEADROOM_STEPS 8

enum { BLOCK_NORM = 0, BLOCK_START = 1, BLOCK_SHORT = 2, BLOCK_STOP = 3 };

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 2
#endif

#ifndef MAX_GRANULES
#define MAX_GRANULES 2
#endif

typedef struct {
  int channels;
  int samplerate;
} priv_shine_wave_t;

typedef struct {
  int version;
  int layer;
  int granules_per_frame;
  int mode; /* + */ /* Stereo mode */
  int bitrate;
  int emphasis;
  int padding;
  int bits_per_frame;
  int bits_per_slot;
  /* Fractional slots per frame and padding lag, in 1/samplerate slots. */
  long frac_slots_per_frame;
  long slot_lag;
  int whole_slots_per_frame;
  int bitrate_index; /* + */    /* See Main.c and Layer3.c */
  int samplerate_index; /* + */ /* See Main.c and Layer3.c */
  int crc;
  int ext;
  int mode_ext;
  int copyright; /* + */
  int original;  /* + */
} priv_shine_mpeg_t;

/* Largest quantised value of a band whose lines are quantised by comparing
 * them with thresholds. */
#define QUANT_DIRECT_LEVELS 3

/* Lines quantised together by quantize, according to their largest. */
#define QUANT_RUN_LINES 32

typedef struct {
  int32_t *xr;                 /* magnitudes of the spectral values */
  int32_t xrabs[GRANULE_SIZE]; /* xr absolute */
  int32_t xrmax;               /* maximum of xrabs array */
  /* Maximum of each run of QUANT_RUN_LINES lines, without the model. */
  int32_t run_peak[GRANULE_SIZE / QUANT_RUN_LINES];
  /* For each step size up to 0, the smallest xrabs quantised to 1, 2...
   * QUANT_DIRECT_LEVELS + 1. */
  uint32_t quant_threshold[128][QUANT_DIRECT_LEVELS + 1];
} l3loop_t;

typedef struct {
  int off[MAX_CHANNELS];
  int32_t x[MAX_CHANNELS][HAN_SIZE];
} subband_t;

/* Side information */
typedef struct {
  unsigned part2_3_length;
  unsigned block_type; /* BLOCK_NORM at quality 0 */
  unsigned subblock_gain[3];
  unsigned big_values;
  unsigned count1;
  unsigned global_gain;
  unsigned scalefac_compress;
  unsigned table_select[3];
  unsigned region0_count;
  unsigned region1_count;
  unsigned preflag;
  unsigned scalefac_scale;
  unsigned count1table_select;
  unsigned part2_length;
  unsigned sfb_lmax;
  unsigned address1;
  unsigned address2;
  unsigned address3;
  int quantizerStepSize;
  unsigned slen[4];
} gr_info;

typedef struct {
  unsigned private_bits;
  unsigned main_data_begin; /* bytes */
  int resvDrain;
  unsigned scfsi[MAX_CHANNELS][4];
  struct {
    struct {
      gr_info tt;
    } ch[MAX_CHANNELS];
  } gr[MAX_GRANULES];
} shine_side_info_t;

typedef struct {
  int32_t l[MAX_GRANULES][MAX_CHANNELS][22];    /* [cb] */
  int32_t s[MAX_GRANULES][MAX_CHANNELS][13][3]; /* [window][cb] */
} shine_scalefac_t;

typedef struct shine_global_flags {
  priv_shine_wave_t wave;
  priv_shine_mpeg_t mpeg;
  int quality;           /* 0 to SHINE_QUALITY_MAX */
  bitstream_t bs;        /* the frame being written */
  frame_stream_t stream; /* the frames written */
  shine_side_info_t side_info;
  int sideinfo_len;
  int mean_bits;
  shine_scalefac_t scalefactor;
  const int16_t *buffer[MAX_CHANNELS];
  int l3_enc[MAX_CHANNELS][MAX_GRANULES][GRANULE_SIZE];
  int32_t l3_sb_sample[MAX_CHANNELS][MAX_GRANULES + 1][18][SBLIMIT];
  int32_t mdct_freq[MAX_CHANNELS][MAX_GRANULES][GRANULE_SIZE];
  int ResvSize;
  int ResvMax;
  l3loop_t l3loop;
  subband_t subband;
  shine_kernels_t kernels;
  struct psy_model *psy; /* NULL at quality 0 */
  /* doc/psy/resampling.md: NULL when the input is at the encoding rate,
   * wave.samplerate. */
  struct shine_resampler *resampler;
  int input_samples;                /* per pass, at the input rate */
  int16_t *resampled[MAX_CHANNELS]; /* converted samples not yet in a frame */
  int resampled_count;
  int resampled_capacity;
} shine_global_config;

#endif
