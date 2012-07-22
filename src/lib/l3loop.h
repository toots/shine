#ifndef L3LOOP_H
#define L3LOOP_H

#define e        2.71828182845
#define CBLIMIT  21
#define SFB_LMAX 22

void L3_loop_initialise(void);

void L3_iteration_loop(encoder_t *config);

#endif

