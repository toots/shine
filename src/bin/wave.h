#ifndef WAVE_H
#define WAVE_H

#include <stdio.h>

FILE *wave_open(const char *file, config_t *config, int quiet);
int  wave_get(short buffer[2][samp_per_frame], void *config_in);
void wave_close(FILE *file);

#endif
