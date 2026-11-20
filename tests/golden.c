/* Golden stream hashes: encodes synthetic signals in every configuration and
 * compares the hash of each stream with tests/golden.txt. Any change to the
 * encoded bytes fails, so that changes meant to be bit-exact are proven so.
 *
 * Run with SHINE_UPDATE_GOLDEN=1 to rewrite tests/golden.txt, for changes
 * meant to alter the stream.
 *
 * Signals use integer arithmetic only, so that the test itself does not
 * depend on the platform's floating point. */

#include "helpers.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMPLITUDE 20000

typedef void (*signal_generator_t)(int16_t *samples, long count, int channels);

typedef struct {
  const char *name;
  signal_generator_t generate;
} signal_t;

static int16_t clamp(long value) {
  return value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
}

/* Magic circle oscillator: a sine of frequency about
 * step / 65536 / (2 pi) * samplerate, for step / 65536 < 2. */
typedef struct {
  int64_t x, y;
} oscillator_t;

static int16_t oscillate(oscillator_t *oscillator, int64_t step) {
  oscillator->x -= step * oscillator->y / 65536;
  oscillator->y += step * oscillator->x / 65536;
  return clamp(oscillator->y);
}

static void silence(int16_t *samples, long count, int channels) {
  memset(samples, 0, count * channels * sizeof(int16_t));
}

static void white_noise(int16_t *samples, long count, int channels) {
  unsigned int seed = 1;

  for (long i = 0; i < count * channels; i++)
    samples[i] = noise(&seed);
}

static void tones(int16_t *samples, long count, int channels) {
  oscillator_t low = {AMPLITUDE, 0}, high = {AMPLITUDE, 0};

  for (long i = 0; i < count; i++) {
    samples[i * channels] = oscillate(&low, 2000);
    if (channels == 2)
      samples[i * channels + 1] = oscillate(&high, 30000);
  }
}

/* Up to near Nyquist. */
static void sweep(int16_t *samples, long count, int channels) {
  oscillator_t oscillator = {AMPLITUDE, 0};

  for (long i = 0; i < count; i++) {
    int16_t value = oscillate(&oscillator, 256 + 120000 * i / count);
    for (int c = 0; c < channels; c++)
      samples[i * channels + c] = value;
  }
}

/* Short noise bursts in silence, for pre-echo. */
static void transients(int16_t *samples, long count, int channels) {
  unsigned int seed = 3;

  silence(samples, count, channels);
  for (long i = 0; i < count; i++)
    if (i % 1000 < 64)
      for (int c = 0; c < channels; c++)
        samples[i * channels + c] = noise(&seed);
}

/* Full scale square waves, opposite phase across channels. */
static void full_scale(int16_t *samples, long count, int channels) {
  for (long i = 0; i < count; i++) {
    int16_t value = (i / 50) % 2 ? 32767 : -32768;
    samples[i * channels] = value;
    if (channels == 2)
      samples[i * channels + 1] = value == 32767 ? -32768 : 32767;
  }
}

static const signal_t signals[] = {
    {"silence", silence},       {"noise", white_noise},
    {"tones", tones},           {"sweep", sweep},
    {"transients", transients}, {"full-scale", full_scale},
};

#define SIGNAL_COUNT (int)(sizeof(signals) / sizeof(*signals))

static uint64_t fnv1a(const unsigned char *data, size_t size) {
  uint64_t hash = 0xcbf29ce484222325ULL;

  for (size_t i = 0; i < size; i++) {
    hash ^= data[i];
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

/* Expected lines, in the order configurations and signals run. */
static char **golden;
static int golden_count, golden_next;
static FILE *update;

static void load_golden(const char *path) {
  FILE *file = fopen(path, "r");
  char line[256];

  if (!file) {
    printf("FAIL: cannot read %s; run with SHINE_UPDATE_GOLDEN=1 to create "
           "it\n",
           path);
    exit(1);
  }

  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\r\n")] = 0;
    if (line[0] == '#' || line[0] == 0)
      continue;
    golden = realloc(golden, (golden_count + 1) * sizeof(*golden));
    if (!golden || !(golden[golden_count] = malloc(strlen(line) + 1))) {
      fprintf(stderr, "out of memory\n");
      exit(2);
    }
    strcpy(golden[golden_count++], line);
  }
  fclose(file);
}

static int check_golden(const shine_config_t *config, const char *name) {
  int ok = 1;

  for (int s = 0; s < SIGNAL_COUNT; s++) {
    shine_config_t copy = *config;
    stream_t stream = {NULL, 0, 0};
    shine_t encoder = shine_initialise(&copy);
    int channels = config->wave.channels;
    long count;
    int16_t *samples;
    char line[256];

    if (!encoder) {
      printf("FAIL %s: shine_initialise failed\n", name);
      return 0;
    }

    /* Not a whole number of frames, as with real input. */
    count = 10L * shine_samples_per_pass(encoder) - 37;
    samples = malloc(count * channels * sizeof(int16_t));
    if (!samples) {
      fprintf(stderr, "out of memory\n");
      exit(2);
    }
    signals[s].generate(samples, count, channels);
    encode_samples(encoder, channels, samples, count, &stream);

    snprintf(line, sizeof(line), "%s, %s: %016" PRIx64, name, signals[s].name,
             fnv1a(stream.data, stream.size));

    if (update)
      fprintf(update, "%s\n", line);
    else if (golden_next >= golden_count) {
      printf("FAIL %s, %s: missing from the golden file\n", name,
             signals[s].name);
      ok = 0;
    } else if (strcmp(line, golden[golden_next++])) {
      printf("FAIL %s\n  expected %s\n", line, golden[golden_next - 1]);
      ok = 0;
    }

    free(samples);
    stream_free(&stream);
    shine_close(encoder);
  }

  return ok;
}

int main(void) {
  const char *path = getenv("SHINE_GOLDEN");
  int status;

  if (!path) {
    printf("FAIL: SHINE_GOLDEN is not set\n");
    return 1;
  }

  if (getenv("SHINE_UPDATE_GOLDEN")) {
    update = fopen(path, "w");
    if (!update) {
      printf("FAIL: cannot write %s\n", path);
      return 1;
    }
    fprintf(update, "# Stream hashes checked by tests/golden.c. Regenerate "
                    "with SHINE_UPDATE_GOLDEN=1 make check.\n");
  } else
    load_golden(path);

  status = run_all_configs(check_golden);

  if (update) {
    if (fclose(update)) {
      printf("FAIL: cannot write %s\n", path);
      return 1;
    }
    printf("updated %s\n", path);
  } else if (golden_next != golden_count) {
    printf("FAIL: golden file has %d lines, %d were checked\n", golden_count,
           golden_next);
    status = 1;
  }

  return status;
}
