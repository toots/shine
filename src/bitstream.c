/*
 *  bit_stream.c package
 *  Author:  Jean-Georges Fritsch, C-Cube Microsystems
 *
 * This package provides functions to write information to the bit stream.
 *
 * Removed unused functions. Feb 2001 P.Everett
 */

#include "g_includes.h"
#include "bitstream.h"

/*
 * empty_buffer
 * ------------
 * empty the buffer to the output device when the buffer becomes full
 */
void empty_buffer(bitstream_t *bs, int minimum, config_t *config)
{
  register int i;

  for (i=bs->buf_size-1;i>=minimum;i--)
    config->write_mp3(sizeof(unsigned char), &bs->buf[i], config);

//  fwrite(&bs->buf[i], sizeof(unsigned char), 1, bs->pt);

/*  fflush(bs->pt);*/ /* NEW SS to assist in debugging*/

  for (i=minimum-1; i>=0; i--)
    bs->buf[bs->buf_size - minimum + i] = bs->buf[i];

  bs->buf_byte_idx = bs->buf_size -1 - minimum;
  bs->buf_bit_idx = 8;
}

/* open the device to write the bit stream into it */
void open_bit_stream_w(bs, bs_filenam, size)
bitstream_t *bs;   /* bit stream structure */
char *bs_filenam;       /* name of the bit stream file */
int size;               /* size of the buffer */
{
/* NO LONGER DONE HERE
    if ((bs->pt = fopen(bs_filenam, "wb")) == NULL) {
      #ifdef DEBUG
      printf("Could not create \"%s\".\n", bs_filenam);
      #endif
      exit(1);
   }
*/
   bs->buf = (unsigned char *)malloc(size*sizeof(unsigned char));
   bs->buf_size = size;
   bs->buf_byte_idx = size-1;
   bs->buf_bit_idx=8;
   bs->totbit=0;
   bs->mode = WRITE_MODE;
   bs->eob = false;
   bs->eobs = false;
}

/*close the device containing the bit stream */
void close_bit_stream(bitstream_t *bs)
{
  /* fclose(bs->pt); */ /* NO LONGER DONE HERE */
  free(bs->buf);
}

/*
 * putbits:
 * --------
 * write N bits into the bit stream.
 * bs = bit stream structure
 * val = value to write into the buffer
 * N = number of bits of val
 */
void putbits(bitstream_t *bs, unsigned long int val, unsigned int N, config_t *config)
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
        empty_buffer(bs, MINIMUM, config);
      bs->buf[bs->buf_byte_idx] = 0;
    }
    j -= k;
  }
}

/*return the current bit stream length (in bits)*/
unsigned long sstell(bitstream_t *bs)
{
  return(bs->totbit);
}

