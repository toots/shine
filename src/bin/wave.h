#ifndef WAVE_H
#define WAVE_H

#include <stdio.h>
#include <stdbool.h>

typedef struct {
        FILE *file;
        long length;
} wave_t;

bool wave_open(const char *fname, wave_t *wave, shine_config_t *config, int quiet, long *data_length);
int  wave_get(int16_t buffer[2][samp_per_frame], wave_t *wave, void *config_in);
void wave_close(wave_t *wave);

#endif
