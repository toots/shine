/*

  ASM functions:
  
    mul     Fractional multiply.
    muls    Fractional multiply with single bit left shift.
    mulr    Fractional multiply with rounding.
    mulsr   Fractional multiply with single bit left shift and rounding.

*/

/* Fractional multiply */
static inline long mul(long x,long y) { 
    register long result;
    asm ("smull r3, %0, %2, %1" : "=r" (result) : "r" (x), "r" (y) : "r3" );
    return result;
}

/* Fractional multiply with single bit left shift. */
static inline long muls(long x, long y) {
    long result;
    asm (
        "smull r3, %0, %2, %1\n\t"
        "movs r3, r3, lsl #1\n\t"
        "adc %0, %0, %0"
        : "=r" (result) : "r" (x), "r" (y) : "r3"
    );
    return result;
}

static inline long mulr(long x, long y) {
    long result;
    asm (
        "smull r3, %0, %2, %1\n\t"
        "adds r3, r3, #0x80000000\n\t"
        "adc %0, %0, #0"
        : "=r" (result) : "r" (x), "r" (y) : "r3"    
    );
    return result;
}

static inline long mulsr(long x, long y) {
    long result;
    asm (
        "mov r12, %2\n\t"
        "smull r3, r0, %1, r12\n\t"
        "movs r3, r3, lsl #1\n\t"
        "adc r0, r0, r0\n\t"
        "adds r3, r3, #0x80000000\n\t"
        "adc %0, r0, #0"
        : "=r" (result) : "r" (x), "r" (y) : "r0", "r3" , "r12"   
    );
    return result;
}
