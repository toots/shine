#ifndef L3LOOP_H
#define L3LOOP_H

#define e        2.71828182845
#define CBLIMIT  21
#define SFB_LMAX 22

void L3_loop_initialise(void);

void L3_iteration_loop(double          pe[][2], 
                       long            mdct_freq_org[2][2][samp_per_frame2], 
                       L3_psy_ratio_t *ratio,
                       L3_side_info_t *side_info, 
                       int             l3_enc[2][2][samp_per_frame2],
                       int             mean_bits, 
                       L3_scalefac_t  *scalefacitor , config_t *config);

#endif

