#ifndef LAYER3_H
#define LAYER3_H

#include <stdint.h>

#define samp_per_frame  1152

/* Valid samplerates and bitrates. */
static long samplerates[3] = {44100, 48000, 32000};
static int  bitrates[15]   = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};

/* This is the struct used to tell the encoder about the input PCM */

enum channels {
  PCM_MONO   = 1,
  PCM_STEREO = 2
};

typedef struct {
    enum channels channels;
    long          samplerate;
} shine_wave_t;

/* This is the struct the encoder uses to hold informationn about the output MP3 */

enum modes {
  STEREO       = 0,
  JOINT_STEREO = 1,
  DUAL_CHANNEL = 2,
  MONO         = 3 
};

enum emph {
  NONE    = 0,
  MU50_15 = 1,
  CITT    = 3
};

typedef struct {
    enum modes mode;      /* Stereo mode */
    int        bitr;      /* Must conform to known bitrate - see Main.c */
    enum emph  emph;      /* De-emphasis */
    int        copyright;
    int        original;
} shine_mpeg_t;

typedef struct {
  shine_wave_t wave;
  shine_mpeg_t mpeg;
} shine_config_t;

typedef struct shine_global_flags *shine_t;

void L3_set_config_mpeg_defaults(shine_mpeg_t *mpeg);
int L3_find_bitrate_index(int bitr);
int L3_find_samplerate_index(long freq);

shine_t L3_initialise(shine_config_t *config);

unsigned char *L3_encode_frame(shine_t s, int16_t data[2][samp_per_frame], long *written);

void L3_close(shine_t s);

#endif
