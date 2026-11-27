#ifndef BITSTREAM_H
#define BITSTREAM_H

typedef struct bit_stream_struc {
  unsigned char *data;     /* Processed data */
  int data_size;           /* Total data size */
  int data_position;       /* Data position */
  unsigned int cache;      /* bit stream cache */
  unsigned int cache_bits; /* free bits in cache */
} bitstream_t;

/* "bit_stream.h" Definitions */

#define MINIMUM 4 /* Minimum size of the buffer in bytes */
#define MAX_LENGTH                                                             \
  32 /* Maximum length of word written or                                      \
        read from bit stream */

#define BUFFER_SIZE 4096

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

/* Slots whose capacity main data has not reached yet: the reservoir never
 * spans more (doc/psy/reservoir.md §2). */
#define SLOT_QUEUE 128

/* The frames of a stream, laid out as slots of fixed length whose main data
 * may begin in an earlier slot (doc/psy/reservoir.md §1, §4). */
typedef struct {
  unsigned char *data;
  int size;
  int length;   /* end of the last slot begun */
  int main;     /* where the next byte of main data goes */
  int slot;     /* start of the slot holding main */
  int returned; /* bytes handed to the caller, dropped by the next frame */
  int starts[SLOT_QUEUE]; /* slots main data has not reached, oldest first */
  int first, count;
  int failed; /* a frame could not be laid out: nothing more is returned */
} frame_stream_t;

/* Appends a frame's slot and its main data. frame holds header_bytes of
 * header and side information, then main_bytes of main data; the slot is
 * slot_bytes long. Returns -1 when out of memory or when the frame does not
 * fit the layout, and on every call after that: a later frame's
 * main_data_begin counts the bytes of the one that is missing. */
int shine_stream_frame(frame_stream_t *stream, const unsigned char *frame,
                       int header_bytes, int main_bytes, int slot_bytes);

/* The complete slots not returned yet, or every slot begun when ending the
 * stream; NULL and nothing written once a frame has failed. */
unsigned char *shine_stream_take(frame_stream_t *stream, int end, int *written);

int shine_open_bit_stream(bitstream_t *bs, const int size);
void shine_close_bit_stream(bitstream_t *bs);
void shine_putbits(bitstream_t *bs, unsigned int val, unsigned int N);
void shine_flush_bit_stream(bitstream_t *bs);
int shine_get_bits_count(bitstream_t *bs);

#endif
