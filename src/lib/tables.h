#ifndef TABLES_H
#define TABLES_H

#include "types.h"

extern const int shine_slen1_tab[16];
extern const int shine_slen2_tab[16];

extern const int shine_samplerates[9];
extern const int shine_bitrates[16][4];

extern const int shine_scale_fact_band_index[9][23];
extern const int shine_scale_fact_band_index_short[9][14];
extern const int32_t shine_enwindow[];

/* precomputed_tables.c */
extern const int32_t shine_subband_filter[SBLIMIT][64];
extern const int32_t shine_mdct_cosine[18][36];
extern const int32_t shine_mdct_cosine_start[18][36];
extern const int32_t shine_mdct_cosine_stop[18][36];
extern const int32_t shine_mdct_cosine_short[6][12];
extern const int32_t shine_mdct_alias_cs[8];
extern const int32_t shine_mdct_alias_ca[8];
extern const int32_t shine_steptabi[128];
extern const uint32_t shine_steptab_fraction[MAX_STEPSIZE + 1];
extern const int16_t shine_int2idx[10000];

/* Values of ix up to this are decided exactly, from the coefficient in Q8
 * (shine_quant_threshold). */
#define QUANT_EXACT_MAX 64
extern const int32_t shine_quant_threshold[QUANT_EXACT_MAX + 2];
/* ln below this, the scaled coefficient rounded to an integer, covers the
 * values up to QUANT_EXACT_MAX. */
#define QUANT_SPLIT_LINES 260
extern const uint8_t shine_quant_base[QUANT_SPLIT_LINES];
extern const int32_t shine_quant_split[QUANT_SPLIT_LINES];

#endif
