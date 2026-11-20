#ifndef RESERVOIR_H
#define RESERVOIR_H

#include "types.h"

int shine_max_reservoir_bits(shine_global_config *config);
void shine_ResvAdjust(gr_info *gi, shine_global_config *config);
void shine_ResvFrameEnd(shine_global_config *config);

#endif
