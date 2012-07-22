#ifndef LAYER3_H
#define LAYER3_H

void L3_set_config_mpeg_defaults(mpeg_t *mpeg);
int L3_find_bitrate_index(int bitr);
int L3_find_samplerate_index(long freq);

void L3_compress(callback_t *callback);

#endif
