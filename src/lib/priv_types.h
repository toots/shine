#ifndef PRIV_TYPES_H
#define PRIV_TYPES_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <math.h>

#include "types.h"
#include "bitstream.h"

/* #define DEBUG if you want the library to dump info to stdout */

#define false 0
#define true 1

#define samp_per_frame2  576

#define PI          3.14159265358979
#define PI4         0.78539816339745
#define PI12        0.26179938779915
#define PI36        0.087266462599717
#define PI64        0.049087385212
#define SQRT2       1.41421356237
#define LN2         0.69314718
#define LN_TO_LOG10 0.2302585093
#define BLKSIZE     1024
#define HAN_SIZE    512
#define SCALE_BLOCK 12
#define SCALE_RANGE 64
#define SCALE       32768
#define SBLIMIT     32


#ifndef bool
typedef unsigned char bool;
#endif

void error(char* s);

#endif
