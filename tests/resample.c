/* The sample rate converter and the encoding rate rule
 * (doc/psy/resampling.md §6): passband level, rejection above the new
 * Nyquist frequency, the delay, the headroom of the coefficients, and an
 * output that does not depend on how the input is split into calls. */

#include "resample.h"
#include "helpers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define SECONDS 2
#define MAX_PASS 1152
/* §6: tolerances in dB. */
#define PASSBAND_RIPPLE 0.01
#define PASSBAND_DISTORTION -70.0
#define STOPBAND -65.0

static const int rates[9] = {8000,  11025, 12000, 16000, 22050,
                             24000, 32000, 44100, 48000};

/* Converts `count` samples in passes of `pass` samples, or of pseudo-random
 * sizes for 0. Returns the output count. */
static int convert(int input_rate, int output_rate, const int16_t *input,
                   int count, int pass, int16_t *output) {
  shine_resampler_t *resampler =
      shine_resampler_open(input_rate, output_rate, 1);
  unsigned int seed = 7;
  int written = 0;

  if (!resampler)
    exit(1);
  for (int done = 0; done < count;) {
    int size = pass ? pass : 1 + (noise(&seed) & 1023);
    const int16_t *in = input + done;
    int16_t *out = output + written;

    if (size > count - done)
      size = count - done;
    written += shine_resample(resampler, &in, 1, size, &out);
    done += size;
  }
  shine_resampler_close(resampler);
  return written;
}

/* Level in dB of `samples` against a tone of amplitude `amplitude`, and of
 * what is left once the tone at `frequency` cycles per sample is removed. */
static void tone_levels(const int16_t *samples, int count, double frequency,
                        double amplitude, double *level, double *residual) {
  double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, a, b, rest = 0, det;

  for (int i = 0; i < count; i++) {
    double s = sin(2 * PI * frequency * i), c = cos(2 * PI * frequency * i);
    ss += s * s;
    cc += c * c;
    sc += s * c;
    ys += samples[i] * s;
    yc += samples[i] * c;
  }
  det = ss * cc - sc * sc;
  a = (ys * cc - yc * sc) / det;
  b = (yc * ss - ys * sc) / det;
  for (int i = 0; i < count; i++) {
    double error = samples[i] - a * sin(2 * PI * frequency * i) -
                   b * cos(2 * PI * frequency * i);
    rest += error * error;
  }
  *level = 10 * log10((a * a + b * b) / (amplitude * amplitude) + 1e-30);
  *residual = 10 * log10(2 * rest / count / (amplitude * amplitude) + 1e-30);
}

/* Tones across the passband come out at their level, and tones that would
 * alias below RESAMPLE_BAND_FRACTION of the new Nyquist frequency are
 * rejected. */
static int check_response(int input_rate, int output_rate) {
  const int count = SECONDS * input_rate, skip = 4 * RESAMPLE_ZEROS;
  const double amplitude = 16000;
  int16_t *input = malloc(count * sizeof(int16_t));
  int16_t *output = malloc((count + MAX_PASS) * sizeof(int16_t));
  double ripple = 0, distortion = -200, leak = -200;
  int ok;

  /* Passband: 0.2% to 92% of the output Nyquist frequency. */
  for (int step = 0; step <= 46; step++) {
    double frequency = (step ? step * 0.01 : 0.001) * output_rate, level, rest;
    int written;

    for (int i = 0; i < count; i++)
      input[i] =
          (int16_t)lround(amplitude * sin(2 * PI * frequency * i / input_rate));
    written = convert(input_rate, output_rate, input, count, MAX_PASS, output);
    tone_levels(output + skip, written - 2 * skip, frequency / output_rate,
                amplitude, &level, &rest);
    if (fabs(level) > ripple)
      ripple = fabs(level);
    if (rest > distortion)
      distortion = rest;
  }
  /* Stopband: from the frequency that aliases onto the passband edge. */
  for (int step = 54; step * output_rate < 50 * input_rate; step++) {
    double frequency = step * 0.01 * output_rate, level, rest, total;
    int written;

    for (int i = 0; i < count; i++)
      input[i] =
          (int16_t)lround(amplitude * sin(2 * PI * frequency * i / input_rate));
    written = convert(input_rate, output_rate, input, count, MAX_PASS, output);
    tone_levels(output + skip, written - 2 * skip, 0.25, amplitude, &level,
                &rest);
    total = 10 * log10(pow(10, level / 10) + pow(10, rest / 10));
    if (total > leak)
      leak = total;
  }
  ok = ripple <= PASSBAND_RIPPLE && distortion <= PASSBAND_DISTORTION &&
       leak <= STOPBAND;
  printf("%s %d to %d Hz: passband within %.4f dB, distortion %.1f dB, "
         "stopband %.1f dB\n",
         ok ? "ok" : "FAIL", input_rate, output_rate, ripple, distortion, leak);
  free(input);
  free(output);
  return ok;
}

