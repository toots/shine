/* Psychoacoustic model, long blocks (doc/psy/). Phase 1: masking thresholds,
 * adaptive ATH, perceptual entropy and the mid/side decision.
 *
 * Energies and thresholds are log2 in Q16 of the energy unit of
 * doc/psy/conventions.md, PSY_LOG_ZERO for 0. */

#ifndef PSY_H
#define PSY_H

#include "psy_tables.h"
#include "types.h"

#define PSY_LOG_ZERO INT32_MIN

/* Analysis channels: left (or mono), right, mid, side. */
#define PSY_CHANNELS 4
#define PSY_LONG_BANDS 22
#define PSY_SHORT_BANDS 13
#define PSY_PE_HISTORY 19

/* The detector granule of a coded granule starts 336 samples before the
 * granule's own input: its slots 1 to 3 then cover the samples its three
 * short windows code with most weight, and slot 3 the first window of the
 * next granule (doc/psy/block-switching.md §1.4). The type of a granule
 * depends on whether the next one is short, which takes its input up to 250
 * samples past the end of the granule being coded. */
#define PSY_DETECTOR_OFFSET 336
#define PSY_LOOKAHEAD 256
#define PSY_INPUT_HISTORY 608

typedef struct {
  /* History of an analysis channel (doc/psy/signal-path.md §4). */
  int granules_seen;                  /* saturates at 2 */
  uint64_t last_energy[GRANULE_SIZE]; /* previous line energies */
  int last_shift;                     /* their scale, 2^-last_shift */
  int32_t nb_1[PSY_NPART_MAX], nb_2[PSY_NPART_MAX];
} psy_channel_t;

/* Partition values of one analysis channel's long spectrum. */
typedef struct {
  int32_t eb[PSY_NPART_MAX], max[PSY_NPART_MAX];
  uint64_t eb_raw[PSY_NPART_MAX]; /* scaled by 2^-shift */
  int shift;
  int32_t unit; /* log2 of the energy unit of eb_raw */
  int idx[PSY_NPART_MAX];
  int32_t ecb[PSY_NPART_MAX]; /* spread, before the limits */
  int32_t thr[PSY_NPART_MAX];
} psy_partitions_t;

/* A partition layout, long or short (doc/psy/spectral-analysis.md §3), with
 * the tables of the stream's sample rate and bitrate. */
typedef struct {
  int npart, lines, bands;
  int preecho;    /* the long layout: masking.md §3.1 applies, MASK_ADJUST */
  int32_t log2_k; /* energy unit of a squared coefficient */
  const int16_t *start; /* first line of each partition, then lines */
  const int32_t *mld, *masking_lower;
  const uint8_t *spread_lo, *spread_hi;
  const int16_t *spread_offset;
  const uint32_t *spread;
  const int *band_part; /* first partition of each band, then npart */
  const int32_t *minval, *ath_part;
} psy_layout_t;

#define PSY_ALLOCATION_BANDS (3 * PSY_SHORT_BANDS)

/* The bands the noise allocation works on, for one coded channel of one
 * granule (doc/psy/noise-allocation.md §6): the long bands, or each short
 * band of each window in coded order, band 3 sb + window. */
typedef struct {
  int is_short, count;
  int shaped;  /* the first bands, which have an allowed distortion */
  int lsf;     /* an MPEG-2 or 2.5 stream */
  int preflag; /* noise-allocation.md §5.1 */
  int32_t log2_k;
  int edge[PSY_ALLOCATION_BANDS + 1]; /* first line of each band, then 576 */
  int32_t xmin[PSY_ALLOCATION_BANDS], en[PSY_ALLOCATION_BANDS];
  int32_t peak[PSY_ALLOCATION_BANDS]; /* largest magnitude of each band */
} psy_bands_t;

/* Slots of a band in the step cache, indexed by step mod PSY_STEP_SLOTS:
 * the steps an allocation tries for a band are a few apart. */
#define PSY_STEP_SLOTS 16

enum { PSY_STEP_EMPTY, PSY_STEP_QUANTISED, PSY_STEP_MEASURED };

/* Bands as quantised at a step, and their noise once measured, during the
 * allocation of one channel: its search revisits most (band, step) pairs. */
