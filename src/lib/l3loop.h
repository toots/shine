#ifndef L3LOOP_H
#define L3LOOP_H

#include "types.h"

void shine_iteration_loop(shine_global_config *config);

/* Quantizes lines begin..end - 1 of the current granule with one step size.
 * Returns the largest ix, above 8192 when the step is too small. */
int shine_quantize_band(int ix[GRANULE_SIZE], int begin, int end, int stepsize,
                        shine_global_config *config);

/* Huffman bits of ix, choosing the granule's code tables and regions. */
int shine_count_bits(int ix[GRANULE_SIZE], gr_info *cod_info,
                     shine_global_config *config);

/* The noise allocation of the model (psy.c, doc/psy/noise-allocation.md):
 * step size, scalefactors and ix of the granule, with its part2_3_length
 * returned. */
int shine_psy_allocate(shine_global_config *config, int ix[GRANULE_SIZE],
                       int gr, int ch, int max_bits);

#endif
