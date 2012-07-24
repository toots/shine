#ifndef WAVE_H
#define WAVE_H

#include <stdio.h>

FILE *wave_open(const char *file, shine_config_t *config, int quiet);
int  wave_get(int16_t buffer[2][samp_per_frame], FILE *file, void *config_in);
void wave_close(FILE *file);

#endif
