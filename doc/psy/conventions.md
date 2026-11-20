# Conventions and shared formulas

## Requirement words

MUST, SHOULD and MAY are used as in RFC 2119. A named parameter in
`UPPER_CASE` is defined in [parameters.md](parameters.md) with its recommended
value. A parameter marked **tuning** may be changed with a metric report
([tests.md](tests.md) §4); one marked **essential** is part of the model's
structure.

## Units and scale

- **Samples** are on a full-scale convention: full scale (FS) is the largest
  magnitude the input format represents, ±32768 for 16-bit PCM. Every
  level-dependent constant is stated in dBFS or as a fraction of FS, never in
  the units of one sample format.
- **Energy unit.** Line energies of every transform are normalised so that a
  sine of amplitude FS has a total line energy of 1 in one analysis block
  (a granule for long spectra, a window for short ones), averaged over its
  phase. A level is then a power, whatever the block length. The normalisation constant of each transform is
  a generated constant ([fixed-point.md](fixed-point.md) §3). Energies,
  thresholds and the threshold of hearing are all in this unit, so they
  compare directly whatever the transform.
- **dB** converts to an energy ratio as `10^(dB/10)` and to an amplitude
  ratio as `10^(dB/20)`.
- `log10`, `log2` and `ln` are the mathematical functions. `int(x)` truncates
  toward zero. `/` between non-negative integers is integer division.

## Frames, granules, channels

- A frame holds `mode_gr` granules: 2 for MPEG-1 (32, 44.1, 48 kHz), 1 for
  MPEG-2 and 2.5 (≤ 24 kHz). A granule is 576 samples per channel.
- `fs` is the output sample rate in Hz.
- **Coded channels** are 0 (left or mono) and 1 (right).
- **Analysis channels** are 0 = left (or mono), 1 = right, 2 = mid,
  3 = side. Channels 2 and 3 exist only when the stream may use M/S coding
  ([stereo.md](stereo.md)). `n_psy` is the number of analysis channels.
  `ch01(c) = c & 1` is the coded channel whose block type analysis channel
  `c` follows: mid follows left, side follows right.
- Mid and side are always `M = (L + R)/√2`, `S = (L − R)/√2`, in every part
  of the model, so that L/R and M/S quantities share one level scale.

## Scalefactor band tables

The scalefactor band (sfb) boundaries are those of ISO 11172-3 / 13818-3 for
`fs`, in MDCT lines: `sfb_l[0..22]` over 576 lines and `sfb_s[0..13]` over 192
lines. There are 22 long bands, where index 21 is the band above the last coded
scalefactor ("sfb21"), and 13 short bands, where index 12 is "sfb12".

## Bark scale

```
bark(f) = 13·atan(0.76·F) + 3.5·atan((F/7.5)²),   F = max(f, 0) / 1000
```

`f` in Hz. Zwicker's approximation.

## Threshold of hearing curve

`ATH(f)` in dB SPL, `f` in Hz, curve parameter `c`:

```
F = clamp(f / 1000, 0.1, 24.0)                 (kHz)
ATH(f) = 3.640·F^-0.8
       − 6.800·exp(−0.6·(F − 3.4)²)
       + 6.000·exp(−0.15·(F − 8.7)²)
       + (0.6 + 0.04·c)·0.001·F⁴
```

This is the Painter & Spanias curve with an adjustable F⁴ term. Increasing `c`
raises the high-frequency part of the curve. `c = ATH_CURVE` from the
per-bitrate row ([parameters.md](parameters.md) §1). `ATH_min` is the minimum
of the curve over `f`, near 3.4 kHz, computed by the table generator. How the
curve is placed on the energy scale is in [ath.md](ath.md) §1.

## Spreading function

`s3(d)` is the energy factor that a masker spreads to a maskee `d` Bark above
it (`d = bark(maskee) − bark(masker)`):

```
t = 3·d          if d ≥ 0
t = 1.5·d        if d < 0
x = 8·((t − 0.5)² − 2·(t − 0.5))   if 0.5 ≤ t ≤ 2.5,  else 0
t = t + 0.474
y = 15.811389 + 7.5·t − 17.5·sqrt(1 + t²)
s3(d) = 0                          if y ≤ −60
s3(d) = 10^((x + y)/10) / 0.6609193
```

The `y` term is the Schroeder spreading function. The `x` term is the dip of
ISO 11172-3 psychoacoustic model 2. The Bark argument is scaled by 3 upward and by 1.5 downward, so masking
spreads half as far, in Bark, toward higher frequencies as toward lower ones.
The divisor normalises the integral over Bark to 1.

## Stereo demasking factor

```
stereo_demask(f) = 10^(1.25·(1 − cos(π·m)) − 2.5),   m = min(bark(f), 15.5) / 15.5
```

It rises from −25 dB at 0 Bark to 0 dB at 15.5 Bark and above. It is a fit of
the binaural masking level difference given by Johnston & Ferreira (ICASSP
1992).

## Interpolation in the log domain

```
interp(x, y, r) = x^r · y^(1−r)     if x > 0 and y > 0
                = 0                  otherwise
```

for `0 ≤ r ≤ 1`. The model only uses `r` in `(0, 1)`. In the log domain this is
the linear blend `r·log x + (1 − r)·log y`.

## Masking tables `tab` and `delta`

Indexed by the tonality index `k` in 0..8 ([masking.md](masking.md) §1).
**Tuning.**

| k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| `tab[k]` | 1.0 | 0.79433 | 0.63096 | 0.63096 | 0.63096 | 0.63096 | 0.63096 | 0.25119 | 0.11749 |
| dB | 0 | −1 | −2 | −2 | −2 | −2 | −2 | −6 | −9.3 |
| `delta[k]` | 2 | 2 | 2 | 1 | 1 | 1 | 0 | 0 | −1 |

`tab` goes from 0 dB for a noise-like masker to −9.3 dB for a tonal one. A
tonal masker masks noise less than a noise-like masker of the same energy.
`delta[k]` is the distance, in partitions, over which neighbouring maskers
add with a boost ([masking.md](masking.md) §2).
