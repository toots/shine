/* Generates src/lib/precomputed_tables.c, so that the library computes no
 * floating point at run time. The expressions are those the encoder used to
 * evaluate at initialisation.
 *
 * cc -Isrc/lib tools/generate_tables.c -lm -o generate_tables
 * ./generate_tables > src/lib/precomputed_tables.c
 * clang-format -i src/lib/precomputed_tables.c
 */

#include "tables.h"
#include "types.h"

#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979
#define PI36 0.087266462599717
#define PI64 0.049087385212

static void subband_filter(void) {
  printf(
      "/* Polyphase filterbank coefficients, rounded to the 9th decimal like "
      "the\n * ISO tables. */\n");
  printf("const int32_t shine_subband_filter[SBLIMIT][64] = {\n");
  for (int i = 0; i < SBLIMIT; i++) {
    printf("    {");
    for (int j = 0; j < 64; j++) {
      double filter = 1e9 * cos((double)((2 * i + 1) * (16 - j) * PI64));
      if (filter >= 0)
        modf(filter + 0.5, &filter);
      else
        modf(filter - 0.5, &filter);
      printf("%s%d", j ? ", " : "", (int32_t)(filter * (0x7fffffff * 1e-9)));
    }
    printf("},\n");
  }
  printf("};\n\n");
}

static void mdct_cosine(void) {
  printf(
      "/* MDCT coefficients, combined with the window. Scaled by 8/9, as the\n"
      " * decoder's IMDCT and overlap-add have a gain of 9 where the rest of\n"
      " * the encoder compensates 8, and by 1/4 for headroom: see\n"
      " * MDCT_HEADROOM_STEPS. */\n");
  printf("const int32_t shine_mdct_cosine[18][36] = {\n");
  for (int m = 0; m < 18; m++) {
    printf("    {");
    for (int k = 0; k < 36; k++)
      printf("%s%d", k ? ", " : "",
             (int32_t)(sin(PI36 * (k + 0.5)) *
                       cos((PI / 72) * (2 * k + 19) * (2 * m + 1)) *
                       (2.0 / 9.0) * 0x7fffffff));
    printf("},\n");
  }
  printf("};\n\n");
}

/* Window of block type `type` (ISO 11172-3 2.4.3.4.10.3), sample i of 36. */
static double block_window(int type, int i) {
  if (type == 1)
    return i < 18   ? sin(PI36 * (i + 0.5))
           : i < 24 ? 1
           : i < 30 ? sin(PI / 12 * (i - 18 + 0.5))
                    : 0;
  if (type == 3)
    return i < 6    ? 0
           : i < 12 ? sin(PI / 12 * (i - 6 + 0.5))
           : i < 18 ? 1
                    : sin(PI36 * (i + 0.5));
  return sin(PI36 * (i + 0.5));
}

/* (n - 0.4054)**(4/3) in Q8, rounded up. */
static int shine_quant_threshold_value(int n) {
  return n ? (int)ceil(pow(n - 0.4054, 4.0 / 3) * 256) : 0;
}

static void mdct_cosine_switching(void) {
  static const char *names[2] = {"start", "stop"};

  printf("/* As shine_mdct_cosine, with the windows of block types 1 (start)\n"
         " * and 3 (stop). */\n");
  for (int type = 1; type <= 3; type += 2) {
    printf("const int32_t shine_mdct_cosine_%s[18][36] = {\n", names[type / 2]);
    for (int m = 0; m < 18; m++) {
      printf("    {");
      for (int k = 0; k < 36; k++)
        printf("%s%d", k ? ", " : "",
               (int32_t)(block_window(type, k) *
                         cos((PI / 72) * (2 * k + 19) * (2 * m + 1)) *
                         (2.0 / 9.0) * 0x7fffffff));
      printf("},\n");
    }
    printf("};\n\n");
  }
  printf("/* The 12-point MDCT of a short window (block type 2), with its\n"
         " * window. The decoder's short IMDCT has a gain of 3 where the long\n"
         " * one has 9, which would make the scale 3 times that of\n"
         " * shine_mdct_cosine, 2/3. The sum of the absolute values of a row\n"
         " * is then up to 3.34: scaled by a further 1/4, see\n"
         " * SHORT_HEADROOM_STEPS. */\n");
  printf("const int32_t shine_mdct_cosine_short[6][12] = {\n");
  for (int m = 0; m < 6; m++) {
    printf("    {");
    for (int k = 0; k < 12; k++)
      printf("%s%d", k ? ", " : "",
             (int32_t)(sin(PI / 12 * (k + 0.5)) *
                       cos((PI / 24) * (2 * k + 7) * (2 * m + 1)) *
                       (1.0 / 6.0) * 0x7fffffff));
    printf("},\n");
  }
  printf("};\n\n");
}

