/*
  ASM functions:

    mul     Fractional multiply.
    muls    Fractional multiply with single bit left shift.
    mulr    Fractional multiply with rounding.
    mulsr   Fractional multiply with single bit left shift and rounding.

*/

#include <stdint.h>

/* Fractional multiply */
static inline int32_t mul(int32_t x,int32_t y) {
    register int32_t result;
#if __ARM_ARCH >= 6
    asm ("smmul %0, %2, %1" : "=r" (result) : "r" (x), "r" (y));
#else
    asm ("smull r3, %0, %2, %1" : "=r" (result) : "r" (x), "r" (y) : "r3" );
#endif
    return result;
}

/* Fractional multiply with single bit left shift. */
static inline int32_t muls(int32_t x, int32_t y) {
    int32_t result;
    asm (
        "smull r3, %0, %2, %1\n\t"
        "movs r3, r3, lsl #1\n\t"
        "adc %0, %0, %0"
        : "=r" (result) : "r" (x), "r" (y) : "r3", "cc"
    );
    return result;
}

static inline int32_t mulr(int32_t x, int32_t y) {
    int32_t result;
#if __ARM_ARCH >= 6
    asm ("smmulr %0, %2, %1" : "=r" (result) : "r" (x), "r" (y));
#else
    asm (
        "smull r3, %0, %2, %1\n\t"
        "adds r3, r3, #0x80000000\n\t"
        "adc %0, %0, #0"
        : "=r" (result) : "r" (x), "r" (y) : "r3", "cc"
    );
#endif
    return result;
}

static inline int32_t mulsr(int32_t x, int32_t y) {
    int32_t result;
    asm (
        "smull r3, %0, %1, %2\n\t"
        "movs r3, r3, lsl #1\n\t"
        "adc %0, %0, %0\n\t"
        "adds r3, r3, #0x80000000\n\t"
        "adc %0, %0, #0"
        : "=r" (result) : "r" (x), "r" (y) : "r3", "cc"
    );
    return result;
}

#define mul0(hi,lo,a,b) \
    asm ("smull %0, %1, %2, %3" : "=r" (lo), "=r" (hi) : "r" (a), "r" (b))

#define muladd(hi,lo,a,b) \
    asm ("smlal %0, %1, %2, %3" : "+r" (lo), "+r" (hi) : "r" (a), "r" (b))

#define mulsub(hi,lo,a,b) \
    asm ("smlal %0, %1, %2, %3" : "+r" (lo), "+r" (hi) : "r" (a), "r" (-(b)))

#define mulz(hi,lo)

#define cmuls(dre, dim, are, aim, bre, bim) \
do { \
    register int32_t tre, tim; \
    asm ( \
        "smull r3, %0, %2, %4\n\t" \
        "smlal r3, %0, %3, %5\n\t" \
        "movs r3, r3, lsl #1\n\t" \
        "adc %0, %0, %0\n\t" \
        "smull r3, %1, %2, %6\n\t" \
        "smlal r3, %1, %4, %3\n\t" \
        "movs r3, r3, lsl #1\n\t" \
        "adc %1, %1, %1\n\t" \
        : "=&r" (tre), "=&r" (tim) \
        : "r" (are), "r" (aim), "r" (bre), "r" (-(bim)), "r" (bim) \
        : "r3", "cc" \
    ); \
    dre = tre; \
    dim = tim; \
} while (0)
