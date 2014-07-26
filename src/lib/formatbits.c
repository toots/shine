/*********************************************************************
  Copyright (c) 1995 ISO/IEC JTC1 SC29 WG1
  formatBitstream.c
**********************************************************************/
/*
  Revision History:

  Date        Programmer                Comment
  ==========  ========================= ===============================
  1995/09/06  mc@fivebats.com           created
  1995/09/18  mc@fivebats.com           bugfix: WriteMainDataBits
  1995/09/20  mc@fivebats.com           bugfix: store_side_info
  2012/07/26  toots@rastageeks.org      clarified license.
*/

/* In email <CAKheQp9H5-8eUeXtCahAhS-0pKOW1kZVC9xY0D0oLVfE_kPPMA@mail.gmail.com>,
 * Mike Coleman <mc@fivebats.com> added the following regarding the code in this file:
 * 
 * "As I recall, the intention of the ISO at the time was to be permissive
 * with the code -- far more permissive than LGPL. In other words, do
 * what you want with the code."
 *
 * I have thus removed a "All Rights Reserved" sentence above. I am leaving this file
 * without a license header as it could also be re-licensed with a more liberal
 * license by other interested parties.
 *   -- Romain Beauxis <toots@rastageeks.org> Thu Jul 26 19:20:11 CDT 2012 */

/*#define DEBUG*/

#include "types.h"
#include "formatbits.h"

void shine_formatbits_initialise(shine_global_config *config)
{
  int ch, gr;

  config->formatbits.BitCount            = 0;
  config->formatbits.BitsRemaining       = config->mpeg.bits_per_frame;
  config->formatbits.side_info.headerPH  = NULL;
  config->formatbits.side_info.frameSIPH = NULL;

  for ( ch = 0; ch < config->wave.channels; ch++ ) {
    config->formatbits.side_info.channelSIPH[ch] = NULL;
    for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
      config->formatbits.side_info.spectrumSIPH[gr][ch] = NULL;
  }
}

void shine_formatbits_close(shine_global_config *config)
{
  int ch, gr;

  shine_BF_freePartHolder(config->formatbits.side_info.headerPH);
    
  shine_BF_freePartHolder(config->formatbits.side_info.frameSIPH);

  for ( ch = 0; ch < config->wave.channels; ch++ ) {
    shine_BF_freePartHolder(config->formatbits.side_info.channelSIPH[ch]);
        
    for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
      shine_BF_freePartHolder(config->formatbits.side_info.spectrumSIPH[gr][ch]);
  }
}

/* forward declarations */
static void           store_side_info( shine_global_config *config );
static int            write_side_info(shine_global_config *config);
static int            WitePartSideInfo(BF_BitstreamPart *part, shine_global_config *config);

static void           main_data( shine_global_config *config);
static void           WriteMainDataBits( unsigned int val, unsigned int nbits, shine_global_config *config);
static void           WritePartMainData(BF_BitstreamPart *part, shine_global_config *config);

static BF_PartHolder *BF_LoadHolderFromBitstreamPart( BF_PartHolder *theHolder, BF_BitstreamPart *thePart );

/*
 * BF_BitStreamFrame:
 * ------------------
 * This is the public interface to the bitstream
 * formatting package. It writes one frame of main data per call.
 *
 * Assumptions:
 * - The back pointer is zero on the first call
 * - An integral number of bytes is written each frame
 *
 * You should be able to change the frame length, side info
 * length, #channels, #granules on a frame-by-frame basis.
 *
 * See formatBitstream.h for more information about the data
 * structures and the bitstream syntax.
 */
void shine_BF_BitstreamFrame(shine_global_config *config)
{
  /* get ptr to bit writing function */
  /* save SI and compute its length */
  store_side_info( config );

  /* write the main data, inserting SI to maintain framing */
  main_data( config );

  /*
   * Caller must ensure that back SI and main data are
   * an integral number of bytes, since the back pointer
   * can only point to a byte boundary and this code
   * does not add stuffing bits
   */
  /* assert( (BitsRemaining % 8) == 0 );*/
}

/*
 * WritePartMainData:
 * ------------------
 */
