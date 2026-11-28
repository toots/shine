#ifndef L3SUBBAND_H
#define L3SUBBAND_H

#include "types.h"
#include <stdint.h>

/* The polyphase analysis filter over the next granule of channel `ch`: 18
 * steps of 32 PCM samples, taken `stride` apart, to 18 samples of each of
 * the 32 subbands. */
void shine_window_filter_subband(shine_global_config *config, int ch,
                                 int stride, int32_t s[18][SBLIMIT]);

#endif
