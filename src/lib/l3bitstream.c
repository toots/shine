/* l3bitstrea.c */

#include "types.h"
#include "l3mdct.h"
#include "l3loop.h"
#include "layer3.h"
#include "formatbits.h"
#include "huffman.h"
#include "bitstream.h"
#include "tables.h"
#include "l3bitstream.h" /* the public interface */

int shine_HuffmanCode(int table_select, int x, int y, unsigned int *code,
                unsigned int *ext, int *cbits, int *xbits );

void shine_bitstream_initialise( shine_global_config *config )
{
  int ch, gr;

  config->l3stream.headerPH = shine_BF_newPartHolder( 12 );
  config->l3stream.frameSIPH = shine_BF_newPartHolder( 12 );

  for ( ch = 0; ch < MAX_CHANNELS; ch++ )
     config->l3stream.channelSIPH[ch] = shine_BF_newPartHolder( 8 );

  for ( gr = 0; gr < MAX_GRANULES; gr++ )
    for ( ch = 0; ch < MAX_CHANNELS; ch++ )
      {
       config->l3stream.spectrumSIPH[gr][ch]   = shine_BF_newPartHolder( 32 );
       config->l3stream.scaleFactorsPH[gr][ch] = shine_BF_newPartHolder( 64 );
       config->l3stream.codedDataPH[gr][ch]    = shine_BF_newPartHolder( GRANULE_SIZE );
       config->l3stream.userSpectrumPH[gr][ch] = shine_BF_newPartHolder( 4 );
      }
  config->l3stream.userFrameDataPH = shine_BF_newPartHolder( 8 );
}

void shine_bitstream_close( shine_global_config *config )
{
  int ch, gr;

  shine_BF_freePartHolder(config->l3stream.headerPH);
  shine_BF_freePartHolder(config->l3stream.frameSIPH);

  for ( ch = 0; ch < MAX_CHANNELS; ch++ )
     shine_BF_freePartHolder(config->l3stream.channelSIPH[ch]);

  for ( gr = 0; gr < MAX_GRANULES; gr++ )
    for ( ch = 0; ch < MAX_CHANNELS; ch++ )
      {
       shine_BF_freePartHolder(config->l3stream.spectrumSIPH[gr][ch]);
       shine_BF_freePartHolder(config->l3stream.scaleFactorsPH[gr][ch]);
       shine_BF_freePartHolder(config->l3stream.codedDataPH[gr][ch]);
       shine_BF_freePartHolder(config->l3stream.userSpectrumPH[gr][ch]);
      }
  shine_BF_freePartHolder(config->l3stream.userFrameDataPH);
}

static void encodeSideInfo( shine_global_config *config );
static void encodeMainData( shine_global_config *config );
static void Huffmancodebits( BF_PartHolder **pph, int *ix, gr_info *gi , shine_global_config *config);

/*
  shine_format_bitstream()

  This is called after a frame of audio has been quantized and coded.
  It will write the encoded audio to the bitstream. Note that
  from a layer3 encoder's perspective the bit stream is primarily
  a series of main_data() blocks, with header and side information
  inserted at the proper locations to maintain framing. (See Figure A.7
  in the IS).
*/

void
shine_format_bitstream(shine_global_config *config)
{
  int gr, ch, i;

  for ( ch =  0; ch < config->wave.channels; ch++ )
    for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
      {
        int *pi = &config->l3_enc[ch][gr][0];
        int32_t *pr = &config->mdct_freq[ch][gr][0];
        for ( i = 0; i < GRANULE_SIZE; i++ )
          {
            if ( (pr[i] < 0) && (pi[i] > 0) )
              pi[i] *= -1;
          }
      }

  encodeSideInfo( config );
  encodeMainData( config );

  config->l3stream.frameData.header  = config->l3stream.headerPH->part;
  config->l3stream.frameData.frameSI = config->l3stream.frameSIPH->part;

  for ( ch = 0; ch < config->wave.channels; ch++ )
    config->l3stream.frameData.channelSI[ch] = config->l3stream.channelSIPH[ch]->part;

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      {
        config->l3stream.frameData.spectrumSI[gr][ch]   = config->l3stream.spectrumSIPH[gr][ch]->part;
        config->l3stream.frameData.scaleFactors[gr][ch] = config->l3stream.scaleFactorsPH[gr][ch]->part;
        config->l3stream.frameData.codedData[gr][ch]    = config->l3stream.codedDataPH[gr][ch]->part;
        config->l3stream.frameData.userSpectrum[gr][ch] = config->l3stream.userSpectrumPH[gr][ch]->part;
      }
  config->l3stream.frameData.userFrameData = config->l3stream.userFrameDataPH->part;

  shine_BF_BitstreamFrame(config);
}

