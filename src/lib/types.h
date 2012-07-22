#ifndef TYPES_H
#define TYPES_H

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

/* Abstract encoder type. */
typedef struct encoder_t encoder;

// TODO: remove!
typedef struct {
  config_t config;

  /* These two app-supplied routines are used to read and write data */
  int  (*get_pcm)(short buffer[2][samp_per_frame], void *config_in);
  int  (*write_mp3)(long bytes, void *buffer, void *config_in);

  void *user; /* For the calling app's convenience */
} callback_t;

#endif
