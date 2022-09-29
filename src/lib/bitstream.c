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

#if !defined(__APPLE__) && !defined(__FreeBSD__)
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
		printf("Cannot write more than 32 bits at a time.\n");
	if (N < 32 && (val >> N) != 0)
		printf("Upper bits (higher than %d) are not all zeros.\n", N);
#endif

	if (bs->cache_bits > N) {
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
		if (N != 0)
			bs->cache = val << bs->cache_bits;
		else
			bs->cache = 0;
	}
}

/*
 * shine_flush_end_of_frame:
 * -------------------------
 * Flush remaining bits of the frame to the bitstream. Must be called only at end of
 * frame, because only then is the cache guaranteed to be filled to a byte boundary.
 */
void shine_flush_end_of_frame(bitstream_t *bs)
{
	int bits_to_flush;
	int cache_bytes;

	if (bs->cache_bits < 32) {
		bits_to_flush = 32 - bs->cache_bits;

		/* fill the remaining cache bits with zeros (they will be ignored). */
		shine_putbits(bs, 0, bs->cache_bits);

		/* correct output position because a full 4 bytes may not have been written */
		cache_bytes = bits_to_flush / 8;
		bs->data_position += cache_bytes - 4;

		/* clear the cache for the next frame */
		bs->cache_bits = 32;
		bs->cache = 0;
	}
}

int shine_get_bits_count(bitstream_t *bs)
{
	return bs->data_position * 8 + 32 - bs->cache_bits;
}

