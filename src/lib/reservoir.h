#ifndef RESERVOIR_H
#define RESERVOIR_H

void ResvFrameBegin(int frameLength, shine_global_config *config);
int  ResvMaxBits   (double *pe, shine_global_config *config);
void ResvAdjust    (gr_info *gi, shine_global_config *config );
void ResvFrameEnd  (shine_global_config *config );

#endif
