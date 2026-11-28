#include "kernels_body.h"
#include <stdio.h>
#include <stdlib.h>

#define LANES 1
#define LANE_T int32_t
#define WIDE_T int64_t
#define WIDEN(x) ((int64_t)(x))
#define NARROW(x) ((int32_t)(x))
#define LANE(v, l) (v)
#define FN(name) name##_x1
#include "kernels_lanes.h"

/* One step at a time: the compiler vectorizes the windowed sums where the
 * instruction set has the multiply. */
static void filter_plain(const int32_t *x, int32_t s[18][32]) {
  for (int step = 0; step < 18; step++) {
    int64_t y[64];
    int32_t a[32];

    filter_window(&x[32 * (17 - step)], y);
    filter_fold(y, a);
    filter_dct3_32_x1(a, s[step]);
  }
}

static void mdct_plain(const int32_t previous[18][32],
                       const int32_t current[18][32], const int32_t window[36],
                       int32_t long_out[32][18], int32_t (*short_out)[192]) {
  mdct_bands_x1(previous, current, window, long_out, short_out);
}

const shine_kernels_t shine_kernels_plain = {.filter = filter_plain,
                                             .mdct = mdct_plain};

static const shine_kernels_t *plain(void) { return &shine_kernels_plain; }

typedef struct {
  const char *name;
  const shine_kernels_t *(*available)(void);
} kernel_set_t;

/* By preference: an extension's set runs on the processors that have it. */
static const kernel_set_t kernel_sets[] = {{"avx2", shine_kernels_avx2},
                                           {"neon", shine_kernels_neon},
                                           {"plain", plain}};

#define KERNEL_SETS ((int)(sizeof(kernel_sets) / sizeof(kernel_sets[0])))

const char *shine_kernels_name(int index) {
  return index >= 0 && index < KERNEL_SETS ? kernel_sets[index].name : NULL;
}

const shine_kernels_t *shine_kernels_named(const char *name) {
  for (int i = 0; i < KERNEL_SETS; i++)
    if (!strcmp(name, kernel_sets[i].name))
      return kernel_sets[i].available();
  return NULL;
}

/* The WebAssembly module imports nothing: no environment, no stderr. */
#ifdef SHINE_NO_ENVIRONMENT
static const char *forced_name(void) { return NULL; }
static void report_unavailable(const char *name) { (void)name; }
#else
static const char *forced_name(void) { return getenv("SHINE_KERNELS"); }

static void report_unavailable(const char *name) {
  fprintf(stderr, "shine: SHINE_KERNELS=%s is not available\n", name);
}
#endif

int shine_kernels_select(shine_kernels_t *kernels) {
  const char *forced = forced_name();
  const shine_kernels_t *set = NULL;

  if (forced && *forced) {
    set = shine_kernels_named(forced);
    if (!set) {
      report_unavailable(forced);
      return -1;
    }
  }
  for (int i = 0; !set; i++)
    set = kernel_sets[i].available();
  *kernels = *set;
  return 0;
}
