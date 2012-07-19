#ifndef WAVE_H
#define WAVE_H

void wave_open(config_t *config);
int  wave_get(short buffer[2][samp_per_frame], void *config_in);
void wave_close(config_t *config);

#endif
