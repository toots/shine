/* L3mdct */

#define __INLINE_ASM
#include "g_includes.h"
#include "Layer3.h"
#include "L3mdct.h"

/*extern long mul(long x, long y); */ /* inlined in header file */
/*extern long muls(long x, long y); */ /* inlined in header file */
 
/* This is table B.9: coefficients for aliasing reduction */
static double c[8] = { -0.6,-0.535,-0.33,-0.185,-0.095,-0.041,-0.0142, -0.0037 };

static long ca[8];
static long cs[8];
static long cos_l[18][36];

/*
 * L3_mdct_initialise:
 * -------------------
 */
void L3_mdct_initialise(void)
{
  int i,m,k;
  double sq;

  /* prepare the aliasing reduction butterflies */
  for(i=8; i--; )
  {
    sq = sqrt(1.0 + (c[i] * c[i]));
    /* scale and convert to fixed point before storing */
    ca[i] = (long)(c[i] / sq * 0x7fffffff);
    cs[i] = (long)(1.0  / sq * 0x7fffffff);
  }

  /* prepare the mdct coefficients */
  for(m=18; m--; )
    for(k=36; k--; )
      /* combine window and mdct coefficients into a single table */
      /* scale and convert to fixed point before storing */
      cos_l[m][k] = (long)(sin(PI36*(k+0.5))
                         * cos((PI/72)*(2*k+19)*(2*m+1)) * 0x7fffffff);
}

/*
 * L3_mdct_sub:
 * ------------
 */
void L3_mdct_sub(long sb_sample[2][3][18][SBLIMIT], 
                 long mdct_freq[2][2][samp_per_frame2], config_t *config)
{
  /* note. we wish to access the array 'mdct_freq[2][2][576]' as
   * [2][2][32][18]. (32*18=576),
   */
  long (*mdct_enc)[18];
  
  int  ch,gr,band,j,k;
  long mdct_in[36];
  long bu,bd,*m;
    
  for(gr=0; gr<2; gr++)
    for(ch=config->wave.channels; ch--; )
    {
      /* set up pointer to the part of mdct_freq we're using */
      mdct_enc = (long (*)[18]) mdct_freq[gr][ch];
      
      /* Compensate for inversion in the analysis filter
       * (every odd index of band AND k)
       */
      for(band=1; band<=31; band+=2 )
        for(k=1; k<=17; k+=2 )
          sb_sample[ch][gr+1][k][band] *= -1;
            
      /* Perform imdct of 18 previous subband samples + 18 current subband samples */
      for(band=32; band--; )
      {
        for(k=18; k--; )
        {
          mdct_in[k]    = sb_sample[ch][ gr ][k][band];
          mdct_in[k+18] = sb_sample[ch][gr+1][k][band];
        }
                
        /* Calculation of the MDCT
         * In the case of long blocks ( block_type 0,1,3 ) there are
         * 36 coefficients in the time domain and 18 in the frequency
         * domain.
         */
        for(k=18; k--; )
        {
          m = &mdct_enc[band][k];
          for(j=36, *m=0; j--; )
            *m += mul(mdct_in[j],cos_l[k][j]);
        }
      }

      /* Perform aliasing reduction butterfly */
      for(band=31; band--; )
        for(k=8; k--; )
        {
          /* must left justify result of multiplication here because the centre
           * two values in each block are not touched.
           */
          bu = muls(mdct_enc[band][17-k],cs[k]) + muls(mdct_enc[band+1][k],ca[k]);
          bd = muls(mdct_enc[band+1][k],cs[k]) - muls(mdct_enc[band][17-k],ca[k]);
          mdct_enc[band][17-k] = bu;
          mdct_enc[band+1][k]  = bd;
        }
    }
    
  /* Save latest granule's subband samples to be used in the next mdct call */
  for(ch=config->wave.channels ;ch--; )
    for(j=18; j--; )
      for(band=32; band--; )
        sb_sample[ch][0][j][band] = sb_sample[ch][2][j][band];
}

 