/* An impulse comes out as the coefficients: its peak is RESAMPLE_ZEROS
 * output samples late, and the absolute sum of the coefficients of an output
 * sample stays below 4, twice the headroom of each of the two 32-bit sums.
 * A constant comes out unchanged. */
static int check_coefficients(int input_rate, int output_rate) {
  enum { INPUT = 6000, WINDOW = 800 };
  static int16_t input[INPUT], output[INPUT + MAX_PASS];
  static int32_t sums[INPUT + MAX_PASS];
  int written = 0, worst = 0, ok = 1;

  memset(sums, 0, sizeof(sums));
  for (int position = 0; position < WINDOW; position++) {
    int peak = 0;

    memset(input, 0, sizeof(input));
    input[position] = 16384;
    written = convert(input_rate, output_rate, input, INPUT, MAX_PASS, output);
    for (int k = 0; k < written; k++) {
      sums[k] += abs(output[k]);
      if (output[k] > output[peak])
        peak = k;
    }
    /* The output sample nearest to the impulse, RESAMPLE_ZEROS later. */
    if (labs(2L * (peak - RESAMPLE_ZEROS) * input_rate -
             2L * position * output_rate) > input_rate) {
      printf("FAIL %d to %d Hz: impulse at %d peaks at %d\n", input_rate,
             output_rate, position, peak);
      ok = 0;
      break;
    }
  }
  /* Output samples whose whole filter lies on the impulses. */
  for (long k = 2 * RESAMPLE_ZEROS + 1;
       (k + 1) * input_rate < (long)WINDOW * output_rate; k++)
    if (sums[k] > worst)
      worst = sums[k];
  if (worst >= 4 * 16384) {
    printf("FAIL %d to %d Hz: coefficients sum to %d / 16384\n", input_rate,
           output_rate, worst);
    ok = 0;
  }

  for (int i = 0; i < INPUT; i++)
    input[i] = -12345;
  written = convert(input_rate, output_rate, input, INPUT, MAX_PASS, output);
  for (int k = 2 * RESAMPLE_ZEROS + 1; k < written; k++)
    if (output[k] != -12345) {
      printf("FAIL %d to %d Hz: a constant comes out as %d\n", input_rate,
             output_rate, output[k]);
      ok = 0;
      break;
    }
  if (ok)
    printf("ok %d to %d Hz: delay %d, coefficients sum to %.3f\n", input_rate,
           output_rate, RESAMPLE_ZEROS, worst / 16384.0);
  return ok;
}

/* The same output whatever the sizes of the passes, and as many samples as
 * the ratio gives. */
static int check_streaming(int input_rate, int output_rate) {
  const int count = 3 * input_rate / 4;
  int16_t *input = malloc(count * sizeof(int16_t));
  int16_t *whole = malloc((count + MAX_PASS) * sizeof(int16_t));
  int16_t *split = malloc((count + MAX_PASS) * sizeof(int16_t));
  unsigned int seed = 3;
  int written, ok = 1;
  long expected = (long)count * output_rate / input_rate;

  for (int i = 0; i < count; i++)
    input[i] = noise(&seed);
  written = convert(input_rate, output_rate, input, count, MAX_PASS, whole);
  if (labs(written - expected) > 1) {
    printf("FAIL %d to %d Hz: %d samples out for %d in\n", input_rate,
           output_rate, written, count);
    ok = 0;
  }
  for (int pass = 0; pass <= 1; pass++)
    if (convert(input_rate, output_rate, input, count, pass, split) !=
            written ||
        memcmp(whole, split, written * sizeof(int16_t))) {
      printf("FAIL %d to %d Hz: output depends on the passes\n", input_rate,
             output_rate);
      ok = 0;
    }
  free(input);
  free(whole);
  free(split);
  return ok;
}

typedef struct {
  int samplerate, channels, bitrate, quality, expected;
} rate_case_t;

/* §1 through the public interface: the encoding rate, a pass of one frame
 * at the input rate, the delay, and frames at the encoding rate holding the
 * whole input. */
