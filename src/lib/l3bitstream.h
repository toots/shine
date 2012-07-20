#ifndef L3_BITSTREAM_H
#define L3_BITSTREAM_H

typedef   bitstream_t L3_bitstream_t;

void L3_format_bitstream(int              l3_enc[2][2][samp_per_frame2],
                         L3_side_info_t  *l3_side,
                         L3_scalefac_t   *scalefac,
                         L3_bitstream_t  *in_bs,
                         long            (*xr)[2][samp_per_frame2],
                         char             *ancillary,
                         int              anc_bits, config_t *config);

int HuffmanCode(int table_select, int x, int y, unsigned *code,
                unsigned int *extword, int *codebits, int *extbits);

int abs_and_sign(int *x); /* returns signx and changes *x to abs(*x) */

#endif
