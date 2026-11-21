# From model output to allowed distortion per band

Phase 2, except temporal masking between short windows (§3), which is
phase 4.

This is the boundary with the noise allocation loop. For each granule and
coded channel, in the stereo mode chosen for the frame, the loop gets the
model's band energies `en_psy` and thresholds `thm_psy`, and the channel's
MDCT coefficients `xr`. It derives the allowed distortion `xmin` per band.
The loop itself is [noise-allocation.md](noise-allocation.md).

The thresholds are not absolute physical quantities. Their level is set by
offsets tuned together with the allocator (`MASK_ADJUST`, the band offsets of
§1). A correct model can still sound wrong against an allocator it was not
tuned with, so model and allocator MUST be validated together on the metric
([tests.md](tests.md) §4).

## 1. Band offsets

Fixed offsets per band region, **tuning**:

| Region | Long sfbs | Long offset | Short sfbs | Short offset |
|---|---|---|---|---|
| bass | 0–6 | −0.5 dB | 0–2 | −2.0 dB |
| alto | 7–13 | −0.25 dB | 3–6 | −1.0 dB |
| treble | 14–20 | −0.025 dB | 7–11 | −0.05 dB |
| sfb21 / sfb12 | 21 | +0.5 dB | 12 | +0.5 dB |

`fact[sb]` is the matching `10^(dB/10)`.

## 2. Allowed distortion

Bands covered: long sfbs `0..20` (`0..16` at fs ≤ 8 kHz), and short sfbs
`0..11` (`0..8` at fs ≤ 8 kHz) for each of the 3 windows. sfb21 and sfb12
have no scalefactor and get no allowed distortion. START and STOP granules
are long.

Per band (one window for short), with `width` MDCT lines and the adapted
band ATH `A = athAdjust(adjust_factor, ath_band[sb]) · fact[sb]`
([ath.md](ath.md) §2–§3), in the energy unit:

```
en0 = K · Σ xr²        (the band's MDCT energy; K = K_long, or K_short per window)
x = min(en0, A)
if en_psy > 0:
    x = max(x, en0 · thm_psy / en_psy · fact[sb])
xmin = max(x, XMIN_FLOOR)
```

- The ATH part allows at most the ATH, and never more than the band's own
  energy.
- The masking part transfers the model's threshold-to-energy ratio onto the
  coded energy. On the MDCT analysis path `en_psy = en0`, so the masking part
  is `thm_psy · fact[sb]`. On the FFT path
  ([spectral-analysis.md](spectral-analysis.md) §6) the ratio carries the
  threshold across transforms.
- `XMIN_FLOOR` keeps `xmin > 0`. It is a level relative to full scale, below
  any reachable ATH, so it decides nothing on real input.

The loop also gets, per granule, the number of bands with `en0 > A`, and per
band whether `en0 > xmin`.

## 3. Temporal masking between short windows (phase 4)

After the three windows of a short sfb are computed:

```
decay = 10^(−192 / (TEMPORAL_SUSTAIN · fs))
if xmin[0] > xmin[1]: xmin[1] += (xmin[0] − xmin[1]) · decay
if xmin[1] > xmin[2]: xmin[2] += (xmin[1] − xmin[2]) · decay
```

A loud window's allowed noise carries forward into the next window, falling
by 10 dB per `TEMPORAL_SUSTAIN`. `decay` is about 0.367 per 192-sample window at
44.1 kHz. Only forward masking within one granule's three short windows is
modelled: there is none across granules, and none for long blocks.

It pushes in the opposite direction to the short pre-echo control
([block-switching.md](block-switching.md) §4) across the same window
boundaries. The pre-echo control acts on thresholds before this step, so
after a click, the allowed noise of the windows that follow decays from the
click window's value and never exceeds it.
