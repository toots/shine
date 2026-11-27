/* Generates src/lib/precomputed_tables.c, so that the library computes no
 * floating point at run time. The expressions are those the encoder used to
 * evaluate at initialisation.
 *
 * cc -Isrc/lib tools/generate_tables.c src/lib/huffman.c -lm \
 *    -o generate_tables
 * ./generate_tables > src/lib/precomputed_tables.c
 * clang-format -i src/lib/precomputed_tables.c
 */

#include "huffman.h"
#include "resample.h"
#include "tables.h"
#include "types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979
#define PI36 0.087266462599717

/* The DCT-IV of size n, cos(pi (2i + 1) (2k + 1) / (4n)), in Q31. */
static void dct4_table(const char *name, int n) {
  printf("const int32_t %s[%d][%d] = {\n", name, n, n);
  for (int i = 0; i < n; i++) {
    printf("    {");
    for (int k = 0; k < n; k++)
      printf(
          "%s%ld", k ? ", " : "",
          lround(cos(PI * (2 * i + 1) * (2 * k + 1) / (4 * n)) * 2147483648.0));
    printf("},\n");
  }
  printf("};\n\n");
}

/* cos and sin of pi (j + 1/8) / n for j < n/2, in Q31: the twiddles of a
 * DCT-IV of size n computed with a DFT of size n/2. */
static void dct4_twiddle(int n) {
  printf("const int32_t shine_dct4_twiddle_%d[%d][2] = {", n, n / 2);
  for (int j = 0; j < n / 2; j++) {
    double angle = PI * (j + 0.125) / n;

    printf("%s{%ld, %ld}", j ? ", " : "", lround(cos(angle) * 2147483648.0),
           lround(sin(angle) * 2147483648.0));
  }
  printf("};\n\n");
}

static void subband_filter(void) {
  const long c = lround(cos(PI / 4) * 2147483648.0);

  printf("/* The leaves of the polyphase filter's DCT-III of size 32 computed "
         "directly:\n * the DCT-III of size 2 and the DCT-IVs of sizes 2 and "
         "4, in Q31. */\n");
  printf("const int32_t shine_dct3_2[2][2] = {{%d, %ld}, {%d, %ld}};\n\n",
         0x7fffffff, c, 0x7fffffff, -c);
  dct4_table("shine_dct4_2", 2);
  dct4_table("shine_dct4_4", 4);
  printf("/* The twiddles of the DCT-IVs computed with a DFT. */\n");
  dct4_twiddle(6);
  dct4_twiddle(8);
  dct4_twiddle(16);
  dct4_twiddle(18);
  printf("/* cos and sin of 2 pi k / 9 for k = 1, 2 and 4, in Q31. */\n");
  printf("const int32_t shine_dft9_twiddle[3][2] = {");
  for (int k = 1; k <= 4; k *= 2)
    printf("%s{%ld, %ld}", k > 1 ? ", " : "",
           lround(cos(2 * PI * k / 9) * 2147483648.0),
           lround(sin(2 * PI * k / 9) * 2147483648.0));
  printf("};\n\n");
}

/* Window of block type `type` (ISO 11172-3 2.4.3.4.10.3), sample i of 36.
 * The long analysis of a short block uses the normal window. */
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

