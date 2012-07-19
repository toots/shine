#ifndef L3SUBBAND_H
#define L3SUBBAND_H

void L3_subband_initialise();
void L3_window_filter_subband(short **buffer, long s[SBLIMIT], int k);

#endif
