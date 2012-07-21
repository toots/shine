#ifndef TYPES_H
#define TYPES_H

#define WAVE_RIFF_PCM 0
#define WAVE_PCM_LOHI 1
#define WAVE_PCM_HILO 2
#define WAVE_PCM_AIFF 3

#define MODE_MONO     3

#define samp_per_frame  1152

/* In the wave_t struct below, values marked with a '+' are necessary.
   Everything else is optional and for the calling apps read
   and write vectors convenience */

/* This is the struct used to tell the encoder about the input PCM */

typedef struct {
    int  type;
    int  channels;      /* + */
    int  bits;
    long samplerate;    /* + */
    long length;
} wave_t;

/* In the mpeg_t struct below, values marked with a '+'
   actually control something the calling app might want
   to change.  The rest are internal values.  See set_defaults()
   in shineenc Main.c for example init.
*/

/* This is the struct the encoder uses to hold informationn about the output MP3 */

typedef struct {
    int    type;
    int    layr;
    int    mode;      /* + */ /* Stereo mode */
    int    bitr;      /* + */ /* Must conform to known bitrate - see Main.c */
    int    psyc;      /* + */ /* Which psy model - see Main.c */
    int    emph;      /* + */ /* De-emphasis */
    int    padding;
    long   samples_per_frame;
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
} mpeg_t;

typedef struct {
  wave_t wave;
  mpeg_t mpeg;
} config_t;

// TODO: remove!
typedef struct {
  config_t config;

  /* These two app-supplied routines are used to read and write data */
  int  (*get_pcm)(short buffer[2][samp_per_frame], void *config_in);
  int  (*write_mp3)(long bytes, void *buffer, void *config_in);

  void *user; /* For the calling app's convenience */
} callback_t;

#endif
