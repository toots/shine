/* The arm64 set: the bodies built on vectors of 4 lanes, each lane one
 * filter step or one subband. Not where the build has no vector registers
 * (-mgeneral-regs-only). */
#include "kernels_body.h"

#if defined(__aarch64__) && defined(__ARM_NEON) &&                             \
    (defined(__clang__) || __GNUC__ >= 9)
typedef int32_t lanes4_t __attribute__((vector_size(16)));
typedef int64_t wide4_t __attribute__((vector_size(32)));

#define LANES 4
#define LANE_T lanes4_t
#define WIDE_T wide4_t
#define WIDEN(x) __builtin_convertvector(x, wide4_t)
#define NARROW(x) __builtin_convertvector(x, lanes4_t)
#define LANE(v, l) (v)[l]
#define FN(name) name##_x4
#include "kernels_lanes.h"

static void filter_neon(const int32_t *x, int32_t s[18][32]) {
  filter_granule_x4(x, s);
}

static void mdct_neon(const int32_t previous[18][32],
                      const int32_t current[18][32], const int32_t window[36],
                      int32_t long_out[32][18], int32_t (*short_out)[192]) {
  mdct_bands_x4(previous, current, window, long_out, short_out);
}

static const shine_kernels_t kernels_neon = {filter_neon, mdct_neon};

const shine_kernels_t *shine_kernels_neon(void) { return &kernels_neon; }
#else
const shine_kernels_t *shine_kernels_neon(void) { return NULL; }
#endif
