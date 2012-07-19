/* L3SubBand */
 
#define __INLINE_ASM
#include "g_includes.h"
#include "tables.h" 
#include "L3subband.h"

static int off[2] = {0,0};
static long fl[SBLIMIT][64];
static long x[2][HAN_SIZE];
static long z[2][HAN_SIZE];
static long ew[HAN_SIZE];

/*extern long mul(long x, long y); */ /* inlined in header file */

/*
 * L3_subband_initialise:
 * ----------------------
 * Calculates the analysis filterbank coefficients and rounds to the
 * 9th decimal place accuracy of the filterbank tables in the ISO
 * document.  The coefficients are stored in #filter#
 */
void L3_subband_initialise()
{
  int i,j;
  double filter;

  for(i=2; i-- ; ) 
    for(j=HAN_SIZE; j--; )
      x[i][j] = 0;

  for (i=SBLIMIT; i--; )
    for (j=64; j--; ) 
    {
      if ((filter = 1e9*cos((double)((2*i+1)*(16-j)*PI64))) >= 0)
        modf(filter+0.5, &filter);
      else
        modf(filter-0.5, &filter);
      /* scale and convert to fixed point before storing */
      fl[i][j] = (long)(filter * (0x7fffffff * 1e-9));
    }
    
  /* note. 0.035781 is enwindow maximum value */
  /* scale and convert to fixed point before storing */
  for (i=HAN_SIZE; i--;)
    ew[i] = (long)(enwindow[i] * 0x7fffffff);
}

/*
 * L3_window_filter_subband:
 * -------------------------
 * Overlapping window on PCM samples
 * 32 16-bit pcm samples are scaled to fractional 2's complement and
 * concatenated to the end of the window buffer #x#. The updated window
 * buffer #x# is then windowed by the analysis window #enwindow# to produce
 * the windowed sample #z#
 * Calculates the analysis filter bank coefficients
 * The windowed samples #z# is filtered by the digital filter matrix #filter#
 * to produce the subband samples #s#. This done by first selectively
 * picking out values from the windowed samples, and then multiplying
 * them by the filter matrix, producing 32 subband samples.
 */
void L3_window_filter_subband(short **buffer, long s[SBLIMIT] , int k)
{
  long y[64];    
  int i,j;

  /* replace 32 oldest samples with 32 new samples */
  for (i=31;i>=0;i--)
    x[k][i+off[k]] = ((long)*(*buffer)++) << 16;

  /* shift samples into proper window positions */
  for (i=HAN_SIZE; i--; )
    z[k][i] = mul(x[k][(i+off[k])&(HAN_SIZE-1)],ew[i]);

  off[k] = (off[k] + 480) & (HAN_SIZE-1); /* offset is modulo (HAN_SIZE)*/

  for (i=64; i--; ) 
    for (j=8, y[i] = 0; j--; )
      y[i] += z[k][i+(j<<6)];

  for (i=SBLIMIT; i--; )
    for (j=64, s[i]= 0; j--; )
      s[i] += mul(fl[i][j],y[j]);
}
 
