#ifndef WAVE_H
#define WAVE_H

#include <stdio.h>
#include <stdbool.h>

typedef struct {
        FILE *file;
        long length;
	long duration;
} wave_t;

bool wave_open(const char *fname, wave_t *wave, shine_config_t *config, int quiet);
int  wave_get(int16_t **buffer, wave_t *wave, void *config_in, int samp_per_frame);
void wave_close(wave_t *wave);

#endif
