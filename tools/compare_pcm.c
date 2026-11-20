/* Compares decoded PCM with its reference, for tools/quality-report.sh.
 *
 * compare_pcm <channels> <reference.raw> <decoded.raw>
 *
 * Both are interleaved signed 16-bit native-endian PCM at the same rate.
 * Finds the delay of the decoded stream by cross-correlation, then prints,
 * averaged over the channels: the level of the decoded stream relative to
 * the reference in dB, the gain-corrected SNR in dB, and the delay in
 * samples. A periodic signal correlates as well at every period: items
 * need some aperiodic content for the delay to be found. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_DELAY 8192
#define WINDOW 32768

static short *load(const char *path, long *count) {
  FILE *file = fopen(path, "rb");
  short *samples = NULL;
  long size;

  if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) || !(samples = malloc(size + 1))) {
    fprintf(stderr, "compare_pcm: cannot read %s\n", path);
    exit(2);
  }
  *count = (long)fread(samples, sizeof(short), size / sizeof(short), file);
  fclose(file);
  return samples;
}

int main(int argc, char **argv) {
  long reference_count, decoded_count, frames, start, window, delay = 0;
  short *reference, *decoded;
  double best = -INFINITY, level = 0, snr = 0;
  int channels;

  if (argc != 4 || (channels = atoi(argv[1])) < 1) {
    fprintf(stderr, "usage: compare_pcm <channels> <reference.raw> "
                    "<decoded.raw>\n");
    return 2;
  }
  reference = load(argv[2], &reference_count);
  decoded = load(argv[3], &decoded_count);
  reference_count /= channels;
  decoded_count /= channels;

  /* A window from the middle of the item, away from silent intros. */
  window = reference_count < WINDOW ? reference_count / 2 : WINDOW;
  start = (reference_count - window) / 2;
  for (long d = 0; d < MAX_DELAY && start + window + d <= decoded_count; d++) {
    double sum = 0;

    for (long i = start; i < start + window; i++)
      for (int c = 0; c < channels; c++)
        sum += (double)reference[i * channels + c] *
               decoded[(i + d) * channels + c];
    if (sum > best) {
      best = sum;
      delay = d;
    }
  }

  frames = reference_count < decoded_count - delay ? reference_count
                                                   : decoded_count - delay;
  if (frames <= 0) {
    fprintf(stderr, "compare_pcm: the decoded stream is too short\n");
    return 1;
  }
  for (int c = 0; c < channels; c++) {
    double rr = 0, dd = 0, rd = 0, gain, error = 0;

    for (long i = 0; i < frames; i++) {
      double r = reference[i * channels + c],
             x = decoded[(i + delay) * channels + c];
      rr += r * r;
      dd += x * x;
      rd += r * x;
    }
    if (rr == 0 || dd == 0) {
      fprintf(stderr, "compare_pcm: channel %d is silent\n", c);
      return 1;
    }
    gain = rd / dd;
    for (long i = 0; i < frames; i++) {
      double e = reference[i * channels + c] -
                 gain * decoded[(i + delay) * channels + c];
      error += e * e;
    }
    level += 10 * log10(dd / rr) / channels;
    snr += 10 * log10(rr / error) / channels;
  }
  printf("%+.2f %.2f %ld\n", level, snr, delay);
  return 0;
}