typedef struct {
  int step[PSY_ALLOCATION_BANDS][PSY_STEP_SLOTS];
  int max[PSY_ALLOCATION_BANDS][PSY_STEP_SLOTS]; /* largest ix of the band */
  int32_t noise[PSY_ALLOCATION_BANDS][PSY_STEP_SLOTS];
  uint8_t state[PSY_ALLOCATION_BANDS][PSY_STEP_SLOTS];
  uint16_t ix[PSY_STEP_SLOTS][GRANULE_SIZE]; /* each band at its lines */
} psy_step_cache_t;

typedef struct psy_model {
  int rate; /* samplerate index */
  int npart;
  int channels; /* analysis channels: 1, 2, or 4 with M/S */
  int adapt_ath;
  const psy_bitrate_row_t *row;
  /* doc/psy/parameters.md §1: a one-channel stream at a low rate takes
   * MASK_ADJUST and MASK_ADJUST_SHORT from its bits per sample. */
  int low_mono;
  int32_t low_mono_adjust[2];
  psy_layout_t layout[2]; /* long, short */
  /* doc/psy/bandwidth.md: the lowpass gains of long and short lines, Q15,
   * and whether there is a cutoff. */
  int lowpass;
  int32_t lowpass_long[GRANULE_SIZE], lowpass_short[192];
  int band_part[PSY_LONG_BANDS + 1]; /* first partition of each band */
  int32_t minval[PSY_NPART_MAX];
  int32_t ath_part[PSY_NPART_MAX];
  int32_t ath_band[PSY_LONG_BANDS];
  int xmin_bands;                         /* bands with an allowed distortion */
  uint32_t loudness_weight[GRANULE_SIZE]; /* eql_w, Q32 */

  int64_t adjust_factor, adjust_limit; /* Q31 */
  psy_channel_t channel[PSY_CHANNELS];
  /* Scratch, holding the frame's last granule. */
  int64_t coefficients[PSY_CHANNELS][GRANULE_SIZE];
  psy_partitions_t parts[PSY_CHANNELS];

  /* Outputs of the last frame (doc/psy/signal-path.md §3.2). */
  int32_t en[MAX_GRANULES][PSY_CHANNELS][PSY_LONG_BANDS];
  int32_t thm[MAX_GRANULES][PSY_CHANNELS][PSY_LONG_BANDS];
  int32_t pe[MAX_GRANULES][PSY_CHANNELS]; /* Q8 */
  int32_t energy[MAX_GRANULES][PSY_CHANNELS];
  int32_t loudness[MAX_GRANULES][MAX_CHANNELS];
  int ms;
  /* Tests: every granule whose index divides by this is short, whatever
   * the detector says. 0: the detector decides. */
  int forced_short_period;
  long granule_index; /* of the frame's first granule */

  /* doc/psy/signal-path.md §2: the input, PSY_LOOKAHEAD samples ahead of
   * the samples being coded, after PSY_INPUT_HISTORY earlier ones. */
  int16_t input[MAX_CHANNELS][PSY_INPUT_HISTORY + MAX_GRANULES * GRANULE_SIZE];
  /* doc/psy/block-switching.md §1, per analysis channel: the last two
   * sub-block peaks and the last short window sum of the previous detector
   * granule, its slot 3 before the suppression rules, and its slot 2
   * after them. */
  int64_t last_peaks[PSY_CHANNELS][2], last_window_sum[PSY_CHANNELS];
  int next_slot0[PSY_CHANNELS], last_attacks[PSY_CHANNELS];
  /* Whether each granule of the frame is short, and the one after them. */
  int is_short[MAX_GRANULES + 1];
  /* doc/psy/spectral-analysis.md §1.1: the long MDCT of the frame's short
   * granules, with the normal window. */
  int32_t analysis_long[MAX_CHANNELS][MAX_GRANULES][GRANULE_SIZE];
  unsigned previous_type; /* final block type of the granule before */

  /* doc/psy/spectral-analysis.md §2: the three short spectra of every
   * granule of the frame, lines in ascending frequency. */
  int32_t short_lines[MAX_CHANNELS][MAX_GRANULES][3][192];
  psy_channel_t channel_s[PSY_CHANNELS];
  int band_part_s[PSY_SHORT_BANDS + 1];
  int32_t minval_s[PSY_NPART_S_MAX], ath_part_s[PSY_NPART_S_MAX];
  int32_t ath_band_s[PSY_SHORT_BANDS];
  int xmin_bands_s;  /* short bands with an allowed distortion */
  psy_bands_t bands; /* of the channel being allocated */
  psy_step_cache_t steps;
  /* Scratch of the region split (noise-allocation.md §4.2). */
  shine_region_bits_t region_sums[SHINE_REGION_CLASSES + 1][23];
  /* Short band energies and thresholds, per window, after the short
   * pre-echo control (doc/psy/block-switching.md §4). */
  int32_t en_s[MAX_GRANULES][PSY_CHANNELS][3][PSY_SHORT_BANDS];
  int32_t thm_s[MAX_GRANULES][PSY_CHANNELS][3][PSY_SHORT_BANDS];
  int32_t last_thm_s[PSY_CHANNELS][PSY_SHORT_BANDS][3];
  int have_last_thm_s;
  /* §1.6: the pulse factor of each window as a right shift, 0 to 2; that
   * of the next granule's first window; slot 2 of the granule before. */
  int pulse[MAX_GRANULES + 1][PSY_CHANNELS][3], next_pulse0[PSY_CHANNELS];
  int previous_slot2[PSY_CHANNELS];
  /* Allowed distortion of a short granule, per coded channel, window and
   * band (doc/psy/allowed-distortion.md §2, §3). */
  int32_t xmin_s[MAX_GRANULES][MAX_CHANNELS][3][PSY_SHORT_BANDS];
  int was_short; /* the granule before the frame's first */
  /* The attack slots 0..3 of each granule of the frame, after the rules. */
  int attacks[MAX_GRANULES + 1][PSY_CHANNELS][4];
  /* doc/psy/perceptual-entropy.md §3: the PE in use of the last frames, Q8,
   * and the filter output F, Q24. */
  int64_t pe_history[PSY_PE_HISTORY], pe_filter;
  /* doc/psy/reservoir.md §3: the bits of each granule of the last frame,
   * and the reservoir they were drawn from. */
  int granule_bits[MAX_GRANULES], reservoir[MAX_GRANULES];
  /* Allowed distortion per coded channel and band, PSY_LOG_ZERO for the
   * bands that have none (doc/psy/allowed-distortion.md §2). */
  int32_t xmin[MAX_GRANULES][MAX_CHANNELS][PSY_LONG_BANDS];
  /* The step size of each channel's last granule, where the next one's
   * search starts. */
  int last_step[MAX_CHANNELS];
} psy_model_t;

