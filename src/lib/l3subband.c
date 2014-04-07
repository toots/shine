/* L3SubBand */

#include "types.h"
#include "tables.h"
#include "l3subband.h"

/*
 * shine_subband_initialise:
 * ----------------------
 * Calculates the analysis filterbank coefficients and rounds to the
 * 9th decimal place accuracy of the filterbank tables in the ISO
 * document.  The coefficients are stored in #filter#
 */
void shine_subband_initialise(shine_global_config *config)
{
  int i,j;
  double filter;

  for(i=MAX_CHANNELS; i-- ; ) {
    config->subband.off[i] = 0;
    for(j=HAN_SIZE; j--; )
      config->subband.x[i][j] = 0;
  }

  for (i=SBLIMIT; i--; )
    for (j=64; j--; )
    {
      if ((filter = 1e9*cos((double)((2*i+1)*(16-j)*PI64))) >= 0)
        modf(filter+0.5, &filter);
      else
        modf(filter-0.5, &filter);
      /* scale and convert to fixed point before storing */
      config->subband.fl[i][j] = (long)(filter * (0x7fffffff * 1e-9));
    }

  /* note. 0.035781 is shine_enwindow maximum value */
  /* scale and convert to fixed point before storing */
  for (i=HAN_SIZE; i--;)
    config->subband.ew[i] = (long)(shine_enwindow[i] * 0x7fffffff);
}

/*
 * shine_window_filter_subband:
 * -------------------------
 * Overlapping window on PCM samples
 * 32 16-bit pcm samples are scaled to fractional 2's complement and
 * concatenated to the end of the window buffer #x#. The updated window
 * buffer #x# is then windowed by the analysis window #shine_enwindow# to produce
 * the windowed sample #z#
 * Calculates the analysis filter bank coefficients
 * The windowed samples #z# is filtered by the digital filter matrix #filter#
 * to produce the subband samples #s#. This done by first selectively
 * picking out values from the windowed samples, and then multiplying
 * them by the filter matrix, producing 32 subband samples.
 */
void shine_window_filter_subband(int16_t **buffer, long s[SBLIMIT] , int ch, shine_global_config *config)
{
  long y[64];
  int i,j;

  /* replace 32 oldest samples with 32 new samples */
  for (i=31;i>=0;i--)
    config->subband.x[ch][i+config->subband.off[ch]] = ((long)*(*buffer)++) << 16;

  /* shift samples into proper window positions */
  long* z_into  = config->subband.z[ch];
  long* z_from1 = config->subband.x[ch];
  long* z_from2 = config->subband.ew;
  long  offset  = config->subband.off[ch];

  /* loop unrolling: 8 steps each, so need HAN_SIZE % 8 == 0 */
  for (i=HAN_SIZE; i; )
  {
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
    i--;
    z_into[i] = mul(z_from1[(i+offset)&(HAN_SIZE-1)],z_from2[i]);
  }

  config->subband.off[ch] = (config->subband.off[ch] + 480) & (HAN_SIZE-1); /* offset is modulo (HAN_SIZE)*/

  memset(y,0,64*sizeof(long));
  for (j=0; j < 512; j += 64)
  {
    long* z_ptr = config->subband.z[ch] + j + 64;
    long* y_ptr = y + 64;
    /* loop unrolling: 8 steps each for a total of 64*/
    while (y_ptr != y)
    {
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
      *(--y_ptr) += *(--z_ptr);
    }
  }

  for (i=SBLIMIT; i--; )
  {
    long s_value = 0;
    long* y_ptr = y + 64;
    long* cur_fl = config->subband.fl[i] + 64;
    /* loop unrolling: 8 steps each for a total of 64*/
    while (y_ptr != y)
    {
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
      s_value += mul(*(--cur_fl),*(--y_ptr));
    }
    s[i] = s_value;
  }
}

