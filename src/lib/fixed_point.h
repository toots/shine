#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

/* Fractional multiplies of 32-bit values. Current compilers turn them into
 * single instructions, e.g. smmul and smmulr on ARMv7, mult and mfhi on MIPS.
 */

/* a * b / 2**32 */
#define mul(a, b) (int32_t)((((int64_t)a) * ((int64_t)b)) >> 32)

/* a * b / 2**32, rounded */
#define mulr(a, b)                                                             \
  (int32_t)(((((int64_t)a) * ((int64_t)b)) + 0x80000000LL) >> 32)

/* Complex multiply, (dre, dim) = (are, aim) * (bre, bim) / 2**31 */
#define cmuls(dre, dim, are, aim, bre, bim)                                    \
  do {                                                                         \
    int32_t tre;                                                               \
    (tre) = (int32_t)(((int64_t)(are) * (int64_t)(bre) -                       \
                       (int64_t)(aim) * (int64_t)(bim)) >>                     \
                      31);                                                     \
    (dim) = (int32_t)(((int64_t)(are) * (int64_t)(bim) +                       \
                       (int64_t)(aim) * (int64_t)(bre)) >>                     \
                      31);                                                     \
    (dre) = tre;                                                               \
  } while (0)

#endif