static void mdct_cosine(void) {
  printf("/* The windows of the long MDCT by block type, in Q31. Scaled by "
         "8/9, as\n * the decoder's IMDCT and overlap-add have a gain of 9 "
         "where the rest of\n * the encoder compensates 8. */\n");
  printf("const int32_t shine_mdct_window[4][36] = {\n");
  for (int type = 0; type < 4; type++) {
    printf("    {");
    for (int i = 0; i < 36; i++)
      printf("%s%ld", i ? ", " : "",
             lround(block_window(type, i) * (8.0 / 9.0) * 2147483648.0));
    printf("},\n");
  }
  printf("};\n\n");
  printf("/* The window of a short MDCT, in Q31. The decoder's short IMDCT "
         "has a\n * gain of 3 where the long one has 9, which would make the "
         "scale 8/3. With\n * the 1/4 of MDCT_HEADROOM_STEPS, the sum of the "
         "absolute values of the\n * terms of a coefficient is then up to 3.34 "
         "times the input: scaled by a\n * further 1/4, see "
         "SHORT_HEADROOM_STEPS. */\n");
  printf("const int32_t shine_mdct_window_short[12] = {");
  for (int i = 0; i < 12; i++)
    printf("%s%ld", i ? ", " : "",
           lround(sin(PI / 12 * (i + 0.5)) * (2.0 / 3.0) * 2147483648.0));
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

/* Whether table can code a region whose largest value is max. In a family
 * of tables sharing their codes only the first one counts, the others cost
 * more linbits. */
static int huffman_codes(int table, int max) {
  const struct huffcodetab *h = &shine_huffman_table[table];

  if (!h->hlen)
    return 0;
  if (h->linbits)
    return (table == 16 || table == 24) && max <= 15 + (int)h->linmax;
  return max < (int)h->xlen;
}

/* Bits of the pair (x, y) in table, without the sign bits. */
static int huffman_pair_bits(int table, int x, int y) {
  const struct huffcodetab *h = &shine_huffman_table[table];

  return h->hlen[x * h->ylen + y] + (x > 14 ? h->linbits : 0) +
         (y > 14 ? h->linbits : 0);
}

/* Whether no region of values up to max makes table the first cheapest
 * one: another table costs no more on every pair and has a lower index, or
 * costs less on every pair. */
static int huffman_never_chosen(int table, int max) {
  for (int other = 1; other < 32; other++) {
    int no_more = 1, less = 1;

    if (other == table || !huffman_codes(other, max))
      continue;
    for (int x = 0; x <= max; x++)
      for (int y = 0; y <= max; y++) {
        int bits = huffman_pair_bits(table, x, y);
        int other_bits = huffman_pair_bits(other, x, y);

        if (other_bits > bits)
          no_more = 0;
        if (other_bits >= bits)
          less = 0;
      }
    if ((other < table && no_more) || less)
      return 1;
  }
  return 0;
}

static int huffman_candidates(int max, int candidates[HUFFMAN_CHOICES]) {
  int count = 0;

  for (int table = 1; table < 32; table++)
    if (huffman_codes(table, max) && !huffman_never_chosen(table, max)) {
      if (count == HUFFMAN_CHOICES) {
        fprintf(stderr, "more than HUFFMAN_CHOICES candidates\n");
        exit(1);
      }
      candidates[count++] = table;
    }
  return count;
}

static int same_candidates(int max, int other_max) {
  int candidates[HUFFMAN_CHOICES], other[HUFFMAN_CHOICES];
  int count = huffman_candidates(max, candidates);

  if (count != huffman_candidates(other_max, other))
    return 0;
  for (int i = 0; i < count; i++)
    if (candidates[i] != other[i])
      return 0;
  return 1;
}

/* Maxima with the same candidates share one block of costs, indexed with
 * the largest of them. */
static void huffman_choice(void) {
  int stride[16], offset[16], words[16], size = 0;

  for (int max = 15; max >= 1; max--) {
    int candidates[HUFFMAN_CHOICES];

    words[max] = huffman_candidates(max, candidates) > 4 ? 2 : 1;
    if (max < 15 && same_candidates(max, max + 1)) {
      stride[max] = stride[max + 1];
      offset[max] = offset[max + 1];
      continue;
    }
    stride[max] = max + 1;
    offset[max] = size;
    size += words[max] * stride[max] * stride[max];
  }

  printf("const shine_huffman_choice_t shine_huffman_choice[16] = {\n    {0}");
  for (int max = 1; max <= 15; max++) {
    int candidates[HUFFMAN_CHOICES];
    int count = huffman_candidates(max, candidates);

    printf(",\n    {%d, %d, %d, {", count, stride[max], offset[max]);
    for (int i = 0; i < count; i++)
      printf("%s%d", i ? ", " : "", candidates[i]);
    printf("}}");
  }
  printf("};\n\n");

  printf("const uint64_t shine_huffman_choice_cost[%d] = {\n", size);
  for (int max = 15; max >= 1; max--) {
    int candidates[HUFFMAN_CHOICES];
    int count = huffman_candidates(max, candidates);

    if (max < 15 && offset[max] == offset[max + 1])
      continue;
    for (int x = 0; x <= max; x++)
      for (int y = 0; y <= max; y++)
        for (int word = 0; word < words[max]; word++) {
          uint64_t packed = 0;

          for (int i = 4 * word; i < count && i < 4 * word + 4; i++)
            packed |= (uint64_t)huffman_pair_bits(candidates[i], x, y)
                      << (16 * (i - 4 * word));
          printf("0x%llxULL,", (unsigned long long)packed);
        }
  }
  printf("};\n\n");

  printf("const uint32_t shine_huffman_escape_cost[256] = {\n");
  for (int pair = 0; pair < 256; pair++)
    printf("0x%xU,", shine_huffman_table[16].hlen[pair] |
                         shine_huffman_table[24].hlen[pair] << 16);
  printf("};\n");
}

/* Modified Bessel function of the first kind, order 0. */
static double bessel_i0(double x) {
  double sum = 1, term = 1;

  for (int k = 1; k < 64; k++) {
    term *= (x / (2 * k)) * (x / (2 * k));
    sum += term;
  }
  return sum;
}

/* doc/psy/resampling.md §2: sinc(x) under a Kaiser window of half length
 * RESAMPLE_ZEROS, at x = (i - 1) / RESAMPLE_POINTS, in Q30. */
#define RESAMPLE_KAISER_BETA 8.0
static void resample_prototype(void) {
  const int points = RESAMPLE_ZEROS * RESAMPLE_POINTS;

  printf("\n/* The resampling filter prototype, from one point before its "
         "centre. */\n");
  printf("const int32_t shine_resample_prototype[%d] = {\n", points + 3);
  for (int i = -1; i <= points + 1; i++) {
    double x = fabs((double)i) / RESAMPLE_POINTS;
    double position = x / RESAMPLE_ZEROS;
    double value = 0;

    if (i == 0)
      value = 1;
    else if (position < 1)
      value = sin(PI * x) / (PI * x) *
              bessel_i0(RESAMPLE_KAISER_BETA * sqrt(1 - position * position)) /
              bessel_i0(RESAMPLE_KAISER_BETA);
    printf("%ld,", lround(value * (1 << 30)));
  }
  printf("};\n");
}

int main(void) {
  printf("/* Generated by tools/generate_tables.c: do not edit. */\n\n");
  printf("#include \"tables.h\"\n\n");
  subband_filter();
  mdct_cosine();
  mdct_aliasing();
  quantizer_steps();
  printf("\n");
  huffman_choice();
  resample_prototype();
  return 0;
}
