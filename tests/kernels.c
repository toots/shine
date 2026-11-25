/* The filter bank kernels (src/lib/kernels.h): every set built for this
 * processor gives, bit for bit, the analysis filter and MDCTs written out
 * directly, on random and full-scale input. */
#include "kernels.h"
#include "fixed_point.h"
#include "tables.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t seed = 1;

/* A 16-bit sample in the scale the filter gets it, every fourth round at
 * full scale of either sign. The tables' headroom keeps sums of such input
 * within 32 bits. */
static int32_t random_sample(int round) {
  seed = seed * 1103515245u + 12345u;
  if (round % 4 == 3)
    return seed >> 31 ? INT32_MIN : 32767 * 65536;
  return (int32_t)((int64_t)(int16_t)(seed >> 16) * 65536);
}

static void filter_reference(const int32_t x[512], int off, int32_t s[32]) {
  int32_t y[64];

  for (int i = 0; i < 64; i++) {
    y[i] = 0;
    for (int j = 0; j < 8; j++)
      y[i] += mul(x[(off + i + 64 * j) & 511], shine_enwindow[i + 64 * j]);
  }
  for (int i = 0; i < 32; i++) {
    s[i] = 0;
    for (int j = 0; j < 64; j++)
      s[i] += mul(shine_subband_filter[i][j], y[j]);
  }
}

static void mdct_reference(const int32_t in[36], const int32_t table[18][36],
                           int32_t long_out[18], int32_t short_out[3][6]) {
  for (int k = 0; k < 18; k++) {
    long_out[k] = 0;
    for (int j = 0; j < 36; j++)
      long_out[k] += mul(in[j], table[k][j]);
  }
  for (int window = 0; window < 3; window++)
    for (int k = 0; k < 6; k++) {
      short_out[window][k] = 0;
      for (int j = 0; j < 12; j++)
        short_out[window][k] +=
            mul(in[6 + 6 * window + j], shine_mdct_cosine_short[k][j]);
    }
}

static int check(const char *name, const shine_kernels_t *kernels) {
  static const int32_t (*const tables[3])[36] = {
      shine_mdct_cosine, shine_mdct_cosine_start, shine_mdct_cosine_stop};
  int failures = 0, runs = 0;

  for (int round = 0; round < 400; round++) {
    int32_t x[512], in[36], s[32], s_ref[32];
    int32_t long_out[18], long_ref[18], short_out[3][6], short_ref[3][6];
    const int off = 32 * (round % 16);

    for (int i = 0; i < 512; i++)
      x[i] = random_sample(round);
    for (int i = 0; i < 36; i++)
      in[i] = random_sample(round);
    kernels->filter(x, off, s);
    filter_reference(x, off, s_ref);
    failures += memcmp(s, s_ref, sizeof(s)) != 0;
    for (int t = 0; t < 3; t++) {
      kernels->mdct(in, tables[t], long_out, short_out);
      mdct_reference(in, tables[t], long_ref, short_ref);
      failures += memcmp(long_out, long_ref, sizeof(long_out)) != 0 ||
                  memcmp(short_out, short_ref, sizeof(short_out)) != 0;
      kernels->mdct(in, tables[t], long_out, NULL);
      failures += memcmp(long_out, long_ref, sizeof(long_out)) != 0;
      runs += 2;
    }
    runs++;
  }
  printf("%-6s %d checks, %d failed\n", name, runs, failures);
  return failures || runs == 0;
}

int main(void) {
  shine_kernels_t selected;
  int failed = check("plain", &shine_kernels_plain);

#ifdef SHINE_KERNELS_AVX2
  __builtin_cpu_init();
  if (__builtin_cpu_supports("avx2"))
    failed |= check("avx2", &shine_kernels_avx2);
  else
    printf("avx2   not run: the processor does not have it\n");
#endif
  shine_kernels_select(&selected);
  failed |= check("chosen", &selected);
  return failed;
}
