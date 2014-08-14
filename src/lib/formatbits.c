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
#include "layer3.h"

/* forward declarations */
static int  write_side_info(shine_global_config *config);
static void WriteMainDataBits( unsigned int val, unsigned int nbits, shine_global_config *config);

/*
 * WritePartMainData:
 * ------------------
 */
static inline void WritePartMainData(BF_PartHolder *thePH, shine_global_config *config)
{
  BF_BitstreamElement *ep, *end;

  /* assert(thePH); */

  for (ep = thePH->element, end = ep + thePH->nrEntries; ep != end; ep++ )
    WriteMainDataBits( ep->value, ep->length, config );
}

static inline void WitePartSideInfo(BF_PartHolder *thePH, shine_global_config *config)
{
  BF_BitstreamElement *ep, *end;

  /* assert( thePH ); */

  for (ep = thePH->element, end = ep + thePH->nrEntries; ep != end; ep++)
      shine_putbits( &config->bs, ep->value, ep->length);
}

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
  int gr, ch;

  for (gr = 0; gr < config->mpeg.granules_per_frame; gr++)
    for (ch = 0; ch < config->wave.channels; ch++)
      {
        WritePartMainData( &config->l3stream.scaleFactorsPH[gr][ch], config );
        WritePartMainData( &config->l3stream.codedDataPH[gr][ch],    config );
        WritePartMainData( &config->l3stream.userSpectrumPH[gr][ch], config );
      }
  WritePartMainData( &config->l3stream.userFrameDataPH,  config );
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

  if (nbits > config->l3stream.BitsRemaining) {
    if (config->l3stream.BitsRemaining) {
      nbits -= config->l3stream.BitsRemaining;
      extra  = val >> nbits;
      val   &= (1UL << nbits) - 1;
      shine_putbits( &config->bs, extra, config->l3stream.BitsRemaining);
    }

    config->l3stream.BitsRemaining = config->mpeg.bits_per_frame - write_side_info(config);
  }

  shine_putbits( &config->bs, val, nbits);
  config->l3stream.BitsRemaining -= nbits;
}

static int write_side_info(shine_global_config *config)
{
  int bits, ch, gr;

  bits = shine_get_bits_count(&config->bs);

  WitePartSideInfo( &config->l3stream.headerPH,  config );
  WitePartSideInfo( &config->l3stream.frameSIPH, config );

  if ( config->mpeg.version == MPEG_I )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      WitePartSideInfo( &config->l3stream.channelSIPH[ch], config );

  for ( gr = 0; gr < config->mpeg.granules_per_frame; gr++ )
    for ( ch = 0; ch < config->wave.channels; ch++ )
      WitePartSideInfo( &config->l3stream.spectrumSIPH[gr][ch], config );

  return shine_get_bits_count(&config->bs) - bits;
}

/* Allocate a new holder of a given size */
void shine_BF_initPartHolder(BF_PartHolder *thePH, unsigned int max_elements)
{
  thePH->nrEntries = 0;
  thePH->max_elements = max_elements;
  thePH->element = calloc((int)max_elements, sizeof(BF_BitstreamElement));
  /* assert( thePH->element ); */
}

/* Add theElement to thePH, growing the holder if necessary. Returns ptr to the
  holder, which may not be the one you called it with! */
void shine_BF_addElement( BF_PartHolder *thePH, BF_BitstreamElement *theElement )
{
  int needed_entries = thePH->nrEntries + 1;
  const int extraPad = 8;  /* add this many more if we need to resize */

  /* grow if necessary */
  if ( needed_entries > thePH->max_elements ) {
#ifdef DEBUG
    printf("Resizing part holder from %d to %d\n", thePH->max_elements, thePH->max_elements + extraPad );
#endif

    thePH->element = realloc(thePH->element, (thePH->max_elements + extraPad) * sizeof(BF_BitstreamElement));
    /* assert( thePH->element ); */
    thePH->max_elements += extraPad;
  }

  /* copy the data */
  thePH->element[thePH->nrEntries++] = *theElement;
}

/* Add a bit value and length to the element list in thePH */
void shine_BF_addEntry( BF_PartHolder *thePH,
                        unsigned int value,
                        unsigned int length )
{
  if ( length ) {
    BF_BitstreamElement myElement;

    myElement.value  = value;
    myElement.length = length;

    shine_BF_addElement( thePH, &myElement );
  }
}
