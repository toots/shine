#ifndef L3SUBBAND_H
#define L3SUBBAND_H

#include <stdint.h>

void L3_subband_initialise( shine_global_config *config );
void L3_window_filter_subband(int16_t **buffer, long s[SBLIMIT], int k,  shine_global_config *config);

#endif
