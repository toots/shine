#ifndef LAYER3_H
#define LAYER3_H

#define samp_per_frame  1152

/* Valid samplerates and bitrates. */
static long samplerates[3] = {44100, 48000, 32000};
static int  bitrates[15]   = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};

/* This is the struct used to tell the encoder about the input PCM */

typedef struct {
    int  channels;
    long samplerate;
} wave_t;

/* This is the struct the encoder uses to hold informationn about the output MP3 */

typedef struct {
    int    mode;      /* Stereo mode */
    int    bitr;      /* Must conform to known bitrate - see Main.c */
    int    emph;      /* De-emphasis */
    int    copyright;
    int    original;
} mpeg_t;

typedef struct {
  wave_t wave;
  mpeg_t mpeg;
} config_t;

typedef struct shine_global_flags shine_t;

void L3_set_config_mpeg_defaults(mpeg_t *mpeg);
int L3_find_bitrate_index(int bitr);
int L3_find_samplerate_index(long freq);

shine_t *L3_initialise(config_t *config);

unsigned char *L3_encode_frame(shine_t *s, short data[2][samp_per_frame], long *written);

void L3_close(shine_t *s);

#endif
