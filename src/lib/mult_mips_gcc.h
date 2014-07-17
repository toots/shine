/*
  ASM functions:

    mul     Fractional multiply.
    muls    Fractional multiply with single bit left shift.
    mulr    Fractional multiply with rounding.
    mulsr   Fractional multiply with single bit left shift and rounding.

*/

#include <stdint.h>

#define mul(a,b) \
({ \ 
	register int32_t res; \
	__asm__ __volatile__("mult %0, %1" : : "r" (a), "r" (b)); \
	__asm__ __volatile__("mfhi %0" : "=&r" (res)); \
	res; \
})

#define cmuls(dre, dim, are, aim, bre, bim) \
do { \
	register int32_t t1, t2, tre; \
	__asm__ __volatile__("mult %0, %1" : : "r" (are), "r" (bre)); \
	__asm__ __volatile__("msub %0, %1" : : "r" (aim), "r" (bim)); \
	__asm__ __volatile__("mfhi %0; mflo %1" : "=&r" (t1), "=&r" (t2)); \
	tre = (t1 << 1) | ((uint32_t)t2 >> 31); \
	__asm__ __volatile__("mult %0, %1" : : "r" (are), "r" (bim)); \
	__asm__ __volatile__("madd %0, %1" : : "r" (bre), "r" (aim)); \
	__asm__ __volatile__("mfhi %0; mflo %1" : "=&r" (t1), "=&r" (t2)); \
	dim = (t1 << 1) | ((uint32_t)t2 >> 31); \
	dre = tre; \
} while (0)
