#ifndef BITSTREAM_H
#define BITSTREAM_H

typedef struct  bit_stream_struc {
    unsigned char *data;        /* Processed data */
    int         data_size;      /* Total data size */
    int         data_position;  /* Data position */
    unsigned char *buf;         /* bit stream buffer */
    int         buf_size;       /* size of buffer (in number of bytes) */
    long        totbit;         /* bit counter of bit stream */
    int         buf_byte_idx;   /* pointer to top byte in buffer */
    int         buf_bit_idx;    /* pointer to top bit of top byte in buffer */
    int         mode;           /* bit stream open in read or write mode */
    int         eob;            /* end of buffer index */
    int         eobs;           /* end of bit stream flag */
    char        format;

    /* format of file in rd mode (BINARY/ASCII) */
} bitstream_t;

/* "bit_stream.h" Definitions */

#define         MINIMUM         4    /* Minimum size of the buffer in bytes */
#define         MAX_LENGTH      32   /* Maximum length of word written or
                                        read from bit stream */
#define         READ_MODE       0
#define         WRITE_MODE      1
#define         ALIGNING        8
#define         BINARY          0
#define         ASCII           1

#ifndef BS_FORMAT
#define         BS_FORMAT       ASCII /* BINARY or ASCII = 2x bytes */
#endif

#define         BUFFER_SIZE     4096

#define         MIN(A, B)       ((A) < (B) ? (A) : (B))
#define         MAX(A, B)       ((A) > (B) ? (A) : (B))


int refill_buffer(bitstream_t *bs);
void shine_empty_buffer(bitstream_t *bs,int minimum);
void shine_open_bit_stream(bitstream_t *bs,const int size);
void shine_close_bit_stream(bitstream_t *bs);
void alloc_buffer(bitstream_t *bs,int size);
void desalloc_buffer(bitstream_t *bs);
void back_track_buffer(bitstream_t *bs,int N);
unsigned int get1bit(bitstream_t *bs);
void put1bit(bitstream_t *bs,int bit);
unsigned long look_ahead(bitstream_t *bs,int N);
unsigned long getbits(bitstream_t *bs,int N);
void shine_putbits(bitstream_t *bs,unsigned long int val, unsigned int N);
void byte_ali_shine_putbits(bitstream_t *bs,unsigned int val,int N);
unsigned long byte_ali_getbits(bitstream_t *bs,int N);
unsigned long shine_sstell(bitstream_t *bs);
int end_bs(bitstream_t *bs);
int seek_sync(bitstream_t *bs,long sync,int N);

unsigned long hgetbits(int N);
#define  hget1bit() hgetbits(1)

#endif
