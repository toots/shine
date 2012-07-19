#include "types.h"
#include <stdio.h>
#include <stdlib.h>

#define false 0
#define true 1

#ifdef __INLINE_ASM

    #if defined(__ARM_ARCH_5TE__) || defined( __XSCALE__) || defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_4__) || defined(__STRONGARM__)
    #include "mult_sarm_gcc.h"
    #else
    #include "mult_noarch_gcc.h"
    #endif
    
#endif

#define close_bit_stream(a) close_bit_stream_r(a)