/* Allocates the model of an encoder, or leaves config->psy NULL at the
 * lowest quality. Returns -1 when out of memory. */
int shine_psy_initialise(shine_global_config *config);

/* Takes the frame's input, sets the block type of every granule from the
 * attacks in it (doc/psy/block-switching.md §1 to §3), and points
 * config->buffer at the samples to code, PSY_LOOKAHEAD behind. Returns the
 * stride of those samples. */
int shine_psy_block_types(shine_global_config *config, int stride);

/* doc/psy/bandwidth.md §1: the cutoff of a bitrate in Hz at any sample
 * rate, 0 for none. */
int shine_psy_cutoff(int bitrate, int channels, int joint);

/* doc/psy/bandwidth.md §2: the lowpass on a granule's long lines and its
 * three short windows, in frequency order. */
void shine_psy_lowpass(const psy_model_t *model, int32_t *long_lines,
                       int32_t (*short_lines)[192]);

/* Runs the model on the frame's MDCT, and codes it as mid/side when the
 * model chooses so (doc/psy/signal-path.md §1). */
void shine_psy_frame(shine_global_config *config);

/* log2 of x in Q16, PSY_LOG_ZERO for 0. */
int32_t shine_psy_log2(uint64_t x);

/* log2(2^a + 2^b). */
int32_t shine_psy_log_add(int32_t a, int32_t b);

#endif
