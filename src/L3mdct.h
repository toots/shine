#ifndef L3_MDCT_H
#define L3_MDCT_H

void L3_mdct_initialise();
void L3_mdct_sub(long sb_sample[2][3][18][SBLIMIT], 
                 long mdct_freq[2][2][samp_per_frame2], config_t *config);

#endif
