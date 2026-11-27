/* l3bitstrea.c */

#include "l3bitstream.h" /* the public interface */
#include "bitstream.h"
#include "huffman.h"
#include "l3loop.h"
#include "l3mdct.h"
#include "layer3.h"
#include "tables.h"
#include "types.h"

static void encodeBigValues(bitstream_t *bs, unsigned table_select,
                            const int *ix, int start, int end);
static void shine_huffman_coder_count1(bitstream_t *bs,
                                       const struct huffcodetab *h, int v,
                                       int w, int x, int y);

static void encodeSideInfo(shine_global_config *config);
static void encodeMainData(shine_global_config *config);
static void Huffmancodebits(shine_global_config *config, int *ix, gr_info *gi);

/*
  shine_format_bitstream()

  This is called after a frame of audio has been quantized and coded.
  It will write the encoded audio to the bitstream. Note that
  from a layer3 encoder's perspective the bit stream is primarily
  a series of main_data() blocks, with header and side information
  inserted at the proper locations to maintain framing. (See Figure A.7
  in the IS).
*/

void shine_format_bitstream(shine_global_config *config) {
  int gr, ch, i;

  for (ch = 0; ch < config->wave.channels; ch++)
    for (gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
      int *pi = &config->l3_enc[ch][gr][0];
      int32_t *pr = &config->mdct_freq[ch][gr][0];
      for (i = 0; i < GRANULE_SIZE; i++) {
        if ((pr[i] < 0) && (pi[i] > 0))
          pi[i] *= -1;
      }
    }

  encodeSideInfo(config);
  encodeMainData(config);
}

/* MPEG-2 and 2.5 long block scalefactors without intensity stereo
 * (ISO 13818-3 2.4.3.2): four groups of 6, 5, 5 and 5 bands for a
 * scalefac_compress below 400, two of 11 and 10 from 500 up, which also
 * means preflag. */
static void encodeScalefactorsLSF(shine_global_config *config, gr_info *gi,
                                  const int32_t *scalefactors) {
  static const int group_bands[2][4] = {{6, 5, 5, 5}, {11, 10, 0, 0}};
  const unsigned compress = gi->scalefac_compress;
  const int with_preflag = compress >= 500;
  const unsigned slen[2][4] = {
      {(compress >> 4) / 5, (compress >> 4) % 5, (compress & 15) >> 2,
       compress & 3},
      {(compress - 500) / 3, (compress - 500) % 3, 0, 0}};
  int sfb = 0;

  for (int group = 0; group < 4; group++)
    for (int band = 0; band < group_bands[with_preflag][group]; band++, sfb++)
      shine_putbits(&config->bs, scalefactors[sfb], slen[with_preflag][group]);
}

/* Short block scalefactors, by band then window: MPEG-1 codes bands 0..5
 * on slen1 bits and 6..11 on slen2 (ISO 11172-3 2.4.2.7), MPEG-2 and 2.5
 * four groups of three bands (13818-3 2.4.3.2). */
static void encodeScalefactorsShort(shine_global_config *config, gr_info *gi,
                                    int32_t (*scalefactors)[3]) {
  unsigned compress = gi->scalefac_compress;
  unsigned slen[4] = {(compress >> 4) / 5, (compress >> 4) % 5,
                      (compress & 15) >> 2, compress & 3};

  if (config->mpeg.version == SHINE_MPEG_I) {
    slen[0] = slen[1] = shine_slen1_tab[compress];
    slen[2] = slen[3] = shine_slen2_tab[compress];
  }
  for (int sfb = 0; sfb < 12; sfb++)
    for (int window = 0; window < 3; window++)
      shine_putbits(&config->bs, scalefactors[sfb][window], slen[sfb / 3]);
}

