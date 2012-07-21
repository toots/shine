/* layer3.c */

#include "g_includes.h"
#include "layer3.h"
#include "l3subband.h"
#include "l3mdct.h"
#include "l3loop.h"
#include "bitstream.h"
#include "l3bitstream.h"

/* Set default values for important vars */
void L3_set_config_mpeg_defaults(mpeg_t *mpeg)
{
  mpeg->type = 1;
  mpeg->layr = 2;
  mpeg->mode = 2;
  mpeg->bitr = 128;
  mpeg->psyc = 2;
  mpeg->emph = 0;
  mpeg->crc  = 0;
  mpeg->ext  = 0;
  mpeg->mode_ext  = 0;
  mpeg->copyright = 0;
  mpeg->original  = 1;

  mpeg->bits_per_slot     = 8;
  mpeg->samples_per_frame = samp_per_frame;
}

/* Compute default encoding values. */
void L3_initialise(config_t *config)
{
  double avg_slots_per_frame;

  L3_subband_initialise();
  L3_mdct_initialise();
  L3_loop_initialise();

  /* Figure average number of 'slots' per frame. */
  avg_slots_per_frame = ((double)config->mpeg.samples_per_frame /
                        ((double)config->wave.samplerate/1000)) *
                        ((double)config->mpeg.bitr /
                         (double)config->mpeg.bits_per_slot);

  config->mpeg.whole_slots_per_frame  = (int)avg_slots_per_frame;

  config->mpeg.frac_slots_per_frame  = avg_slots_per_frame - (double)config->mpeg.whole_slots_per_frame;
  config->mpeg.slot_lag              = -config->mpeg.frac_slots_per_frame;

  if(config->mpeg.frac_slots_per_frame==0)
    config->mpeg.padding = 0;
}

int L3_find_samplerate_index(long freq)
{
  static long mpeg1[3] = {44100, 48000, 32000};
  int i;

  for(i=0;i<3;i++)
    if(freq==mpeg1[i]) return i;

  return -1; /* error - not a valid samplerate for encoder */
}

int L3_find_bitrate_index(int bitr)
{
  static long mpeg1[15] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
  int i;

  for(i=0;i<15;i++)
    if(bitr==mpeg1[i]) return i;

  return -1; /* error - not a valid samplerate for encoder */
}

void L3_compress(callback_t *callback)
{
  int             channel;
  int             i;
  int             gr;
  double          pe[2][2];
  short          *buffer_window[2];
  int             mean_bits;
  int             sideinfo_len;
  static short    buffer[2][samp_per_frame];
  static int      l3_enc[2][2][samp_per_frame2];
  static long     l3_sb_sample[2][3][18][SBLIMIT];
  static long     mdct_freq[2][2][samp_per_frame2];
  static L3_psy_ratio_t  ratio;
  static L3_side_info_t  side_info;
  static L3_scalefac_t   scalefactor;
  static bitstream_t     bs;

  open_bit_stream(&bs, BUFFER_SIZE);

  memset((char *)&side_info,0,sizeof(L3_side_info_t));

  sideinfo_len = (callback->config.wave.channels==1) ? 168 : 288;

  L3_initialise(&callback->config);

  while(callback->get_pcm(buffer, callback))
  {
    buffer_window[0] = buffer[0];
    buffer_window[1] = buffer[1];

    if(callback->config.mpeg.frac_slots_per_frame)
    {
      if(callback->config.mpeg.slot_lag>(callback->config.mpeg.frac_slots_per_frame-1.0))
      { /* No padding for this frame */
        callback->config.mpeg.slot_lag    -= callback->config.mpeg.frac_slots_per_frame;
        callback->config.mpeg.padding = 0;
      }
      else
      { /* Padding for this frame  */
        callback->config.mpeg.slot_lag    += (1-callback->config.mpeg.frac_slots_per_frame);
        callback->config.mpeg.padding = 1;
      }
    }

    callback->config.mpeg.bits_per_frame = 8*(callback->config.mpeg.whole_slots_per_frame + callback->config.mpeg.padding);
    mean_bits = (callback->config.mpeg.bits_per_frame - sideinfo_len)>>1;

    /* polyphase filtering */
    for(gr=0;gr<2;gr++)
      for(channel=callback->config.wave.channels; channel--; )
        for(i=0;i<18;i++)
          L3_window_filter_subband(&buffer_window[channel], &l3_sb_sample[channel][gr+1][i][0] ,channel);

    /* apply mdct to the polyphase output */
    L3_mdct_sub(l3_sb_sample, mdct_freq, &callback->config);

    /* bit and noise allocation */
    L3_iteration_loop(pe,mdct_freq,&ratio,&side_info, l3_enc, mean_bits,&scalefactor, &callback->config);

    /* write the frame to the bitstream */
    L3_format_bitstream(l3_enc,&side_info,&scalefactor, &bs,mdct_freq,NULL,0, &callback->config);

    /* Write data to disk. */
    if (bs.data_position)
      callback->write_mp3(sizeof(unsigned char)*bs.data_position, bs.data, &callback->config);
    
    bs.data_position = 0;
  }
  close_bit_stream(&bs);
}

