/* L3mdct */

#include "types.h"
#include "l3mdct.h"

/* This is table B.9: coefficients for aliasing reduction */
#define MDCT_CA(coef)	(long)(coef / sqrt(1.0 + (coef * coef)) * 0x7fffffff)
#define MDCT_CS(coef)	(long)(1.0  / sqrt(1.0 + (coef * coef)) * 0x7fffffff)

static const long mdct_ca[8] = {
	MDCT_CA(-0.6),
	MDCT_CA(-0.535),
	MDCT_CA(-0.33),
	MDCT_CA(-0.185),
	MDCT_CA(-0.095),
	MDCT_CA(-0.041),
	MDCT_CA(-0.0142),
	MDCT_CA(-0.0037),
};
static const long mdct_cs[8] = {
	MDCT_CS(-0.6),
	MDCT_CS(-0.535),
	MDCT_CS(-0.33),
	MDCT_CS(-0.185),
	MDCT_CS(-0.095),
	MDCT_CS(-0.041),
	MDCT_CS(-0.0142),
	MDCT_CS(-0.0037),
};

/*
 * shine_mdct_initialise:
 * -------------------
 */
void shine_mdct_initialise(shine_global_config *config)
{
  int m,k;

  /* prepare the mdct coefficients */
  for(m=18; m--; )
    for(k=36; k--; )
      /* combine window and mdct coefficients into a single table */
      /* scale and convert to fixed point before storing */
      config->mdct.cos_l[m][k] = (long)(sin(PI36*(k+0.5))
                                      * cos((PI/72)*(2*k+19)*(2*m+1)) * 0x7fffffff);
}

/*
 * shine_mdct_sub:
 * ------------
 */
void shine_mdct_sub(shine_global_config *config)
{
  /* note. we wish to access the array 'config->mdct_freq[2][2][576]' as
   * [2][2][32][18]. (32*18=576),
   */
  long (*mdct_enc)[18];

  int  ch,gr,band,j,k;
  long mdct_in[36];
  long bu,bd;

  for(gr=0; gr<config->mpeg.granules_per_frame; gr++)
    for(ch=config->wave.channels; ch--; )
    {
      /* set up pointer to the part of config->mdct_freq we're using */
      mdct_enc = (long (*)[18]) config->mdct_freq[gr][ch];

      /* Compensate for inversion in the analysis filter
       * (every odd index of band AND k)
       */
      for(band=1; band<=31; band+=2 )
        for(k=1; k<=17; k+=2 )
          config->l3_sb_sample[ch][gr+1][k][band] *= -1;

      /* Perform imdct of 18 previous subband samples + 18 current subband samples */
      for(band=32; band--; )
      {
        for(k=18; k--; )
        {
          mdct_in[k]    = config->l3_sb_sample[ch][ gr ][k][band];
          mdct_in[k+18] = config->l3_sb_sample[ch][gr+1][k][band];
        }

        /* Calculation of the MDCT
         * In the case of long blocks ( block_type 0,1,3 ) there are
         * 36 coefficients in the time domain and 18 in the frequency
         * domain.
         */
        for(k=18; k--; )
        {
          long vm = 0;
          long* cos_l_ptr = config->mdct.cos_l[k] + 36;
          long* mdct_in_ptr = mdct_in + 36;
          /* loop unrolling: 9 steps each */
          while(mdct_in_ptr != mdct_in)
          {
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
            vm += mul(*--mdct_in_ptr,*--cos_l_ptr);
          }
          mdct_enc[band][k] = vm;
        }
      }

      /* Perform aliasing reduction butterfly */
      for(band=31; band--; )
        for(k=8; k--; )
        {
          /* must left justify result of multiplication here because the centre
           * two values in each block are not touched.
           */
          bu = muls(mdct_enc[band][17-k],mdct_cs[k]) + muls(mdct_enc[band+1][k],mdct_ca[k]);
          bd = muls(mdct_enc[band+1][k],mdct_cs[k]) - muls(mdct_enc[band][17-k],mdct_ca[k]);
          mdct_enc[band][17-k] = bu;
          mdct_enc[band+1][k]  = bd;
        }
    }

  /* Save latest granule's subband samples to be used in the next mdct call */
  for(ch=config->wave.channels ;ch--; )
    for(j=18; j--; )
      for(band=32; band--; )
        config->l3_sb_sample[ch][0][j][band] = config->l3_sb_sample[ch][config->mpeg.granules_per_frame][j][band];
}