static void mdct_aliasing(void) {
  static const double coefficients[8] = {-0.6,   -0.535, -0.33,   -0.185,
                                         -0.095, -0.041, -0.0142, -0.0037};

  printf("/* Table B.9: aliasing reduction butterflies. */\n");
  printf("const int32_t shine_mdct_alias_cs[8] = {");
  for (int i = 0; i < 8; i++) {
    double c = coefficients[i];
    printf("%s%d", i ? ", " : "",
           (int32_t)(1.0 / sqrt(1.0 + (c * c)) * 0x7fffffff));
  }
  printf("};\n");
  printf("const int32_t shine_mdct_alias_ca[8] = {");
  for (int i = 0; i < 8; i++) {
    double c = coefficients[i];
    printf("%s%d", i ? ", " : "",
           (int32_t)(c / sqrt(1.0 + (c * c)) * 0x7fffffff));
  }
  printf("};\n\n");
}

static void quantizer_steps(void) {
  printf("/* 2**(-x/4) * 2 for x = -127..0, saturated: quantize() does not "
         "shift\n * its long multiply left to compensate. */\n");
  printf("const int32_t shine_steptabi[128] = {\n");
  for (int i = 0; i < 128; i++) {
    double step = pow(2.0, (double)(127 - i) / 4);
    printf("%s%d", i ? ", " : "    ",
           step * 2 > 0x7fffffff ? 0x7fffffff : (int32_t)(step * 2 + 0.5));
  }
  printf("};\n\n");

  printf("/* 2**(-x/4) * 2**32 for x = 1..MAX_STEPSIZE, index 0 unused. */\n");
  printf("const uint32_t shine_steptab_fraction[MAX_STEPSIZE + 1] = {\n    0");
  for (int i = 1; i <= MAX_STEPSIZE; i++)
    printf(", %uu", (uint32_t)(pow(2.0, -(double)i / 4) * 4294967296.0 + 0.5));
  printf("};\n\n");

  printf("/* nint(x**(3/4) - 0.0946) for x = 0..9999. */\n");
  printf("const int16_t shine_int2idx[10000] = {\n");
  for (int i = 0; i < 10000; i++)
    printf("%s%d", i ? ", " : "    ",
           (int)(sqrt(sqrt((double)i) * (double)i) - 0.0946 + 0.5));
  printf("};\n\n");

  printf(
      "/* The smallest x, in Q8, with nint(x**(3/4) - 0.0946) = n, for\n"
      " * n = 0..QUANT_EXACT_MAX + 1: (n - 0.4054)**(4/3), rounded up. */\n");
  printf("const int32_t shine_quant_threshold[QUANT_EXACT_MAX + 2] = {\n    0");
  for (int n = 1; n <= QUANT_EXACT_MAX + 1; n++)
    printf(", %d", (int32_t)ceil(pow(n - 0.4054, 4.0 / 3) * 256));
  printf("};\n\n");

  /* The scaled coefficient rounded to an integer, ln, covers x in Q8 from
   * 256 ln - 128 to 256 ln + 127, where nint(x**(3/4) - 0.0946) takes at
   * most two values: base, and base + 1 from split on. */
  printf(
      "/* For ln = 0..QUANT_SPLIT_LINES - 1, the scaled coefficient rounded "
      "to\n"
      " * an integer: ix is shine_quant_base[ln], plus 1 when the coefficient\n"
      " * in Q8 is at least shine_quant_split[ln]. */\n");
  {
    int base[QUANT_SPLIT_LINES], split[QUANT_SPLIT_LINES];

    for (int ln = 0; ln < QUANT_SPLIT_LINES; ln++) {
      int low = ln ? 256 * ln - 128 : 0, n = 0;

      while (n <= QUANT_EXACT_MAX && low >= shine_quant_threshold_value(n + 1))
        n++;
      base[ln] = n;
      split[ln] = shine_quant_threshold_value(n + 1) <= 256 * ln + 127
                      ? shine_quant_threshold_value(n + 1)
                      : 0x7fffffff;
    }
    printf("const uint8_t shine_quant_base[QUANT_SPLIT_LINES] = {\n");
    for (int ln = 0; ln < QUANT_SPLIT_LINES; ln++)
      printf("%s%d", ln ? ", " : "    ", base[ln]);
    printf("};\n\nconst int32_t shine_quant_split[QUANT_SPLIT_LINES] = {\n");
    for (int ln = 0; ln < QUANT_SPLIT_LINES; ln++)
      printf("%s%d", ln ? ", " : "    ", split[ln]);
    printf("};\n");
  }
}

int main(void) {
  printf("/* Generated by tools/generate_tables.c: do not edit. */\n\n");
  printf("#include \"tables.h\"\n\n");
  subband_filter();
  mdct_cosine();
  mdct_cosine_switching();
  mdct_aliasing();
  quantizer_steps();
  return 0;
}