static void WritePartMainData(BF_BitstreamPart *part, shine_global_config *config)
{
  BF_BitstreamElement *ep;
  int i;

  /* assert(part); */

  ep = part->element;
  for ( i = 0; i < part->nrEntries; i++, ep++ )
    WriteMainDataBits( ep->value, ep->length, config );
}

static int WitePartSideInfo(BF_BitstreamPart *part, shine_global_config *config)
{
  BF_BitstreamElement *ep;
  int i, bits;

  /* assert( part ); */

  bits = 0;
  ep = part->element;
  for ( i = 0; i < part->nrEntries; i++, ep++ )
    {
      shine_putbits( &config->bs, ep->value, ep->length);
      bits += ep->length;
    }
  return bits;
}

static void main_data(shine_global_config *config)
{
  int gr, ch;

  for (gr = 0; gr < config->mpeg.granules_per_frame; gr++)
    for (ch = 0; ch < config->wave.channels; ch++)
      {
        WritePartMainData( config->l3stream.frameData.scaleFactors[gr][ch], config );
        WritePartMainData( config->l3stream.frameData.codedData[gr][ch],    config );
        WritePartMainData( config->l3stream.frameData.userSpectrum[gr][ch], config );
      }
  WritePartMainData( config->l3stream.frameData.userFrameData,  config );
}

/*
  This is a wrapper around PutBits() that makes sure that the
  framing header and side info are inserted at the proper
  locations
*/

static void WriteMainDataBits(unsigned int val,
                              unsigned int nbits,
                              shine_global_config *config)
{
  unsigned int extra;

  /* assert( nbits <= 32 ); */
  if (config->formatbits.BitCount == config->mpeg.bits_per_frame)
    {
      config->formatbits.BitCount      = write_side_info(config);
      config->formatbits.BitsRemaining = config->mpeg.bits_per_frame - config->formatbits.BitCount;
    }
  if (nbits == 0) return;
  if (nbits > config->formatbits.BitsRemaining)
    {
      extra  = val >> (nbits - config->formatbits.BitsRemaining);
      nbits -= config->formatbits.BitsRemaining;
      shine_putbits( &config->bs, extra, config->formatbits.BitsRemaining);
      config->formatbits.BitCount = write_side_info(config);
      config->formatbits.BitsRemaining = config->mpeg.bits_per_frame - config->formatbits.BitCount;
    }
  shine_putbits( &config->bs, val, nbits);

  config->formatbits.BitCount      += nbits;
  config->formatbits.BitsRemaining -= nbits;
}

static int write_side_info(shine_global_config *config)
{
  int bits, ch, gr;

  bits = 0;

  bits += WitePartSideInfo( config->formatbits.side_info.headerPH->part,  config );
  bits += WitePartSideInfo( config->formatbits.side_info.frameSIPH->part, config );

  for ( ch = 0; ch < config->wave.channels; ch++ )
    bits += WitePartSideInfo( config->formatbits.side_info.channelSIPH[ch]->part, config );

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      bits += WitePartSideInfo( config->formatbits.side_info.spectrumSIPH[gr][ch]->part, config );
  return bits;
}

static void store_side_info(shine_global_config *config)
{
  int ch, gr;

  if (config->formatbits.side_info.headerPH == NULL) { /* must allocate another */
      config->formatbits.side_info.headerPH  = shine_BF_newPartHolder( config->l3stream.frameData.header->nrEntries );
      config->formatbits.side_info.frameSIPH = shine_BF_newPartHolder( config->l3stream.frameData.frameSI->nrEntries );

      for ( ch = 0; ch < config->wave.channels; ch++ ) {
        config->formatbits.side_info.channelSIPH[ch] = shine_BF_newPartHolder( config->l3stream.frameData.channelSI[ch]->nrEntries );
        for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
          config->formatbits.side_info.spectrumSIPH[gr][ch] = shine_BF_newPartHolder( config->l3stream.frameData.spectrumSI[gr][ch]->nrEntries );
      }
  }

  /* copy data */
  config->formatbits.side_info.headerPH  = BF_LoadHolderFromBitstreamPart( config->formatbits.side_info.headerPH,  config->l3stream.frameData.header );
  config->formatbits.side_info.frameSIPH = BF_LoadHolderFromBitstreamPart( config->formatbits.side_info.frameSIPH, config->l3stream.frameData.frameSI );

  for ( ch = 0; ch < config->wave.channels; ch++ ) {
    config->formatbits.side_info.channelSIPH[ch] = BF_LoadHolderFromBitstreamPart(config->formatbits.side_info.channelSIPH[ch], config->l3stream.frameData.channelSI[ch]);
    for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
      config->formatbits.side_info.spectrumSIPH[gr][ch] = BF_LoadHolderFromBitstreamPart(config->formatbits.side_info.spectrumSIPH[gr][ch], config->l3stream.frameData.spectrumSI[gr][ch]);
  }
}

