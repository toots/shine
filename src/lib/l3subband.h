#ifndef L3SUBBAND_H
#define L3SUBBAND_H

#include "types.h"
#include <stdint.h>

void shine_window_filter_subband(const int16_t **buffer, int32_t s[SBLIMIT],
                                 int k, shine_global_config *config,
                                 int stride);

#endif
