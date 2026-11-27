# Resampling

Phase 6. At a low bitrate per channel the lowpass
([bandwidth.md](bandwidth.md)) leaves the top of the spectrum empty. A
stream at a lower sample rate codes the same band with narrower lines and
scalefactor bands, shorter frames in time per bit of side information, and
no lines spent on silence. The encoder therefore chooses the rate of the
stream from the bitrate, and converts the input to it. The caller gives the
rate of its input and nothing else.

The lowest quality level does not convert: its stream is at the input's
rate.

## 1. The encoding rate

With `f_c` the cutoff of the bitrate per channel
([bandwidth.md](bandwidth.md) §1, before its Nyquist rule) and `fs_in` the
input's rate, the encoding rate `fs` MUST be the lowest MPEG rate from
`RESAMPLE_MIN_RATE` = 16 kHz up (16, 22.05, 24, 32, 44.1, 48 kHz) such that

```
f_c ≤ RESAMPLE_BAND_FRACTION · fs / 2          the stream holds the coded band
fs  ≤ RESAMPLE_MIN_RATIO · fs_in               the conversion is worth its cost
(fs, bitrate) is a valid Layer III pair
```

and `fs_in` when there is none, or when the bitrate has no cutoff. The
encoder never raises the rate.

The MPEG-2.5 rates (8, 11.025, 12 kHz) are left out: some decoders do not
play them, and at 32 kbps stereo 12 kHz scored the same as 16 kHz. An input
at one of them is encoded at its own rate.

The second condition leaves out the conversions between neighbouring rates
(48 to 44.1 kHz, 24 to 22.05 kHz, 12 to 11.025 kHz), which save few lines.

Everything else in the encoder, the lowpass and its rules included, runs
at `fs` and knows nothing of `fs_in`. A stream at 32 kHz or below is not
lowpassed ([bandwidth.md](bandwidth.md) §1): there `f_c` chooses the rate
and nothing else. For a 44.1 or 48 kHz input:

| bitrate per channel (kbps) | 16 | 20 | 24, 28 | 32 | 40, 48 | ≥ 56 |
|---|---|---|---|---|---|---|
| `fs` (kHz) | 16 | 16 | 22.05 | 24 | 32 | `fs_in` |

The rule MUST be confirmed by the metric report ([tests.md](tests.md) §4)
whenever the cutoff table changes: each rate it chooses scored at least as
well as the rates above it, **as measured** on stereo at 32, 48, 64, 80
and 96 kbps.

The input rate with the bitrate MUST itself be a valid Layer III pair, as
at the lowest quality level: a configuration is valid at every level or at
none.

## 2. The prototype filter

The converter is a polyphase FIR filter for the ratio `L / M = fs / fs_in`
in lowest terms. All its coefficients are samples of one prototype, a sinc
under a Kaiser window, in units of the output period:

```
h(x) = sinc(x) · I0(RESAMPLE_KAISER_BETA · sqrt(1 − (x / Z)²)) / I0(RESAMPLE_KAISER_BETA)     |x| < Z
     = 0                                                                                    otherwise
Z = RESAMPLE_ZEROS
```

Its response is −6 dB at the new Nyquist frequency `fs / 2`, flat below
`RESAMPLE_BAND_FRACTION · fs / 2` and in the stopband above
`(2 − RESAMPLE_BAND_FRACTION) · fs / 2`. What lies between the new Nyquist
frequency and the stopband aliases above `RESAMPLE_BAND_FRACTION · fs / 2`
only, where §1 guarantees that the lowpass codes nothing: the transition
band is twice as wide as a converter that knows nothing of the encoder
could afford, and the filter half as long.

The prototype is tabulated in Q30 at `RESAMPLE_POINTS` points per output
period, by the table generator. The library MUST NOT compute it.

## 3. The coefficients

Output sample `k` is centred at the input time `t = (k − Z) · M / L`,
`t = q + r / L` with `0 ≤ r < L`: `r` is its phase. Its coefficients are,
for `j` from `−⌊Z·M/L⌋` to `⌊Z·M/L⌋ + 1`, on input sample `q + j`:

```
c_r[j] = h((j·L − r) / M) / Σ_j h((j·L − r) / M)
```

so that every phase has unit gain at 0 Hz: a constant comes out unchanged,
and no phase-dependent gain modulates the signal.

- `h` between two points of the table MUST be the cubic through the four
  nearest points, at the fraction of the interval truncated to Q15, in
  64-bit integers.
- The coefficients are rounded to Q15, halves away from zero, and the
  largest of the phase takes the difference to a sum of exactly 2^15.
- The `L` phases are computed at initialisation. They depend on the two
  rates only.

## 4. Conversion

```
y[k] = saturate16( (Σ_j c_r[j] · x[q + j] + 2^14) >> 15 )
```

with `x` zero before the first input sample. The sum MUST be exact: the
products are summed in two halves of the taps, each in 32 bits, and the
halves in 64 bits. The absolute sum of the coefficients of each half MUST
stay below 2, which holds for every pair of MPEG rates (it is about 1.1).
The output is then bit-exact on every host and under any vectorisation
([fixed-point.md](fixed-point.md) §1).

The converter delays the signal by exactly `Z` output samples, whatever
the ratio. `N` input samples give `⌊N · L / M⌋` output samples, give or
take one, and the output MUST NOT depend on how the input is split into
calls.

## 5. Interface

- A pass (`shine_samples_per_pass`) is one frame at the input's rate, as
  without conversion: 1152 samples from 32 kHz, 576 below.
- The converted samples are queued and encoded as frames complete. A call
  returns the frames that are finished, none or several, as the bit
  reservoir already implies.
- `shine_samplerate` gives `fs`, the rate of the stream.
- `shine_encoder_delay` is in samples of the stream, and counts `Z` more
  when the encoder converts.
- `shine_flush` converts silence until the last input sample, delayed by
  the encoder and a decoder, is in a frame.
- `shine_check_config` is unchanged: it validates a rate and a bitrate as a
  stream.
- `shine_initialise_at` encodes at a rate the caller gives, at most the
  input's, instead of the rate of §1. A caller that has promised a stream
  rate passes the input's rate and gets no conversion.

## 6. Tests

For every pair of rates §1 can choose:

- **Passband.** Tones from 0.2 % to 92 % of the new Nyquist frequency come
  out within `PASSBAND_RIPPLE` = 0.01 dB of their level, with everything
  else `PASSBAND_DISTORTION` = 70 dB below them.
- **Aliasing.** Tones from 108 % of the new Nyquist frequency to the old
  one come out at least `STOPBAND` = 65 dB lower.
- **Delay and headroom.** An impulse peaks `Z` output samples after its
  place; the absolute sum of the coefficients met by one output sample is
  below 4; a constant comes out bit-exact.
- **Streaming.** Passes of one size and of random sizes give the same
  samples.

Through the public interface: the rate chosen for a list of
configurations, the pass, the delay, frames at `fs` that hold the whole
input, and the level of tones decoded by FFmpeg at the reported delay. The
golden hashes pin the streams.

## 7. Parameters

| Name | Value | Kind |
|---|---|---|
| `RESAMPLE_BAND_FRACTION` | 0.92 | tuning, tied to the filter |
| `RESAMPLE_MIN_RATIO` | 0.8 | tuning |
| `RESAMPLE_MIN_RATE` | 16 kHz | compatibility |
| `RESAMPLE_ZEROS` | 32 | filter |
| `RESAMPLE_KAISER_BETA` | 8.0 | filter |
| `RESAMPLE_POINTS` | 32 | table |
| coefficient format | Q15, 16 bits | fixed point |

`RESAMPLE_ZEROS` and `RESAMPLE_KAISER_BETA` give a design rejection of
about 80 dB for a transition of ±8 % around the new Nyquist frequency.
The 16-bit coefficients, which let the sum run in 16-bit SIMD lanes, bound
the measured rejection to 74 to 77 dB for the ratios of a 44.1 or 48 kHz
input down to 22.05 kHz, and 66 dB at the largest ratio (48 to 8 kHz).
`RESAMPLE_BAND_FRACTION` MUST NOT exceed the passband of the prototype.