/* Allocate a new holder of a given size */
BF_PartHolder *shine_BF_newPartHolder(unsigned int max_elements)
{
  BF_PartHolder *newPH = calloc(1, sizeof(BF_PartHolder));
  /* assert( newPH ); */
  newPH->max_elements = max_elements;
  newPH->part = calloc(1, sizeof(BF_BitstreamPart));
  /* assert( newPH->part ); */
  newPH->part->element = calloc((int)max_elements, sizeof(BF_BitstreamElement));
  /* assert( newPH->part->element ); */
  newPH->part->nrEntries = 0;
  return newPH;
}

BF_PartHolder *shine_BF_NewHolderFromBitstreamPart( BF_BitstreamPart *thePart )
{
  BF_PartHolder *newHolder = shine_BF_newPartHolder( thePart->nrEntries );
  return BF_LoadHolderFromBitstreamPart( newHolder, thePart );
}

static BF_PartHolder *BF_LoadHolderFromBitstreamPart( BF_PartHolder *theHolder, BF_BitstreamPart *thePart )
{
  BF_BitstreamElement *pElem;
  int i;

  theHolder->part->nrEntries = 0;
  for ( i = 0; i < thePart->nrEntries; i++ )
    {
      pElem = &(thePart->element[i]);
      theHolder = shine_BF_addElement( theHolder, pElem );
    }
  return theHolder;
}

/* Grow or shrink a part holder. Always creates a new one of the right length
  and frees the old one after copying the data. */
BF_PartHolder *shine_BF_resizePartHolder( BF_PartHolder *oldPH, int max_elements )
{
  int elems;
  BF_PartHolder *newPH;

#ifdef DEBUG
  printf("Resizing part holder from %d to %d\n", oldPH->max_elements, max_elements );
#endif
  /* create new holder of the right length */
  newPH = shine_BF_newPartHolder( max_elements );

  /* copy values from old to new */
  elems = (oldPH->max_elements > max_elements) ? max_elements : oldPH->max_elements;
  newPH->part->nrEntries = elems;
  memcpy(newPH->part->element, oldPH->part->element, elems * sizeof(newPH->part->element[0]));

  /* free old holder */
  shine_BF_freePartHolder( oldPH );

  return newPH;
}

BF_PartHolder *shine_BF_freePartHolder( BF_PartHolder *thePH )
{
  if (thePH == NULL) return NULL;

  free( thePH->part->element );
  free( thePH->part );
  free( thePH );
  return NULL;
}

/* Add theElement to thePH, growing the holder if necessary. Returns ptr to the
  holder, which may not be the one you called it with! */
BF_PartHolder *shine_BF_addElement( BF_PartHolder *thePH, BF_BitstreamElement *theElement )
{
  BF_PartHolder *retPH = thePH;
  int needed_entries = thePH->part->nrEntries + 1;
  int extraPad = 8;  /* add this many more if we need to resize */

  /* grow if necessary */
  if ( needed_entries > thePH->max_elements )
    retPH = shine_BF_resizePartHolder( thePH, needed_entries + extraPad );

  /* copy the data */
  retPH->part->element[retPH->part->nrEntries++] = *theElement;
  return retPH;
}

/* Add a bit value and length to the element list in thePH */
BF_PartHolder *shine_BF_addEntry( BF_PartHolder *thePH,
                            unsigned int value,
                            unsigned int length )
{
  BF_BitstreamElement myElement;
  myElement.value  = value;
  myElement.length = length;
  if ( length )
    return shine_BF_addElement( thePH, &myElement );
  else
    return thePH;
}
