# Masking thresholds per partition

Long and short windows share one procedure (§1–§3). Their differences are in §4. Inputs per
analysis channel: `eb`, `max`, `max_t`, `avg_t` per partition
([spectral-analysis.md](spectral-analysis.md) §3).

Every quantity in this file MUST be finite and ≥ 0 for any input, including
digital silence, and every rule below says what happens when an operand is 0.

## 1. Tonality index

For each partition `b`, over its neighbourhood `N(b) = {b−1, b, b+1}` clipped
to `[0, npart−1]`, with `n = |N(b)|`:

```
a = Σ_{p∈N(b)} avg_t[p]
if a > 0:
    m = max_{p∈N(b)} max_t[p]
    L = Σ_{p∈N(b)} numlines[p] − 1
    idx[b] = min(int(TONALITY_SCALE·(n·m − a) / (a·L)), 8)     if L > 0
           = 0                                                if L = 0
else:
    idx[b] = 0
```

`n·m ≥ a`, so the index is ≥ 0. A flat spectrum gives 0; one dominant line
gives a large index. A high index means a tonal masker, which masks noise less.
Silence is noise-like (0). A neighbourhood of one line carries no
peak-to-average information and is noise-like.

The index reads one realisation of the spectrum, not its expectation. On
white noise, MDCT line energies spread widely, and a neighbourhood of a few
lines reads as tonal: three one-line partitions give an index near 8. The
mean index falls as partitions widen. Whether `TONALITY_SCALE` and the
smoothing of [spectral-analysis.md](spectral-analysis.md) §1.2 handle this
well enough is a perceptual judgement ([tests.md](tests.md) §4): on the
score, a scale of 15 is the best of 10, 12, 13, 15, 17, 20, 26 and 34, by
0.4 on the mean of the held-out sources over the value of 20 it replaces.

## 2. Spreading

For maskee partition `b`, with its spreading row `[lo, hi]`
([spectral-analysis.md](spectral-analysis.md) §4):

```
ecb = Σ_{p=lo..hi} S[b][p] · eb[p] · tab[idx[p]]
k_avg = (1 + 2·Σ_{p=lo..hi} idx[p]) / (2·(hi − lo + 1))      integer division, rounded mean
avg_mask = tab[k_avg] · AVG_MASK_FACTOR
ecb = ecb · avg_mask
```

Each masker contribution is attenuated by its own tonality. The sum is then
attenuated by the mean tonality over the row, and by a further 3 dB.

Contributions add as energies. The sum does not depend on the order of its
terms, so it MAY be computed in any order, or as a product of the spreading
matrix with the vector of attenuated energies. A non-linear law, boosting
close maskers of similar level by up to 3 dB and keeping only the louder of
two distant ones, scores lower by the measure of [tests.md](tests.md) §4
from 96 kbps up. The lower thresholds of the plain sum at low bitrates are
what `MASK_ADJUST` compensates ([parameters.md](parameters.md) §1).

## 3. Threshold per partition

The limits apply in the order of this section. The order is part of the
definition: each step is not commutative with the next.

### 3.1 Pre-echo limit (long windows)

A sudden onset inside a long window must not raise the allowed noise above
what the previous granules allowed, since noise spread over the whole window
would then precede the onset audibly. With `bt = blocktype_prev[ch01(c)]`,
the final type of the previous granule:

```
l1 = PREECHO_RATIO_1 · nb_1[b]      if nb_1 exists and nb_1[b] > 0,  else none
l2 = PREECHO_RATIO_2 · nb_2[b]      if nb_2 exists and nb_2[b] > 0,  else none
lim = min(l1, l2)        if bt = NORM
    = l1                 if bt = START, STOP or SHORT
thr = min(ecb, lim)      ("none" imposes no limit)
nb_2[b] = nb_1[b];  nb_1[b] = ecb                     (before limiting)
```

The threshold may not exceed 2× (+3 dB) that of the previous granule, or 16×
(+12 dB) that of the one before. The second limit is dropped after a
non-NORM granule: in the sequence quiet / onset coded short / steady loud,
the steady granule compared with the quiet one would look like pre-echo,
although the onset was handled by the short granule. A history of 0 (silence)
imposes no limit. An onset after silence is the attack detector's case
([block-switching.md](block-switching.md)).

Both limits apply to every block type.

### 3.2 Short windows

No pre-echo limit inside the masking step (`thr = ecb`). Short pre-echo is
handled across windows ([block-switching.md](block-switching.md) §4).

### 3.3 Tonal ceiling

A threshold above the energy of a strongly tonal partition makes the
allocator take bits from other bands, and the tonal component is then
distorted heavily. The threshold is capped relative to the partition's peak
line:

```
thr = min(thr, max[b] · minval[b] · avg_mask)     if numlines[b] ≥ MINVAL_MIN_LINES
```

A partition of one line has its peak equal to its energy, so the ceiling
would only be a fixed offset, not a tonal protection. It does not apply
there. This rule holds at every sample rate.

`minval` per partition, from `bval` and `MINVAL_DEPTH`
([parameters.md](parameters.md) §1), **tuning**:

Long:
```
x = 20·(bval/10 − 1)
if x > 6: x = 30
if x < −MINVAL_DEPTH: x = −MINVAL_DEPTH
minval_l = 10^((x − 8)/10) · numlines
```

Short:
```
x = 7·(bval/12 − 1)
if bval > 12: x = x·(1 + 3.1·ln(1 + x))
if bval < 12: x = x·(1 + 2.3·ln(1 − x))
then the same two clamps, and minval_s = 10^((x − 8)/10) · numlines
```

Above about 13 Bark, `x = 30` puts the ceiling 22 dB above `max · numlines`.
It is then inactive in practice.

### 3.4 Masking lowering and energy clamp

```
ml = masking_lower[b] · 10^(adjust/10)
if ml > 1: thr = thr · ml
thr = min(thr, eb[b])
if ml < 1: thr = thr · ml
```

`adjust` is `MASK_ADJUST` for long windows and `MASK_ADJUST_SHORT` for short
windows ([parameters.md](parameters.md) §1), fixed per stream. The lowering
is applied here once, and nowhere else. A raised threshold is raised before
the clamp, so it never exceeds the partition energy; a lowered one is lowered
from the clamped value.

`masking_lower[b]` is a fixed tilt over the partition index, **tuning**:

```
masking_lower[b] = 10^(MASKING_TILT · (npart − b)/npart / 10)
```

The threshold is lowered by 7.4 dB at the lowest partition, falling linearly
in partition index to nearly 0 dB at the top. The short layout uses the same
law with its own `npart`.

## 4. Short windows

The procedure of §1–§3 per short window `j`, with:

- the short partition layout, spreading matrix (with `norm_s`), `minval_s`
  and `masking_lower` of the short layout;
- energies from `E_s[j]` ([spectral-analysis.md](spectral-analysis.md) §2);
- no pre-echo limit (§3.2).

The short thresholds of a granule MUST be those of this procedure wherever
a rule reads them: in a SHORT granule, and in the granule before one whose
short pre-echo control reads the previous granule
([block-switching.md](block-switching.md) §4) and whose own are read. An
encoder MAY leave them out for any other granule; it MUST NOT put another
value in their place ([known-complexity.md](known-complexity.md) entry 10).
The short spectra exist for every granule
([spectral-analysis.md](spectral-analysis.md) §2).

## 5. Partition to scalefactor band

Thresholds and energies go to bands by the sums of
[spectral-analysis.md](spectral-analysis.md) §5, the long layout onto `sfb_l`
and the short layout onto `sfb_s`, per window.
