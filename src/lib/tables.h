#ifndef TABLES_H
#define TABLES_H

#include "types.h"

extern const int shine_slen1_tab[16];
extern const int shine_slen2_tab[16];

extern const int shine_samplerates[9];
extern const int shine_bitrates[16][4];

extern const int shine_scale_fact_band_index[9][23];
extern const int32_t shine_enwindow[];

/* precomputed_tables.c */
extern const int32_t shine_subband_filter[SBLIMIT][64];
extern const int32_t shine_mdct_cosine[18][36];
extern const int32_t shine_mdct_alias_cs[8];
extern const int32_t shine_mdct_alias_ca[8];
extern const int32_t shine_steptabi[128];
extern const uint32_t shine_steptab_fraction[MAX_STEPSIZE + 1];
extern const int16_t shine_int2idx[10000];

#endif
