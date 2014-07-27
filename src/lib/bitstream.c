/*
 *  bit_stream.c package
 *  Author:  Jean-Georges Fritsch, C-Cube Microsystems
 *
 * This package provides functions to write information to the bit stream.
 *
 * Removed unused functions. Feb 2001 P.Everett
 */

#include "types.h"
#include "bitstream.h"

#if !defined(__APPLE__)
#include <malloc.h>
#endif

/* open the device to write the bit stream into it */
void shine_open_bit_stream(bitstream_t *bs, int size)
{
  bs->data = (unsigned char *)malloc(size*sizeof(unsigned char));
  bs->data_size = size;
  bs->data_position = 0;
  bs->cache = 0;
  bs->cache_bits = 32;
}

/*close the device containing the bit stream */
void shine_close_bit_stream(bitstream_t *bs)
{
  if (bs->data)
    free(bs->data);
}

/*
 * shine_putbits:
 * --------
 * write N bits into the bit stream.
 * bs = bit stream structure
 * val = value to write into the buffer
 * N = number of bits of val
 */
void shine_putbits(bitstream_t *bs, unsigned int val, unsigned int N)
{
#ifdef DEBUG
	if (N > 32)
		printf("Cannot read or write more than %d bits at a time.\n", 32);
#endif
	if (N < 32)
		val &= ((1UL << N) - 1);

	if (bs->cache_bits >= N) {
		bs->cache_bits -= N;
		bs->cache |= val << bs->cache_bits;
	} else {
		if (bs->data_position + sizeof(unsigned int) >= bs->data_size) {
			bs->data = (unsigned char *)realloc(bs->data, bs->data_size + (bs->data_size / 2));
			bs->data_size += (bs->data_size / 2);
		}

		N -= bs->cache_bits;
		bs->cache |= val >> N;
#ifdef SHINE_BIG_ENDIAN
		*(unsigned int*)(bs->data + bs->data_position) = bs->cache;
#else
		*(unsigned int*)(bs->data + bs->data_position) = SWAB32(bs->cache);
#endif
		bs->data_position += sizeof(unsigned int);
		bs->cache_bits = 32 - N;
		bs->cache = val << bs->cache_bits;
	}
}