static void encodeMainData(shine_global_config *config)
{
  int gr, ch, sfb;
  shine_side_info_t  si = config->side_info;

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      {
        config->l3stream.scaleFactorsPH[gr][ch]->part->nrEntries = 0;
        config->l3stream.codedDataPH[gr][ch]->part->nrEntries = 0;
      }

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    {
      for ( ch = 0; ch < config->wave.channels; ch++ )
        {
          BF_PartHolder **pph = &config->l3stream.scaleFactorsPH[gr][ch];
          gr_info *gi = &(si.gr[gr].ch[ch].tt);
          unsigned slen1 = slen1_tab[ gi->scalefac_compress ];
          unsigned slen2 = slen2_tab[ gi->scalefac_compress ];
          int *ix = &config->l3_enc[ch][gr][0];

          if ( (gr == 0) || (si.scfsi[ch][0] == 0) )
            for ( sfb = 0; sfb < 6; sfb++ )
              *pph = shine_BF_addEntry( *pph,  config->scalefactor.l[gr][ch][sfb], slen1 );

          if ( (gr == 0) || (si.scfsi[ch][1] == 0) )
            for ( sfb = 6; sfb < 11; sfb++ )
              *pph = shine_BF_addEntry( *pph,  config->scalefactor.l[gr][ch][sfb], slen1 );

          if ( (gr == 0) || (si.scfsi[ch][2] == 0) )
            for ( sfb = 11; sfb < 16; sfb++ )
              *pph = shine_BF_addEntry( *pph,  config->scalefactor.l[gr][ch][sfb], slen2 );

          if ( (gr == 0) || (si.scfsi[ch][3] == 0) )
            for ( sfb = 16; sfb < 21; sfb++ )
              *pph = shine_BF_addEntry( *pph,  config->scalefactor.l[gr][ch][sfb], slen2 );

          Huffmancodebits( &config->l3stream.codedDataPH[gr][ch], ix, gi, config );
        }
    }
}

