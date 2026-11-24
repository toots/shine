# Bandwidth

Phase 5. At a low bitrate per channel, the bits that code the top of the
spectrum are taken from the bands below it. Above a cutoff that falls with
the bitrate, the content is quieter, more masked, and barely audible to
many listeners, while every line coded there costs as much as one below.
The encoder MUST NOT code the spectrum above the cutoff at the levels of
the model that run it, and SHOULD lower the lines below it smoothly.

## 1. The cutoff

With `kbps_ch` the bitrate divided by the number of channels (a joint or
dual stereo stream counts two), the cutoff `f_c` is interpolated linearly
in `kbps_ch` between the rows of this table, **tuning**:

| `kbps_ch` | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 80 | 96 | 112 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `f_c` (kHz) | 3 | 5.5 | 8 | 11 | 13 | 14.5 | 15.5 | 16.5 | 18 | 19 | 19.5 |

Below 8 the cutoff is that of 8. From `LOWPASS_NONE_KBPS` = 128 per
channel there is no cutoff. Where `f_c ≥ LOWPASS_NYQUIST_FRACTION · fs/2`
there is none either: the band left above it is too narrow to save bits.

## 2. The filter

The filter acts on the MDCT coefficients of every granule, long and short,
of every coded and analysis spectrum, before the model
([signal-path.md](signal-path.md) §1). The model and the allocation then
see the spectrum that is coded.

A line of centre frequency `f` (long lines `(k + 0.5)·fs/1152`, short
lines `(k + 0.5)·fs/384`) is multiplied by

```
g(f) = 1                                  f ≤ f_p
     = (f_c − f) / (f_c − f_p)            f_p < f < f_c
     = 0                                  f ≥ f_c
f_p = LOWPASS_PASS_FRACTION · f_c
```

The taper lowers the lines next to the cutoff rather than leaving a step
that a decoder's IMDCT would turn into ringing. Gains are fixed per
stream, in Q15, computed at initialisation.

The lowest quality level does not filter.

## 3. Parameters

| Name | Value | Kind |
|---|---|---|
| cutoff table | §1 | tuning |
| `LOWPASS_NONE_KBPS` | 128 | tuning |
| `LOWPASS_NYQUIST_FRACTION` | 0.95 | tuning |
| `LOWPASS_PASS_FRACTION` | 0.9 | tuning |

The cutoffs follow the bandwidth that is commonly traded against bitrate
by perceptual coders. They MUST be confirmed by the metric report and by
listening ([tests.md](tests.md) §4): a cutoff that is too low is heard as
dullness, which the score grades mildly.
