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

typedef struct psy_model {
  int rate; /* samplerate index */
  int npart;
  int channels; /* analysis channels: 1, 2, or 4 with M/S */
  int adapt_ath;
  const psy_bitrate_row_t *row;
  int band_part[PSY_LONG_BANDS + 1]; /* first partition of each band */
  int32_t minval[PSY_NPART_MAX];
  int32_t ath_part[PSY_NPART_MAX];
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
} psy_model_t;

/* Allocates the model of an encoder, or leaves config->psy NULL at the
 * lowest quality. Returns -1 when out of memory. */
int shine_psy_initialise(shine_global_config *config);

/* Runs the model on the frame's MDCT, and codes it as mid/side when the
 * model chooses so (doc/psy/signal-path.md §1). */
void shine_psy_frame(shine_global_config *config);

/* log2 of x in Q16, PSY_LOG_ZERO for 0. */
int32_t shine_psy_log2(uint64_t x);

/* log2(2^a + 2^b). */
int32_t shine_psy_log_add(int32_t a, int32_t b);

#endif
