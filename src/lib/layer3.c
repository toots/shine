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
  mpeg->bitr = 128;
  mpeg->emph = 0;
  mpeg->copyright = 0;
  mpeg->original  = 1;
}

/* Compute default encoding values. */
void L3_initialise(config_t *pub_config, priv_config_t *config)
{
  double avg_slots_per_frame;

  L3_subband_initialise();
  L3_mdct_initialise();
  L3_loop_initialise();

  /* Copy public config. */
  memcpy(&config->wave, &pub_config->wave, sizeof(pub_config->wave));
  config->mpeg.mode      = pub_config->mpeg.mode;     
  config->mpeg.bitr      = pub_config->mpeg.bitr; 
  config->mpeg.emph      = pub_config->mpeg.emph; 
  config->mpeg.copyright = pub_config->mpeg.copyright;  
  config->mpeg.original  = pub_config->mpeg.original; 

  /* Set default values. */
  config->mpeg.crc       = 0;
  config->mpeg.ext       = 0;
  config->mpeg.mode_ext  = 0;

  config->mpeg.bits_per_slot     = 8;

  /* Figure average number of 'slots' per frame. */
  avg_slots_per_frame = ((double)samp_per_frame /
                        ((double)config->wave.samplerate/1000)) *
                        ((double)config->mpeg.bitr /
                         (double)config->mpeg.bits_per_slot);

  config->mpeg.whole_slots_per_frame  = (int)avg_slots_per_frame;

  config->mpeg.frac_slots_per_frame  = avg_slots_per_frame - (double)config->mpeg.whole_slots_per_frame;
  config->mpeg.slot_lag              = -config->mpeg.frac_slots_per_frame;

  if(config->mpeg.frac_slots_per_frame==0)
    config->mpeg.padding = 0;

  config->mpeg.samplerate_index = L3_find_samplerate_index(config->wave.samplerate);
  config->mpeg.bitrate_index    = L3_find_bitrate_index(config->mpeg.bitr);
}

int L3_find_samplerate_index(long freq)
{
  int i;

  for(i=0;i<3;i++)
    if(freq==samplerates[i]) return i;

  return -1; /* error - not a valid samplerate for encoder */
}

int L3_find_bitrate_index(int bitr)
{
  int i;

  for(i=0;i<15;i++)
    if(bitr==bitrates[i]) return i;

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
  static priv_config_t   config;

  open_bit_stream(&bs, BUFFER_SIZE);

  memset((char *)&side_info,0,sizeof(L3_side_info_t));

  sideinfo_len = (config.wave.channels==1) ? 168 : 288;

  L3_initialise(&callback->config, &config);

  while(callback->get_pcm(buffer, callback))
  {
    buffer_window[0] = buffer[0];
    buffer_window[1] = buffer[1];

    if(config.mpeg.frac_slots_per_frame)
    {
      if(config.mpeg.slot_lag>(config.mpeg.frac_slots_per_frame-1.0))
      { /* No padding for this frame */
        config.mpeg.slot_lag    -= config.mpeg.frac_slots_per_frame;
        config.mpeg.padding = 0;
      }
      else
      { /* Padding for this frame  */
        config.mpeg.slot_lag    += (1-config.mpeg.frac_slots_per_frame);
        config.mpeg.padding = 1;
      }
    }

    config.mpeg.bits_per_frame = 8*(config.mpeg.whole_slots_per_frame + config.mpeg.padding);
    mean_bits = (config.mpeg.bits_per_frame - sideinfo_len)>>1;

    /* polyphase filtering */
    for(gr=0;gr<2;gr++)
      for(channel=config.wave.channels; channel--; )
        for(i=0;i<18;i++)
          L3_window_filter_subband(&buffer_window[channel], &l3_sb_sample[channel][gr+1][i][0] ,channel);

    /* apply mdct to the polyphase output */
    L3_mdct_sub(l3_sb_sample, mdct_freq, &config);

    /* bit and noise allocation */
    L3_iteration_loop(pe,mdct_freq,&ratio,&side_info, l3_enc, mean_bits,&scalefactor, &config);

    /* write the frame to the bitstream */
    L3_format_bitstream(l3_enc,&side_info,&scalefactor, &bs,mdct_freq,NULL,0, &config);

    /* Write data to disk. */
    if (bs.data_position)
      callback->write_mp3(sizeof(unsigned char)*bs.data_position, bs.data, &config);
    
    bs.data_position = 0;
  }
  close_bit_stream(&bs);
}

