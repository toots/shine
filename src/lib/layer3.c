/* layer3.c */

#include "types.h"
#include "layer3.h"
#include "l3subband.h"
#include "l3mdct.h"
#include "l3loop.h"
#include "bitstream.h"
#include "formatbits.h"
#include "l3bitstream.h"

/* Set default values for important vars */
void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg)
{
  mpeg->bitr = 128;
  mpeg->emph = NONE;
  mpeg->copyright = 0;
  mpeg->original  = 1;
}

/* Compute default encoding values. */
shine_global_config *shine_initialise(shine_config_t *pub_config)
{
  double avg_slots_per_frame;
  shine_global_config *config;

  config = calloc(1,sizeof(shine_global_config));
  if (config == NULL)
    return config;

  shine_subband_initialise(config);
  shine_mdct_initialise(config);
  shine_loop_initialise(config);
  shine_formatbits_initialise(config);
  shine_bitstream_initialise(config );

  /* Copy public config. */
  config->wave.channels   = pub_config->wave.channels;
  config->wave.samplerate = pub_config->wave.samplerate; 
  config->mpeg.mode       = pub_config->mpeg.mode;     
  config->mpeg.bitr       = pub_config->mpeg.bitr; 
  config->mpeg.emph       = pub_config->mpeg.emph; 
  config->mpeg.copyright  = pub_config->mpeg.copyright;  
  config->mpeg.original   = pub_config->mpeg.original; 

  /* Set default values. */
  config->ResvMax        = 0;
  config->ResvSize       = 0;
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

  config->mpeg.samplerate_index = shine_find_samplerate_index(config->wave.samplerate);
  config->mpeg.bitrate_index    = shine_find_bitrate_index(config->mpeg.bitr)+1;

  shine_open_bit_stream(&config->bs, BUFFER_SIZE);

  memset((char *)&config->side_info,0,sizeof(shine_side_info_t));

  config->sideinfo_len = (config->wave.channels==1) ? 168 : 288;

  return config;
}

int shine_find_samplerate_index(long freq)
{
  int i;

  for(i=0;i<6;i++)
    if(freq==samplerates[i]) return i;

  return -1; /* error - not a valid samplerate for encoder */
}

int shine_find_bitrate_index(int bitr)
{
  int i;

  for(i=0;i<14;i++)
    if(bitr==bitrates[i]) return i;

  return -1; /* error - not a valid samplerate for encoder */
}

unsigned char *shine_encode_frame(shine_global_config *config, int16_t data[2][samp_per_frame], long *written)
{
  int i, gr, channel;

  config->buffer[0] = data[0];
  if (config->wave.channels == 2)
    config->buffer[1] = data[1];

  if(config->mpeg.frac_slots_per_frame)
  {
    if(config->mpeg.slot_lag>(config->mpeg.frac_slots_per_frame-1.0))
    { /* No padding for this frame */
      config->mpeg.slot_lag    -= config->mpeg.frac_slots_per_frame;
      config->mpeg.padding = 0;
    }
    else
    { /* Padding for this frame  */
      config->mpeg.slot_lag    += (1-config->mpeg.frac_slots_per_frame);
      config->mpeg.padding = 1;
    }
  }

  config->mpeg.bits_per_frame = 8*(config->mpeg.whole_slots_per_frame + config->mpeg.padding);
  config->mean_bits = (config->mpeg.bits_per_frame - config->sideinfo_len)>>1;

  /* polyphase filtering */
  for(gr=0;gr<2;gr++)
    for(channel=config->wave.channels; channel--; )
      for(i=0;i<18;i++)
        shine_window_filter_subband(&config->buffer[channel], &config->l3_sb_sample[channel][gr+1][i][0] ,channel,config);

  /* apply mdct to the polyphase output */
  shine_mdct_sub(config);

  /* bit and noise allocation */
  shine_iteration_loop(config);

  /* write the frame to the bitstream */
  shine_format_bitstream(config);

  /* Return data. */
  *written = config->bs.data_position;
  config->bs.data_position = 0;

  return config->bs.data;
}

unsigned char *shine_flush(shine_global_config *config, long *written) {
  shine_empty_buffer(&config->bs, MINIMUM);
  *written = config->bs.data_position;
  config->bs.data_position = 0;

  return config->bs.data;
}


void shine_close(shine_global_config *config) {
  shine_bitstream_close(config);
  shine_formatbits_close(config);
  shine_close_bit_stream(&config->bs);
  free(config);
}