static void encodeSideInfo( shine_global_config *config )
{
  int gr, ch, scfsi_band, region;
  shine_side_info_t  si = config->side_info;

  config->l3stream.headerPH->part->nrEntries = 0;
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, 0xfff,                             11 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.version,              2 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.layer,                2 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, !config->mpeg.crc,                 1 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.bitrate_index,        4 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.samplerate_index % 3, 2 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.padding,              1 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.ext,                  1 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.mode,                 2 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.mode_ext,             2 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.copyright,            1 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.original,             1 );
  config->l3stream.headerPH = shine_BF_addEntry( config->l3stream.headerPH, config->mpeg.emph,                 2 );

  config->l3stream.frameSIPH->part->nrEntries = 0;

  for (ch = 0; ch < config->wave.channels; ch++ )
    config->l3stream.channelSIPH[ch]->part->nrEntries = 0;

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      config->l3stream.spectrumSIPH[gr][ch]->part->nrEntries = 0;

  if ( config->mpeg.version == MPEG_I )
    config->l3stream.frameSIPH = shine_BF_addEntry( config->l3stream.frameSIPH, 0, 9 );
  else
    config->l3stream.frameSIPH = shine_BF_addEntry( config->l3stream.frameSIPH, 0, 8 );

  if ( config->wave.channels == 2 )
    if ( config->mpeg.version == MPEG_I )
      config->l3stream.frameSIPH = shine_BF_addEntry( config->l3stream.frameSIPH, si.private_bits, 3 );
    else
      config->l3stream.frameSIPH = shine_BF_addEntry( config->l3stream.frameSIPH, si.private_bits, 2 );
  else
    if ( config->mpeg.version == MPEG_I )
      config->l3stream.frameSIPH = shine_BF_addEntry( config->l3stream.frameSIPH, si.private_bits, 5 );
    else
      config->l3stream.frameSIPH = shine_BF_addEntry( config->l3stream.frameSIPH, si.private_bits, 1 );

  if ( config->mpeg.version == MPEG_I )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      for ( scfsi_band = 0; scfsi_band < 4; scfsi_band++ )
        {
          BF_PartHolder **pph = &config->l3stream.channelSIPH[ch];
          *pph = shine_BF_addEntry( *pph, si.scfsi[ch][scfsi_band], 1 );
        }

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    for ( ch = 0; ch < config->wave.channels ; ch++ )
      {
        BF_PartHolder **pph = &config->l3stream.spectrumSIPH[gr][ch];
        gr_info *gi = &(si.gr[gr].ch[ch].tt);
        *pph = shine_BF_addEntry( *pph, gi->part2_3_length,        12 );
        *pph = shine_BF_addEntry( *pph, gi->big_values,            9 );
        *pph = shine_BF_addEntry( *pph, gi->global_gain,           8 );
        if ( config->mpeg.version == MPEG_I )
          *pph = shine_BF_addEntry( *pph, gi->scalefac_compress,   4 );
        else
          *pph = shine_BF_addEntry( *pph, gi->scalefac_compress,   9 );
        *pph = shine_BF_addEntry( *pph, 0, 1 );

        for ( region = 0; region < 3; region++ )
          *pph = shine_BF_addEntry( *pph, gi->table_select[region], 5 );

        *pph = shine_BF_addEntry( *pph, gi->region0_count, 4 );
        *pph = shine_BF_addEntry( *pph, gi->region1_count, 3 );

        if ( config->mpeg.version == MPEG_I )
          *pph = shine_BF_addEntry( *pph, gi->preflag,            1 );
        *pph = shine_BF_addEntry( *pph, gi->scalefac_scale,     1 );
        *pph = shine_BF_addEntry( *pph, gi->count1table_select, 1 );
      }
}

/* Note the discussion of huffmancodebits() on pages 28 and 29 of the IS, as
  well as the definitions of the side information on pages 26 and 27. */
static void Huffmancodebits( BF_PartHolder **pph, int *ix, gr_info *gi, shine_global_config *config )
{
  int shine_huffman_coder_count1( BF_PartHolder **pph, const struct huffcodetab *h, int v, int w, int x, int y );
  int bigv_bitcount( int ix[GRANULE_SIZE], gr_info *cod_info );

  int region1Start;
  int region2Start;
  int i, bigvalues, count1End;
  int v, w, x, y, bits, cbits, xbits, stuffingBits;
  unsigned int code, ext;
  const struct huffcodetab *h;
  int tablezeros = 0, r[3] = { 0 }, rt, *pr;
  int bitsWritten = 0;

  /* 1: Write the bigvalues */
  bigvalues = gi->big_values <<1;

  const int *scalefac = &shine_scale_fact_band_index[config->mpeg.samplerate_index][0];
  unsigned scalefac_index = 100;

  scalefac_index = gi->region0_count + 1;
  region1Start = scalefac[ scalefac_index ];
  scalefac_index += gi->region1_count + 1;
  region2Start = scalefac[ scalefac_index ];

  for ( i = 0; i < bigvalues; i += 2 )
    {
      /* get table pointer */
      int idx = (i >= region1Start) + (i >= region2Start);
      unsigned tableindex = gi->table_select[idx];
      pr = &r[idx];
      h = &shine_huffman_table[ tableindex ];
      /* get huffman code */
      x = ix[i];
      y = ix[i + 1];
      if ( tableindex )
        {
          bits = shine_HuffmanCode( tableindex, x, y, &code, &ext, &cbits, &xbits );
          *pph = shine_BF_addEntry( *pph,  code, cbits );
          *pph = shine_BF_addEntry( *pph,  ext, xbits );
          bitsWritten += rt = bits;
          *pr += rt;
        }
      else
        {
          tablezeros += 1;
          *pr = 0;
        }
    }

  /* 2: Write count1 area */
  h = &shine_huffman_table[gi->count1table_select + 32];
  count1End = bigvalues + (gi->count1 <<2);
  for ( i = bigvalues; i < count1End; i += 4 )
    {
      v = ix[i];
      w = ix[i+1];
      x = ix[i+2];
      y = ix[i+3];
      bitsWritten += shine_huffman_coder_count1( pph, h, v, w, x, y );
    }
  if ( (stuffingBits = gi->part2_3_length - gi->part2_length - bitsWritten) )
    {
      int stuffingWords = stuffingBits / 32;
      int remainingBits = stuffingBits % 32;

      /* Due to the nature of the Huffman code tables, we will pad with ones */
      while ( stuffingWords-- )
        *pph = shine_BF_addEntry( *pph, ~0, 32 );
      if ( remainingBits )
        *pph = shine_BF_addEntry( *pph, ~0, remainingBits );
      bitsWritten += stuffingBits;
    }
}