static int check_rate(const rate_case_t *test) {
  shine_config_t config;
  shine_t encoder;
  stream_t stream = {NULL, 0, 0};
  int pass, delay, expected_delay, frames, frame_samples, input, ok = 0;
  char name[64];

  snprintf(name, sizeof(name), "%d Hz %d ch %d kbps quality %d",
           test->samplerate, test->channels, test->bitrate, test->quality);
  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.samplerate = test->samplerate;
  config.wave.channels = test->channels;
  config.mpeg.bitrate = test->bitrate;
  config.mpeg.mode = test->channels == 1 ? SHINE_MONO : SHINE_JOINT_STEREO;
  config.mpeg.quality = test->quality;
  encoder = shine_initialise(&config);
  if (!encoder) {
    printf("FAIL %s: shine_initialise failed\n", name);
    return 0;
  }

  pass = shine_samples_per_pass(encoder);
  delay = shine_encoder_delay(encoder);
  expected_delay = 528 + (test->quality ? 256 : 0) +
                   (test->expected != test->samplerate ? RESAMPLE_ZEROS : 0);
  if (shine_samplerate(encoder) != test->expected) {
    printf("FAIL %s: encoded at %d Hz, expected %d\n", name,
           shine_samplerate(encoder), test->expected);
    goto end;
  }
  if (pass != (test->samplerate >= 32000 ? 1152 : 576)) {
    printf("FAIL %s: %d samples per pass\n", name, pass);
    goto end;
  }
  if (delay != expected_delay) {
    printf("FAIL %s: delay %d, expected %d\n", name, delay, expected_delay);
    goto end;
  }

  input = 40 * pass - 100;
  encode_noise(encoder, test->channels, input, &stream);
  config.wave.samplerate = test->expected;
  frames = walk_frames(&stream, &config, name);
  if (frames < 0)
    goto end;
  frame_samples = test->expected >= 32000 ? 1152 : 576;
  if ((long)frames * frame_samples <
      (long)input * test->expected / test->samplerate + delay +
          MP3_DECODER_DELAY) {
    printf("FAIL %s: %d frames cannot hold the input and the delay\n", name,
           frames);
    goto end;
  }
  ok = 1;

end:
  stream_free(&stream);
  shine_close(encoder);
  return ok;
}

int main(void) {
  static const rate_case_t cases[] = {
      {44100, 2, 64, 0, 44100},
      {44100, 2, 64, 1, 24000},
      {44100, 2, 80, 1, 32000},
      {44100, 2, 96, 1, 32000},
      {44100, 2, 112, 1, 44100},
      {44100, 2, 128, 1, 44100},
      {44100, 1, 32, 1, 24000},
      {44100, 1, 48, 1, 32000},
      {44100, 1, 64, 1, 44100},
      {48000, 2, 64, 1, 24000},
      {48000, 2, 96, 1, 32000},
      {48000, 2, 128, 1, 48000},
      {32000, 2, 64, 1, 24000},
      {32000, 2, 96, 1, 32000},
      /* RESAMPLE_MIN_RATE: never an MPEG-2.5 rate below the input's. */
      {22050, 2, 64, 1, 22050},
      {24000, 1, 16, 1, 16000},
      {16000, 1, 8, 1, 16000},
      {8000, 1, 8, 1, 8000},
      {44100, 2, 32, 1, 16000},
  };
  const int count = sizeof(cases) / sizeof(*cases);
  int checked = 0, passed = 0;

  for (int in = 0; in < 9; in++)
    for (int out = 0; out < in; out++) {
      /* RESAMPLE_MIN_RATIO: the pairs the rule can choose. */
      if (5 * rates[out] > 4 * rates[in])
        continue;
      checked += 3;
      passed += check_response(rates[in], rates[out]);
      passed += check_coefficients(rates[in], rates[out]);
      passed += check_streaming(rates[in], rates[out]);
    }
  for (int i = 0; i < count; i++) {
    checked++;
    passed += check_rate(&cases[i]);
  }

  /* shine_initialise_at keeps the rate a caller has promised. */
  {
    shine_config_t config;
    shine_t s;

    shine_set_config_mpeg_defaults(&config.mpeg);
    config.wave.samplerate = 44100;
    config.wave.channels = 2;
    config.mpeg.mode = SHINE_JOINT_STEREO;
    config.mpeg.bitrate = 64;
    config.mpeg.quality = 1;
    checked++;
    s = shine_initialise_at(&config, 44100);
    if (s && shine_samplerate(s) == 44100 && shine_samples_per_pass(s) == 1152)
      passed++;
    else
      printf("FAIL shine_initialise_at(44100) at quality 1 and 64 kbps\n");
    if (s)
      shine_close(s);
  }

  printf("%d/%d checks passed\n", passed, checked);
  return checked >= 3 * 20 + count + 1 && passed == checked ? 0 : 1;
}
