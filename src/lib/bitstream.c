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

/*
 * shine_empty_buffer
 * ------------
 * empty the buffer to the output device when the buffer becomes full
 */
void shine_empty_buffer(bitstream_t *bs, int minimum)
{
  int total = bs->buf_size-minimum;
  int i;

  if (bs->data_size-bs->data_position < total) {
    bs->data = realloc(bs->data, sizeof(unsigned char)*(total+bs->data_position));
    bs->data_size = total+bs->data_position; 
  }
 
  for (i=minimum;i<bs->buf_size;i++)
    bs->data[bs->data_position+bs->buf_size-1-i] = bs->buf[i];

  bs->data_position += total; 

  for (i=0;i<minimum; i++)
    bs->buf[bs->buf_size - minimum + i] = bs->buf[i];

  bs->buf_byte_idx = bs->buf_size -1 - minimum;
  bs->buf_bit_idx = 8;
}

/* open the device to write the bit stream into it */
void shine_open_bit_stream(bitstream_t *bs, int size)
{
  bs->data = NULL;
  bs->data_size = 0;
  bs->data_position = 0;
  bs->buf = (unsigned char *)malloc(size*sizeof(unsigned char));
  bs->buf_size = size;
  bs->buf_byte_idx = size-1;
  bs->buf_bit_idx=8;
  bs->totbit=0;
  bs->mode = WRITE_MODE;
  bs->eob = 0;
  bs->eobs = 0;
}

/*close the device containing the bit stream */
void shine_close_bit_stream(bitstream_t *bs)
{
  if (bs->data) free(bs->data);
  free(bs->buf);
}

/*
 * shine_putbits:
 * --------
 * write N bits into the bit stream.
 * bs = bit stream structure
 * val = value to write into the buffer
 * N = number of bits of val
 */
void shine_putbits(bitstream_t *bs, unsigned long int val, unsigned int N)
{
  static int putmask[9]={0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff};
  register int j = N;
  register int k, tmp;

  #ifdef DEBUG
  if (N > MAX_LENGTH)
    printf("Cannot read or write more than %d bits at a time.\n", MAX_LENGTH);
  #endif

  bs->totbit += N;
  while (j > 0)
  {
    k = MIN(j, bs->buf_bit_idx);
    tmp = val >> (j-k);
    bs->buf[bs->buf_byte_idx] |= (tmp&putmask[k]) << (bs->buf_bit_idx-k);
    bs->buf_bit_idx -= k;
    if (!bs->buf_bit_idx)
    {
      bs->buf_bit_idx = 8;
      bs->buf_byte_idx--;
      if (bs->buf_byte_idx < 0)
        shine_empty_buffer(bs, MINIMUM);
      bs->buf[bs->buf_byte_idx] = 0;
    }
    j -= k;
  }
}

/*return the current bit stream length (in bits)*/
unsigned long shine_sstell(bitstream_t *bs)
{
  return(bs->totbit);
}

