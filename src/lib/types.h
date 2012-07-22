#ifndef PRIV_TYPES_H
#define PRIV_TYPES_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <math.h>

#define samp_per_frame2  576

#include "bitstream.h"
#include "priv_layer3.h"

/* #define DEBUG if you want the library to dump info to stdout */

#define false 0
#define true 1

#define PI          3.14159265358979
#define PI4         0.78539816339745
#define PI12        0.26179938779915
#define PI36        0.087266462599717
#define PI64        0.049087385212
#define SQRT2       1.41421356237
#define LN2         0.69314718
#define LN_TO_LOG10 0.2302585093
#define BLKSIZE     1024
#define HAN_SIZE    512
#define SCALE_BLOCK 12
#define SCALE_RANGE 64
#define SCALE       32768
#define SBLIMIT     32

#ifndef bool
typedef unsigned char bool;
#endif

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 2
#endif

#ifndef MAX_GRANULES
#define MAX_GRANULES 2
#endif

/*
  A BitstreamElement contains encoded data
  to be written to the bitstream.
  'length' bits of 'value' will be written to
  the bitstream msb-first.
*/
typedef struct
{
    unsigned long int value;
    unsigned int length;
} BF_BitstreamElement;

/*
  A BitstreamPart contains a group
  of 'nrEntries' of BitstreamElements.
  Each BitstreamElement will be written
  to the bitstream in the order it appears
  in the 'element' array.
*/
typedef struct
{
    unsigned long int nrEntries;
    BF_BitstreamElement *element;
} BF_BitstreamPart;

/*
  This structure contains all the information needed by the
  bitstream formatter to encode one frame of data. You must
  fill this out and provide a pointer to it when you call
  the formatter.
  Maintainers: If you add or remove part of the side
  information, you will have to update the routines that
  make local copies of that information (in formatBitstream.c)
*/

typedef struct BF_FrameData
{
    int              frameLength;
    int              nGranules;
    int              nChannels;
    BF_BitstreamPart *header;
    BF_BitstreamPart *frameSI;
    BF_BitstreamPart *channelSI[MAX_CHANNELS];
    BF_BitstreamPart *spectrumSI[MAX_GRANULES][MAX_CHANNELS];
    BF_BitstreamPart *scaleFactors[MAX_GRANULES][MAX_CHANNELS];
    BF_BitstreamPart *codedData[MAX_GRANULES][MAX_CHANNELS];
    BF_BitstreamPart *userSpectrum[MAX_GRANULES][MAX_CHANNELS];
    BF_BitstreamPart *userFrameData;
} BF_FrameData;

/*
  This structure contains information provided by
  the bitstream formatter. You can use this to
  check to see if your code agrees with the results
  of the call to the formatter.
*/
typedef struct BF_FrameResults
{
    int SILength;
    int mainDataLength;
    int nextBackPtr;
} BF_FrameResults;

typedef struct BF_PartHolder
{
    int max_elements;
    BF_BitstreamPart *part;
} BF_PartHolder;

typedef struct {
    int    mode;      /* + */ /* Stereo mode */
    int    bitr;      /* + */ /* Must conform to known bitrate - see Main.c */
    int    emph;      /* + */ /* De-emphasis */
    int    padding;
    long   bits_per_frame;
    long   bits_per_slot;
    double frac_slots_per_frame;
    double slot_lag;
    int    whole_slots_per_frame;
    int    bitrate_index;     /* + */ /* See Main.c and Layer3.c */
    int    samplerate_index;  /* + */ /* See Main.c and Layer3.c */
    int    crc;
    int    ext;
    int    mode_ext;
    int    copyright;  /* + */
    int    original;   /* + */
} priv_mpeg_t;

typedef struct {
    int BitCount;
    int ThisFrameSize;
    int BitsRemaining;
} formatbits_t;

typedef struct {
  BF_FrameData    frameData;
  BF_FrameResults frameResults;

  BF_PartHolder *headerPH;
  BF_PartHolder *frameSIPH;
  BF_PartHolder *channelSIPH[ MAX_CHANNELS ];
  BF_PartHolder *spectrumSIPH[ MAX_GRANULES ][ MAX_CHANNELS ];
  BF_PartHolder *scaleFactorsPH[ MAX_GRANULES ][ MAX_CHANNELS ];
  BF_PartHolder *codedDataPH[ MAX_GRANULES ][ MAX_CHANNELS ];
  BF_PartHolder *userSpectrumPH[ MAX_GRANULES ][ MAX_CHANNELS ];
  BF_PartHolder *userFrameDataPH;
} l3stream_t;

typedef struct shine_global_flags { 
  wave_t         wave;
  priv_mpeg_t    mpeg;
  bitstream_t    bs;
  L3_side_info_t side_info;
  int            sideinfo_len;
  int            mean_bits;
  L3_psy_ratio_t ratio;
  L3_scalefac_t  scalefactor;
  int16_t       *buffer[2];
  double         pe[2][2];
  int            l3_enc[2][2][samp_per_frame2];
  long           l3_sb_sample[2][3][18][SBLIMIT];
  long           mdct_freq[2][2][samp_per_frame2];
  formatbits_t   formatbits;
  l3stream_t     l3stream;
} shine_global_config;

#endif
