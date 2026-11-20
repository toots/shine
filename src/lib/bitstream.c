/*
 *  bit_stream.c package
 *  Author:  Jean-Georges Fritsch, C-Cube Microsystems
 *
 * This package provides functions to write information to the bit stream.
 *
 * Removed unused functions. Feb 2001 P.Everett
 */

#include "bitstream.h"
#include "types.h"

/* open the device to write the bit stream into it */
int shine_open_bit_stream(bitstream_t *bs, int size) {
  bs->data = (unsigned char *)malloc(size * sizeof(unsigned char));
  if (!bs->data)
    return -1;
  bs->data_size = size;
  bs->data_position = 0;
  bs->cache = 0;
  bs->cache_bits = 32;
  return 0;
}

/* Makes room for one more word. Unreachable in practice: a call to the
 * encoder writes at most two frames, far below BUFFER_SIZE. On failure,
 * bits are dropped rather than written out of bounds. */
static int ensure_word_space(bitstream_t *bs) {
  unsigned char *data;

  if (bs->data_position + (int)sizeof(unsigned int) < bs->data_size)
    return 0;

  data = realloc(bs->data, bs->data_size + bs->data_size / 2);
  if (!data)
    return -1;
  bs->data = data;
  bs->data_size += bs->data_size / 2;
  return 0;
}

/*close the device containing the bit stream */
void shine_close_bit_stream(bitstream_t *bs) {
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
void shine_putbits(bitstream_t *bs, unsigned int val, unsigned int N) {

  if (N == 0)
    return;

  if (bs->cache_bits > N) {
    bs->cache_bits -= N;
    bs->cache |= val << bs->cache_bits;
  } else {
    if (ensure_word_space(bs) < 0)
      return;

    N -= bs->cache_bits;
    bs->cache |= val >> N;
    {
      /* Most significant byte first, on any host. Locals, since stores through
       * unsigned char may alias bs: compilers merge these into one store. */
      unsigned char *out = bs->data + bs->data_position;
      unsigned int word = bs->cache;

      out[0] = (unsigned char)(word >> 24);
      out[1] = (unsigned char)(word >> 16);
      out[2] = (unsigned char)(word >> 8);
      out[3] = (unsigned char)word;
    }
    bs->data_position += sizeof(unsigned int);
    bs->cache_bits = 32 - N;
    if (N != 0)
      bs->cache = val << bs->cache_bits;
    else
      bs->cache = 0;
  }
}

/* Write out the whole bytes left in the cache. */
void shine_flush_bit_stream(bitstream_t *bs) {
  if (ensure_word_space(bs) < 0)
    return;

  for (; bs->cache_bits <= 24; bs->cache_bits += 8) {
    bs->data[bs->data_position++] = bs->cache >> 24;
    bs->cache <<= 8;
  }
}

int shine_get_bits_count(bitstream_t *bs) {
  return bs->data_position * 8 + 32 - bs->cache_bits;
}