static void encodeMainData(shine_global_config *config) {
  int gr, ch, sfb;
  shine_side_info_t si = config->side_info;

  for (gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
    for (ch = 0; ch < config->wave.channels; ch++) {
      gr_info *gi = &(si.gr[gr].ch[ch].tt);
      int *ix = &config->l3_enc[ch][gr][0];
      unsigned slen1, slen2;

      if (gi->block_type == BLOCK_SHORT) {
        encodeScalefactorsShort(config, gi, config->scalefactor.s[gr][ch]);
        Huffmancodebits(config, ix, gi);
        continue;
      }
      if (config->mpeg.version != SHINE_MPEG_I) {
        encodeScalefactorsLSF(config, gi, config->scalefactor.l[gr][ch]);
        Huffmancodebits(config, ix, gi);
        continue;
      }
      slen1 = shine_slen1_tab[gi->scalefac_compress];
      slen2 = shine_slen2_tab[gi->scalefac_compress];

      if (gr == 0 || si.scfsi[ch][0] == 0)
        for (sfb = 0; sfb < 6; sfb++)
          shine_putbits(&config->bs, config->scalefactor.l[gr][ch][sfb], slen1);
      if (gr == 0 || si.scfsi[ch][1] == 0)
        for (sfb = 6; sfb < 11; sfb++)
          shine_putbits(&config->bs, config->scalefactor.l[gr][ch][sfb], slen1);
      if (gr == 0 || si.scfsi[ch][2] == 0)
        for (sfb = 11; sfb < 16; sfb++)
          shine_putbits(&config->bs, config->scalefactor.l[gr][ch][sfb], slen2);
      if (gr == 0 || si.scfsi[ch][3] == 0)
        for (sfb = 16; sfb < 21; sfb++)
          shine_putbits(&config->bs, config->scalefactor.l[gr][ch][sfb], slen2);

      Huffmancodebits(config, ix, gi);
    }
  }

  /* Stuffing, as ancillary data after the last granule. */
  for (int bits = si.resvDrain; bits > 0; bits -= 32)
    shine_putbits(&config->bs, 0, bits < 32 ? bits : 32);
}

static void encodeSideInfo(shine_global_config *config) {
  int gr, ch, scfsi_band, region;
  shine_side_info_t si = config->side_info;

  shine_putbits(&config->bs, 0x7ff, 11);
  shine_putbits(&config->bs, config->mpeg.version, 2);
  shine_putbits(&config->bs, config->mpeg.layer, 2);
  shine_putbits(&config->bs, !config->mpeg.crc, 1);
  shine_putbits(&config->bs, config->mpeg.bitrate_index, 4);
  shine_putbits(&config->bs, config->mpeg.samplerate_index % 3, 2);
  shine_putbits(&config->bs, config->mpeg.padding, 1);
  shine_putbits(&config->bs, config->mpeg.ext, 1);
  shine_putbits(&config->bs, config->mpeg.mode, 2);
  shine_putbits(&config->bs, config->mpeg.mode_ext, 2);
  shine_putbits(&config->bs, config->mpeg.copyright, 1);
  shine_putbits(&config->bs, config->mpeg.original, 1);
  shine_putbits(&config->bs, config->mpeg.emphasis, 2);

  if (config->mpeg.version == SHINE_MPEG_I) {
    shine_putbits(&config->bs, si.main_data_begin, 9);
    if (config->wave.channels == 2)
      shine_putbits(&config->bs, si.private_bits, 3);
    else
      shine_putbits(&config->bs, si.private_bits, 5);
  } else {
    shine_putbits(&config->bs, si.main_data_begin, 8);
    if (config->wave.channels == 2)
      shine_putbits(&config->bs, si.private_bits, 2);
    else
      shine_putbits(&config->bs, si.private_bits, 1);
  }

  if (config->mpeg.version == SHINE_MPEG_I)
    for (ch = 0; ch < config->wave.channels; ch++) {
      for (scfsi_band = 0; scfsi_band < 4; scfsi_band++)
        shine_putbits(&config->bs, si.scfsi[ch][scfsi_band], 1);
    }

  for (gr = 0; gr < config->mpeg.granules_per_frame; gr++)
    for (ch = 0; ch < config->wave.channels; ch++) {
      gr_info *gi = &(si.gr[gr].ch[ch].tt);

      shine_putbits(&config->bs, gi->part2_3_length, 12);
      shine_putbits(&config->bs, gi->big_values, 9);
      shine_putbits(&config->bs, gi->global_gain, 8);
      if (config->mpeg.version == SHINE_MPEG_I)
        shine_putbits(&config->bs, gi->scalefac_compress, 4);
      else
        shine_putbits(&config->bs, gi->scalefac_compress, 9);
      if (gi->block_type) {
        /* window_switching_flag, block_type, no mixed block. The region
         * counts are implied. */
        shine_putbits(&config->bs, 1, 1);
        shine_putbits(&config->bs, gi->block_type, 2);
        shine_putbits(&config->bs, 0, 1);
        for (region = 0; region < 2; region++)
          shine_putbits(&config->bs, gi->table_select[region], 5);
        for (int window = 0; window < 3; window++)
          shine_putbits(&config->bs, gi->subblock_gain[window], 3);
      } else {
        shine_putbits(&config->bs, 0, 1);

        for (region = 0; region < 3; region++)
          shine_putbits(&config->bs, gi->table_select[region], 5);

        shine_putbits(&config->bs, gi->region0_count, 4);
        shine_putbits(&config->bs, gi->region1_count, 3);
      }

      if (config->mpeg.version == SHINE_MPEG_I)
        shine_putbits(&config->bs, gi->preflag, 1);
      shine_putbits(&config->bs, gi->scalefac_scale, 1);
      shine_putbits(&config->bs, gi->count1table_select, 1);
    }
}

