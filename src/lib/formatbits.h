#ifndef _FORMAT_BITSTREAM_H
#define _FORMAT_BITSTREAM_H
/*********************************************************************
  Copyright (c) 1995 ISO/IEC JTC1 SC29 WG1
  formatBitstream.h
**********************************************************************/

/*
  Revision History:

  Date        Programmer                Comment
  ==========  ========================= ===============================
  1995/09/06  mc@fivebats.com           created
  2012/26/07  toots@rastageeks.org      clarified license

*/

#include "types.h"

/*
  The following is a shorthand bitstream syntax for
  the type of bitstream this package will create.
  The bitstream has headers and side information that
  are placed at appropriate sections to allow framing.
  The main data is placed where it fits in a manner
  similar to layer3, which means that main data for a
  frame may be written to the bitstream before the
  frame's header and side information is written.

BitstreamFrame()
{
    Header();
    FrameSI();

    for ( ch )
        ChannelSI();

    for ( gr )
        for ( ch )
            SpectrumSI();

    MainData();
}

MainData()
{
    for ( gr )
        for ( ch )
        {
            Scalefactors();
            CodedData();
            UserSpectrum();
        }
    UserFrameData();
}

*/

/*
  public functions in formatBitstream.c
*/

/* encode a frame of audio and write it to your bitstream */
void shine_BF_BitstreamFrame( shine_global_config *config);

void shine_BF_initPartHolder( BF_PartHolder *thePH, unsigned int max_elements );
void shine_BF_addEntry( BF_PartHolder *thePH, unsigned int value, unsigned int length );

#endif
