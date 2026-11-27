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

| `kbps_ch` | 8 | 16 | 24 | 28 | 32 | 40 | 48 | 56 | 64 | 80 | 96 | 112 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `f_c` (kHz) | 3 | 7 | 9.5 | 11 | 11 | 14.5 | 14.5 | 17 | 17.25 | 18 | 19 | 19.5 |

Below 8 the cutoff is that of 8. From `LOWPASS_NONE_KBPS` = 128 per
channel there is no cutoff. Where `f_c ≥ LOWPASS_NYQUIST_FRACTION · fs/2`
there is none either: the band left above it is too narrow to save bits.

A stream whose rate `fs` is at most `LOWPASS_FREE_RATE` = 32 kHz has no
cutoff: the whole band of the stream is coded, and `f_c` only chooses its
rate ([resampling.md](resampling.md) §1). That rate is the lowest one that
holds `f_c`, so the band between `f_c` and `fs/2` is a small part of the
stream, and removing it costs more on the score than its lines cost in
bits: over the corpus, coding it gains 0.8 to 3.2 points of the mean at
44.1 kHz stereo from 32 to 96 kbps, 2.3 to 4.1 at 16 and 22.05 kHz from 16
to 48 kbps, and loses 0.2 at mono 40 kbps, with no item down by more than
2.4 ([tests.md](tests.md) §4). A stream above that rate keeps its cutoff:
the band it removes is wide, and its top has no scalefactor of its own.

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
| `LOWPASS_FREE_RATE` | 32 kHz | tuning |
| `LOWPASS_PASS_FRACTION` | 0.9 | tuning |

The cutoffs follow the bandwidth that is commonly traded against bitrate
by perceptual coders. They MUST be confirmed by the metric report and by
listening ([tests.md](tests.md) §4): a cutoff that is too low is heard as
dullness, which the score grades mildly.

`LOWPASS_FREE_RATE` is set on the score alone, which rewards bandwidth,
and under 32 kbps the score is below the range it was compared with
listeners in: the full band at those bitrates MUST be confirmed by
listening before it is relied on.