/* Note the discussion of huffmancodebits() on pages 28 and 29 of the IS, as
  well as the definitions of the side information on pages 26 and 27. */
static void Huffmancodebits(shine_global_config *config, int *ix, gr_info *gi) {
  const int *scalefac =
      &shine_scale_fact_band_index[config->mpeg.samplerate_index][0];
  unsigned scalefac_index;
  int region1Start, region2Start;
  int i, bigvalues, count1End, region, regionEnd[3];
  int v, w, x, y;
  const struct huffcodetab *h;
  int bits;

  bits = shine_get_bits_count(&config->bs);

  /* 1: Write the bigvalues */
  bigvalues = gi->big_values << 1;

  if (gi->block_type) {
    /* ISO 11172-3 2.4.2.7: region 0 ends after 3 short bands or 8 long
     * ones, and there is no region 2. */
    region1Start =
        gi->block_type == BLOCK_SHORT
            ? 3 * shine_scale_fact_band_index_short[config->mpeg
                                                        .samplerate_index][3]
            : scalefac[8];
    region2Start = GRANULE_SIZE;
  } else {
    scalefac_index = gi->region0_count + 1;
    region1Start = scalefac[scalefac_index];
    scalefac_index += gi->region1_count + 1;
    region2Start = scalefac[scalefac_index];
  }

  regionEnd[0] = MIN(region1Start, bigvalues);
  regionEnd[1] = MIN(region2Start, bigvalues);
  regionEnd[2] = bigvalues;
  for (i = 0, region = 0; region < 3; region++) {
    unsigned tableindex = gi->table_select[region];

    if (i >= regionEnd[region])
      continue;
    if (tableindex)
      encodeBigValues(&config->bs, tableindex, ix, i, regionEnd[region]);
    i = regionEnd[region];
  }

  /* 2: Write count1 area */
  h = &shine_huffman_table[gi->count1table_select + 32];
  count1End = bigvalues + (gi->count1 << 2);
  for (i = bigvalues; i < count1End; i += 4) {
    v = ix[i];
    w = ix[i + 1];
    x = ix[i + 2];
    y = ix[i + 3];
    shine_huffman_coder_count1(&config->bs, h, v, w, x, y);
  }

  bits = shine_get_bits_count(&config->bs) - bits;
  bits = gi->part2_3_length - gi->part2_length - bits;
  if (bits) {
    int stuffingWords = bits / 32;
    int remainingBits = bits % 32;

    /* Due to the nature of the Huffman code tables, we will pad with ones */
    while (stuffingWords--)
      shine_putbits(&config->bs, 0xffffffff, 32);
    if (remainingBits)
      shine_putbits(&config->bs, (1UL << remainingBits) - 1, remainingBits);
  }
}

