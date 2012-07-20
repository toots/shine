#ifndef RESERVOIR_H
#define RESERVOIR_H

void ResvFrameBegin(L3_side_info_t *l3_side, int mean_bits, int frameLength );
int  ResvMaxBits   (L3_side_info_t *l3_side, double *pe, int mean_bits, config_t *config );
void ResvAdjust    (gr_info *gi, L3_side_info_t *l3_side, int mean_bits, config_t *config );
void ResvFrameEnd  (L3_side_info_t *l3_side, int mean_bits, config_t *config );

#endif
