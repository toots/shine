#ifndef RESAMPLE_H
#define RESAMPLE_H

#include "layer3.h"

#include <stdint.h>

/* doc/psy/resampling.md: the converter from the input rate to the encoding
 * rate. */

/* Half length of the prototype filter, in output periods: the delay of the
 * converter in output samples. */
#define RESAMPLE_ZEROS 32
/* Points of the prototype per output period. */
#define RESAMPLE_POINTS 32

typedef struct shine_resampler shine_resampler_t;

/* `shine_initialise` with the encoding rate given, at most the input's,
 * rather than chosen by §1. */
shine_t shine_initialise_at(const shine_config_t *config, int samplerate);

/* §1: the rate to encode an input at, `samplerate` itself for no
 * conversion. `cutoff` is the lowpass cutoff of the bitrate in Hz, 0 for
 * none (doc/psy/bandwidth.md §1). */
int shine_resample_rate(int samplerate, int bitrate, int cutoff);

/* NULL when out of memory. */
shine_resampler_t *shine_resampler_open(int input_rate, int output_rate,
                                        int channels);
void shine_resampler_close(shine_resampler_t *resampler);

/* The most samples `shine_resample` writes for `count` input samples. */
int shine_resample_bound(const shine_resampler_t *resampler, int count);

/* Converts `count` samples per channel, input[ch][i * stride], at most
 * 2 * GRANULE_SIZE, into output[ch]. Returns the samples written per
 * channel. The output does not depend on how the input is split into
 * calls. */
int shine_resample(shine_resampler_t *resampler, const int16_t *const *input,
                   int stride, int count, int16_t *const *output);

#endif
