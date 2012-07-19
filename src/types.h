#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>

/* #define DEBUG if you want the library to dump info to stdout */

#define samp_per_frame  1152
#define samp_per_frame2  576

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


/* In the wave_t struct below, values marked with a '+' are necessary.
   Everything else is optional and for the calling apps read
   and write vectors convenience */

/* This is the struct used to tell the encoder about the input PCM */

typedef struct {
    FILE *file;
    int  type;
    int  channels;      /* + */
    int  bits; 
    long samplerate;    /* + */
    long total_samples; /* + */
    long length;
} wave_t;



/* In the mpeg_t struct below, values marked with a '+' 
   actually control something the calling app might want
   to change.  The rest are internal values.  See set_defaults()
   in shineenc Main.c for example init.
*/

/* This is the struct the encoder uses to hold informationn about the output MP3 */

typedef struct {
    FILE *file;
    int  type;
    int  layr;      
    int  mode;      /* + */ /* Stereo mode */
    int  bitr;      /* + */ /* Must conform to known bitrate - see Main.c */
    int  psyc;      /* + */ /* Which psy model - see Main.c */
    int  emph;      /* + */ /* De-emphasis */
    int  padding;
    long samples_per_frame;
    long bits_per_frame;
    long bits_per_slot;
    long total_frames;
    int  bitrate_index;     /* + */ /* See Main.c and Layer3.c */
    int  samplerate_index;  /* + */ /* See Main.c and Layer3.c */
    int crc;
    int ext;
    int mode_ext;
    int copyright;  /* + */
    int original;   /* + */
} mpeg_t;


typedef struct {
    time_t start_time;

    char* infile;   /* For calling app's convenience */
    wave_t wave;

    char* outfile;  /* For calling app's convenience */
    mpeg_t mpeg;
    
    /* These two app-supplied routines are used to read and write data */
    int  (*get_pcm)(short buffer[2][samp_per_frame], void *config_in);
    int  (*write_mp3)(long bytes, void *buffer, void *config_in); 
    
    void *user; /* For the calling app's convenience */
    
} config_t;



#ifndef bool
typedef unsigned char bool;
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#define WAVE_RIFF_PCM 0
#define WAVE_PCM_LOHI 1
#define WAVE_PCM_HILO 2
#define WAVE_PCM_AIFF 3

#define MODE_MONO     3

void error(char* s);

#endif
