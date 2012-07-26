#warning NO MULT FILE FOR ARCHITECTURE - USING GENERIC MATH

#include <stdint.h>

#define mul(a,b)   (int32_t)  ( ( ((int64_t) a) * ((int64_t) b) ) >>32 )

#define muls(a,b)  (int32_t)  ( ( ((int64_t) a) * ((int64_t) b) ) >>31 )

#define mulr(a,b)  (int32_t)  ( ( ( ((int64_t) a) * ((int64_t) b)) + 0x80000000 ) >>32 )

#define mulsr(a,b) (int32_t)  ( ( ( ((int64_t) a) * ((int64_t) b)) + 0x80000000 ) >>31 )

