#ifndef L3_BITSTREAM_H
#define L3_BITSTREAM_H

typedef   bitstream_t L3_bitstream_t;

void L3_format_bitstream(shine_global_config *config);

int HuffmanCode(int table_select, int x, int y, unsigned *code,
                unsigned int *extword, int *codebits, int *extbits);

int abs_and_sign(int *x); /* returns signx and changes *x to abs(*x) */

#endif
