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
#include <string.h>

/* open the device to write the bit stream into it */
int shine_open_bit_stream(bitstream_t *bs, int size) {
  bs->data = (unsigned char *)malloc(size * sizeof(unsigned char));
  if (!bs->data)
    return -1;
  bs->data_size = size;
  bs->data_position = 0;
  bs->cache = 0;
  bs->cache_bits = 64;
  return 0;
}

/* Makes room for one more word. Unreachable in practice: a call to the
 * encoder writes at most two frames, far below BUFFER_SIZE. On failure,
 * bits are dropped rather than written out of bounds. */
static int ensure_word_space(bitstream_t *bs) {
  unsigned char *data;

  if (bs->data_position + (int)sizeof(bs->cache) < bs->data_size)
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

/* Most significant byte first, on any host. */
static void store_word(unsigned char *out, uint64_t word) {
  for (int byte = 0; byte < 8; byte++)
    out[byte] = (unsigned char)(word >> (56 - 8 * byte));
}

/* The part of shine_putbits that fills the cache: N >= cache_bits. */
void shine_putbits_word(bitstream_t *bs, uint64_t val, unsigned int N) {
  if (ensure_word_space(bs) < 0)
    return;

  N -= bs->cache_bits;
  store_word(bs->data + bs->data_position, bs->cache | val >> N);
  bs->data_position += sizeof(bs->cache);
  bs->cache_bits = 64 - N;
  bs->cache = N ? val << bs->cache_bits : 0;
}

/* Write out the whole bytes left in the cache. */
void shine_flush_bit_stream(bitstream_t *bs) {
  if (ensure_word_space(bs) < 0)
    return;

  for (; bs->cache_bits <= 56; bs->cache_bits += 8) {
    bs->data[bs->data_position++] = bs->cache >> 56;
    bs->cache <<= 8;
  }
}

static void drop_returned(frame_stream_t *stream) {
  int dropped = stream->returned;

  if (!dropped)
    return;
  memmove(stream->data, stream->data + dropped, stream->length - dropped);
  stream->length -= dropped;
  stream->main -= dropped;
  stream->slot -= dropped;
  for (int i = 0; i < stream->count; i++)
    stream->starts[(stream->first + i) % SLOT_QUEUE] -= dropped;
  stream->returned = 0;
}

static int reserve_slot(frame_stream_t *stream, int slot_bytes) {
  int needed = stream->length + slot_bytes;
  unsigned char *data;

  if (needed <= stream->size)
    return 0;
  data = realloc(stream->data, 2 * needed);
  if (!data)
    return -1;
  stream->data = data;
  stream->size = 2 * needed;
  return 0;
}

int shine_stream_frame(frame_stream_t *stream, const unsigned char *frame,
                       int header_bytes, int main_bytes, int slot_bytes) {
  const unsigned char *main_data = frame + header_bytes;

  if (stream->failed)
    return -1;
  drop_returned(stream);
  if (stream->count == SLOT_QUEUE || reserve_slot(stream, slot_bytes) < 0)
    return stream->failed = -1;

  memcpy(stream->data + stream->length, frame, header_bytes);
  memset(stream->data + stream->length + header_bytes, 0,
         slot_bytes - header_bytes);
  stream->starts[(stream->first + stream->count++) % SLOT_QUEUE] =
      stream->length;
  stream->length += slot_bytes;

  while (main_bytes > 0) {
    int limit, bytes;

    if (stream->count && stream->main == stream->starts[stream->first]) {
      stream->slot = stream->main;
      stream->main += header_bytes;
      stream->first = (stream->first + 1) % SLOT_QUEUE;
      stream->count--;
    }
    limit = stream->count ? stream->starts[stream->first] : stream->length;
    bytes = MIN(main_bytes, limit - stream->main);
    if (bytes <= 0)
      return stream->failed = -1;
    memcpy(stream->data + stream->main, main_data, bytes);
    stream->main += bytes;
    main_data += bytes;
    main_bytes -= bytes;
  }
  return 0;
}

unsigned char *shine_stream_take(frame_stream_t *stream, int end,
                                 int *written) {
  int complete = stream->slot;

  if (stream->failed) {
    *written = 0;
    return NULL;
  }
  if (end || stream->main == stream->length)
    complete = stream->length;
  else if (stream->count && stream->main == stream->starts[stream->first])
    complete = stream->main;

  *written = complete - stream->returned;
  stream->returned = complete;
  return stream->data + complete - *written;
}

int shine_get_bits_count(bitstream_t *bs) {
  return bs->data_position * 8 + 64 - bs->cache_bits;
}
