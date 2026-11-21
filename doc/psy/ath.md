# Absolute threshold of hearing

Phase 1 (partition ATH, adaptive ATH), phase 2 (band ATH in the allowed
distortion), phase 4 (short layout).

The curve `ATH(f)` is defined in [conventions.md](conventions.md). This file
places it on the energy scale, gives it per partition and per band, and
adapts it to programme loudness.

## 1. Placement

The curve is in dB SPL. Placing it on the digital scale needs an assumed
playback level: `ATH_FS_SPL` is the SPL of a sine at full scale. A sine at
the threshold of hearing at frequency `f` then has energy

```
ath_line(f) = 10^((ATH(f) − ATH_FS_SPL)/10) · ATH_OFFSET
```

in the energy unit. `ATH_OFFSET` is the per-bitrate offset
([parameters.md](parameters.md) §1). `ath_floor = ath_line` at the curve's
minimum `ATH_min`.

Since the energy unit is a power, this placement is the same for every
transform and block length. The level is **tuning**. Its meaning is
essential: noise at the threshold is inaudible when full scale plays at
`ATH_FS_SPL`.

## 2. Per partition and per band

```
ath_part[b] = min over the lines k of partition b of ath_line(f_k)
ath_band[sb] = min over the lines k of band sb of ath_line(f_k)
```

`f_k` is the centre frequency of line `k` in its layout
([spectral-analysis.md](spectral-analysis.md) §1–§2). The same rule holds for
long and short layouts.

There is no factor for the number of lines. Noise narrower than a critical
band is detected by its total power, like a tone. Partitions and sfbs are not
wider than a critical band except at the top of the spectrum, where the curve
is steep and the minimum is set by the band's lowest line.

## 3. Adapted ATH

Wherever the ATH is used, it is first adapted with the frame's
`adjust_factor` `a` (§4). With `x` a partition or band ATH:

```
w = max(0, 1 + 10·log10(a²) / ATH_ADAPT_RANGE)     if a > 0,  else 0
athAdjust(a, x) = ath_floor · (x / ath_floor)^w · 10^(ATH_ADAPT_GAIN/10)
```

`a = 1` keeps the curve, shifted by `ATH_ADAPT_GAIN`. Smaller `a` compresses
the curve toward its floor: bands far above the minimum come down more than
bands near it. `ATH_ADAPT_RANGE` is the attenuation at which the curve is flat at its
floor. Its value is the dynamic range of 16-bit PCM, `20·log10(32768)`, and
it does not depend on the input format.

Users: the M/S correction ([stereo.md](stereo.md) §2) with `ath_part`, the
allowed distortion ([allowed-distortion.md](allowed-distortion.md) §2) with
`ath_band`.

## 4. Adaptive ATH

The adaptation lowers the ATH during quiet passages, on the assumption that the
listener turns the volume up. It is an assumption about playback level.
With adaptation off, the encoder works against a threshold that assumes only
`ATH_FS_SPL`. The adaptation MUST be a parameter that a quality level can
turn off. It is on by default.

### 4.1 Loudness

Per granule, coded channels only, from the long line energies:

```
loudness_sq = K_loud · Σ_k E[k] · eql_w[k]
eql_w[k] = 10^(−ATH(f_k)/10) / Σ_{k'} 10^(−ATH(f_k')/10)
```

The weights follow the inverse ATH as an equal-loudness approximation, and
sum to 1. Each line's weight is evaluated at that line's own centre
frequency. `K_loud` is a generated constant: full-scale binary white noise
(independent samples of ±FS) gives `loudness_sq = LOUDNESS_FS_NOISE`. The
limits in §4.2 are relative to that reference, so they do not depend on the
sample format.

### 4.2 Update, once per frame

The frame's loudness is known before any of its granules is modelled
([signal-path.md](signal-path.md) §1). State: `adjust_factor`, initially
0.01, and `adjust_limit`, initially 1.0. A stream's start is treated as
quiet.

```
P_g = loudness_sq[g][0] + (stereo ? loudness_sq[g][1] : loudness_sq[g][0])
P = 0.5 · max_g P_g

if P > 0.03125:
    if adjust_factor ≥ 1: adjust_factor = 1
    else if adjust_factor < adjust_limit: adjust_factor = adjust_limit
    adjust_limit = 1
else:
    L = 31.98·P + 0.000625
    if adjust_factor ≥ L:
        adjust_factor *= 0.075·L + 0.925
        if adjust_factor < L: adjust_factor = L
    else if adjust_limit ≥ L:
        adjust_factor = L
    else if adjust_factor < adjust_limit:
        adjust_factor = adjust_limit
    adjust_limit = L
```

Loud passages restore the full ATH within one frame. Quiet passages lower it
gradually. `adjust_factor` stays within `[0.000625, 1]`. That bounds the
adaptation to about 32 dB. The updated value applies to every use in the
same frame.

`adjust_factor` is a linear factor. It enters the ATH only through
`athAdjust` (§3). No interface carries it in dB.
