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
  for (i=HAN_SIZE; i--; )
    config->subband.z[ch][i] = mul(config->subband.x[ch][(i+config->subband.off[ch])&(HAN_SIZE-1)],config->subband.ew[i]);

  config->subband.off[ch] = (config->subband.off[ch] + 480) & (HAN_SIZE-1); /* offset is modulo (HAN_SIZE)*/

  for (i=64; i--; )
    for (j=8, y[i] = 0; j--; )
      y[i] += config->subband.z[ch][i+(j<<6)];

  for (i=SBLIMIT; i--; )
    for (j=64, s[i]= 0; j--; )
      s[i] += mul(config->subband.fl[i][j],y[j]);
}

