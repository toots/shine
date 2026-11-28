#ifndef L3LOOP_H
#define L3LOOP_H

#include "types.h"

void shine_iteration_loop(shine_global_config *config);

/* Fills the quantiser's thresholds. */
void shine_quantize_initialise(shine_global_config *config);

/* The largest magnitude of lines begin..end - 1 of the current granule. */
int32_t shine_band_peak(int begin, int end, const shine_global_config *config);

/* Quantizes lines begin..end - 1 of the current granule, whose largest
 * magnitude is peak, with one step size. Returns the largest ix, above 8192
 * when the step is too small. */
int shine_quantize_band(int ix[GRANULE_SIZE], int begin, int end, int stepsize,
                        int32_t peak, shine_global_config *config);

/* Huffman bits of ix, choosing the granule's code tables and regions. */
int shine_count_bits(int ix[GRANULE_SIZE], gr_info *cod_info,
                     shine_global_config *config);

/* As shine_count_bits of an ix that is zero from end on, an even line,
 * when the bits are at most limit; above limit otherwise, with the code
 * tables of cod_info incomplete. */
int shine_count_bits_up_to(int ix[GRANULE_SIZE], int end, gr_info *cod_info,
                           shine_global_config *config, int limit);

/* As shine_count_bits, with the region split of a long granule that takes
 * the fewest bits (doc/psy/noise-allocation.md §4.2). `sums` is scratch:
 * one row per class, and row 0 unused. */
int shine_count_bits_split(int ix[GRANULE_SIZE], gr_info *cod_info,
                           shine_global_config *config,
                           shine_region_bits_t (*sums)[23]);

/* The bits of a granule, from its perceptual entropy and the reservoir
 * (doc/psy/reservoir.md §3). Called once, before its first channel. */
void shine_psy_granule_bits(shine_global_config *config, int gr);

/* The share of the granule's bits of channel ch (psy.c,
 * doc/psy/noise-allocation.md §3.1). */
int shine_psy_channel_bits(const shine_global_config *config, int gr, int ch);

/* The noise allocation of the model (psy.c, doc/psy/noise-allocation.md):
 * step size, scalefactors and ix of the granule, with its part2_3_length
 * returned. */
int shine_psy_allocate(shine_global_config *config, int ix[GRANULE_SIZE],
                       int gr, int ch, int max_bits);

#endif
