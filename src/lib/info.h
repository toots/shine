#ifndef INFO_H
#define INFO_H

#include <stdint.h>

/* Updates a CRC-16 (polynomial 0x8005, reflected) with `size` bytes. */
uint16_t shine_info_crc(uint16_t crc, const unsigned char *data, long size);

#endif
