#include "types.h"

#ifdef __INLINE_ASM

    #if defined(__ARM_ARCH_5TE__) || defined( __XSCALE__) || defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_4__) || defined(__STRONGARM__)
    #include "mult_sarm_gcc.h"
    #else
    #include "mult_noarch_gcc.h"
    #endif

#endif