/* A pair or quadruple codeword is followed by, for each of its values in
 * order, the escape linbits when the value is at least 15 on an ESC table,
 * then the sign bit when the value is not 0. l3_enc carries the signs. */
static inline unsigned magnitude(int value) {
  return value < 0 ? -value : value;
}

static inline unsigned sign_bit(int value) { return value < 0; }

static inline unsigned is_nonzero(int value) { return value != 0; }

static void shine_huffman_coder_count1(bitstream_t *bs,
                                       const struct huffcodetab *h, int v,
                                       int w, int x, int y) {
  unsigned p = magnitude(v) + (magnitude(w) << 1) + (magnitude(x) << 2) +
               (magnitude(y) << 3);
  unsigned code = h->table[p];
  unsigned length = h->hlen[p];

  code = code << is_nonzero(v) | sign_bit(v);
  code = code << is_nonzero(w) | sign_bit(w);
  code = code << is_nonzero(x) | sign_bit(x);
  code = code << is_nonzero(y) | sign_bit(y);
  length += is_nonzero(v) + is_nonzero(w) + is_nonzero(x) + is_nonzero(y);
  shine_putbits(bs, code, length);
}

/* Pairs [start, end) of a region coded with a table without linbits. */
static void encodePairs(bitstream_t *bs, const struct huffcodetab *h,
                        const int *ix, int start, int end) {
  const unsigned ylen = h->ylen;

  for (int i = start; i < end; i += 2) {
    int x = ix[i], y = ix[i + 1];
    unsigned idx = magnitude(x) * ylen + magnitude(y);
    unsigned code = h->table[idx];
    unsigned length = h->hlen[idx] + is_nonzero(x) + is_nonzero(y);

    code = code << is_nonzero(x) | sign_bit(x);
    code = code << is_nonzero(y) | sign_bit(y);
    shine_putbits(bs, code, length);
  }
}

/* Appends a value's linbits and sign to an ESC pair's codeword; values from
 * 15 up are coded as 15 in the codeword. */
static inline uint64_t append_escape(uint64_t code, unsigned *length, int value,
                                     unsigned linbits) {
  unsigned m = magnitude(value);

  if (m > 14) {
    code = code << linbits | (m - 15);
    *length += linbits;
  }
  *length += is_nonzero(value);
  return code << is_nonzero(value) | sign_bit(value);
}

/* Pairs [start, end) of a region coded with an ESC table: tables 16 and up
 * are all 16 by 16. */
static void encodeEscapePairs(bitstream_t *bs, const struct huffcodetab *h,
                              const int *ix, int start, int end) {
  const unsigned linbits = h->linbits;

  for (int i = start; i < end; i += 2) {
    int x = ix[i], y = ix[i + 1];
    unsigned idx = MIN(magnitude(x), 15) * 16 + MIN(magnitude(y), 15);
    unsigned length = h->hlen[idx];
    uint64_t code = h->table[idx];

    code = append_escape(code, &length, x, linbits);
    code = append_escape(code, &length, y, linbits);
    shine_putbits(bs, code, length);
  }
}

static void encodeBigValues(bitstream_t *bs, unsigned table_select,
                            const int *ix, int start, int end) {
  const struct huffcodetab *h = &shine_huffman_table[table_select];

  if (table_select > 15)
    encodeEscapePairs(bs, h, ix, start, end);
  else
    encodePairs(bs, h, ix, start, end);
}
