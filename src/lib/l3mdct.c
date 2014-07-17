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
          int32_t vm;
          mul0(vm, mdct_in[35], config->mdct.cos_l[k][35]);
          for(j=35; j; j-=7) {
            muladd(vm, mdct_in[j-1], config->mdct.cos_l[k][j-1]);
            muladd(vm, mdct_in[j-2], config->mdct.cos_l[k][j-2]);
            muladd(vm, mdct_in[j-3], config->mdct.cos_l[k][j-3]);
            muladd(vm, mdct_in[j-4], config->mdct.cos_l[k][j-4]);
            muladd(vm, mdct_in[j-5], config->mdct.cos_l[k][j-5]);
            muladd(vm, mdct_in[j-6], config->mdct.cos_l[k][j-6]);
            muladd(vm, mdct_in[j-7], config->mdct.cos_l[k][j-7]);
          }
          mulz(vm);
          mdct_enc[band][k] = vm;
        }
      }

      /* Perform aliasing reduction butterfly */
      for(band=31; band--; )
        for(k=8; k--; )
        {
          cmuls(mdct_enc[band+1][k], mdct_enc[band][17-k], mdct_enc[band+1][k], mdct_enc[band][17-k], mdct_cs[k], mdct_ca[k]);
        }
    }

  /* Save latest granule's subband samples to be used in the next mdct call */
  for(ch=config->wave.channels ;ch--; )
    memcpy(config->l3_sb_sample[ch][0], config->l3_sb_sample[ch][config->mpeg.granules_per_frame], sizeof(config->l3_sb_sample[0][0]));
}

