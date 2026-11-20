# Perceptual entropy

PE estimates how many bits a granule needs. The formula is phase 1, since
the M/S decision uses it ([stereo.md](stereo.md) §3). The smoothing (§3) is
phase 3, with the reservoir it feeds.

## 1. Which formula

PE is computed from the band energies and thresholds of the granule
([signal-path.md](signal-path.md) §3.2) and its final block type:

- coded channel `c`: the short formula if `block_type[c] = SHORT`, otherwise
  the long one;
- the M/S pair: the short formula if the coded channels are SHORT, otherwise
  the long one.

The thresholds already carry the masking adjustment
([masking.md](masking.md) §3.4). PE does not apply it again.

## 2. Formulas

Long:
```
pe = 1124.23 / 4
for sb = 0..20:
    if thm.l[sb] > 0 and en.l[sb] > thm.l[sb]:
        pe += C_l[sb] · min(log10(en.l[sb] / thm.l[sb]), log10(PE_RATIO_CAP))
```

Short: base `1236.28 / 4`, the sum over sfbs 0..11 and the three windows,
with `C_s`.

| sb | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `C_l` | 6.8 | 5.8 | 5.8 | 6.4 | 6.5 | 9.9 | 12.1 | 14.4 | 15 | 18.9 | 21.6 | 26.9 | 34.2 | 40.2 | 46.8 | 56.5 | 60.7 | 73.9 | 85.7 | 93.4 | 126.1 |
| `C_s` | 11.8 | 13.6 | 17.2 | 32 | 46.5 | 51.3 | 57.5 | 67.1 | 71.5 | 84.6 | 97.6 | 130 | | | | | | | | | |

A band contributes at most `10·C` at the cap. PE is continuous in the band
ratio. sfb21 and sfb12 have no scalefactor and do not contribute.

The coefficients were fitted at 44.1 kHz, and they are used at every sample
rate. Whether they fit the other rates is a perceptual judgement
([tests.md](tests.md) §4). PE depends only on energy ratios, so it does not
depend on the energy unit or the transform.

## 3. Smoothing (phase 3)

Per frame, before allocation. `PEsum` is the sum of the PE in use, L/R or M/S
([stereo.md](stereo.md) §3), over the frame's granules and channels. `B` is
a 19-entry history, initially all `700 · mode_gr · nch`:

```
shift B left by one;  B[18] = PEsum
F = B[9] + Σ_{i=0..8} k[i] · (B[i] + B[18−i])
factor = 670 · 5 · mode_gr · nch / F      if F > 0
       = 1                                otherwise
every pe in use *= factor
```

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| `k[i]/5` | −0.0207887 | −0.0378413 | −0.0432472 | −0.031183 | 0 | 0.0467745 | 0.10091 | 0.151365 | 0.187098 |

The allocator receives PE relative to a weighted average of the last 19
frames, centred 9 frames back. The filter has negative taps, so `F ≤ 0` is
possible on bursty input: PE high at both ends of the window and low in its
middle. The factor is then 1. Bits derived from a smoothed PE MUST be clamped
to the allocator's granule and reservoir limits (phase 3) before any integer
conversion.