static inline int shine_abs_and_sign( int *x )
{
  if ( *x > 0 ) return 0;
  *x *= -1;
  return 1;
}

int shine_huffman_coder_count1( BF_PartHolder **pph, const struct huffcodetab *h, int v, int w, int x, int y )
{
  HUFFBITS huffbits;
  unsigned int signv, signw, signx, signy, p;
  int len;
  int totalBits = 0;

  signv = shine_abs_and_sign( &v );
  signw = shine_abs_and_sign( &w );
  signx = shine_abs_and_sign( &x );
  signy = shine_abs_and_sign( &y );

  p = v + (w << 1) + (x << 2) + (y << 3);
  huffbits = h->table[p];
  len = h->hlen[ p ];
  *pph = shine_BF_addEntry( *pph,  huffbits, len );
  totalBits += len;
  if ( v )
    {
      *pph = shine_BF_addEntry( *pph,  signv, 1 );
      totalBits += 1;
    }
  if ( w )
    {
      *pph = shine_BF_addEntry( *pph,  signw, 1 );
      totalBits += 1;
    }
  if ( x )
    {
      *pph = shine_BF_addEntry( *pph,  signx, 1 );
      totalBits += 1;
    }
  if ( y )
    {
      *pph = shine_BF_addEntry( *pph,  signy, 1 );
      totalBits += 1;
    }
  return totalBits;
}

/* Implements the pseudocode of page 98 of the IS */
int shine_HuffmanCode(int table_select, int x, int y, unsigned int *code,
                unsigned int *ext, int *cbits, int *xbits )
{
  unsigned signx, signy, linbitsx, linbitsy, linbits, ylen, idx;
  const struct huffcodetab *h;

  *cbits = 0;
  *xbits = 0;
  *code  = 0;
  *ext   = 0;

  if (table_select==0) return 0;

  signx = shine_abs_and_sign( &x );
  signy = shine_abs_and_sign( &y );
  h = &(shine_huffman_table[table_select]);
  ylen = h->ylen;
  linbits = h->linbits;
  linbitsx = linbitsy = 0;

  if ( table_select > 15 )
    { /* ESC-table is used */
      if ( x > 14 )
        {
          linbitsx = x - 15;
          x = 15;
        }
      if ( y > 14 )
        {
          linbitsy = y - 15;
          y = 15;
        }

      idx = (x * ylen) + y;
      *code  = h->table[idx];
      *cbits = h->hlen [idx];
      if ( x > 14 )
        {
          *ext   |= linbitsx;
          *xbits += linbits;
        }
      if ( x != 0 )
        {
          *ext <<= 1;
          *ext |= signx;
          *xbits += 1;
        }
      if ( y > 14 )
        {
          *ext <<= linbits;
          *ext |= linbitsy;
          *xbits += linbits;
        }
      if ( y != 0 )
        {
          *ext <<= 1;
          *ext |= signy;
          *xbits += 1;
        }
    }
  else
    { /* No ESC-words */
      idx = (x * ylen) + y;
      *code = h->table[idx];
      *cbits += h->hlen[ idx ];
      if ( x != 0 )
        {
          *code <<= 1;
          *code |= signx;
          *cbits += 1;
        }
      if ( y != 0 )
        {
          *code <<= 1;
          *code |= signy;
          *cbits += 1;
        }
    }
  return *cbits + *xbits;
}

