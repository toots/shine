# Spectral analysis and partitions

The model works on line energies of the MDCT, the transform the encoder
already codes. An FFT analysis path MAY be used instead (§6). Both feed the
same partition procedure, and their energies are in the same unit
([conventions.md](conventions.md)).

## 1. Long spectrum

### 1.1 Line energies

For each analysis channel `c` and granule, with `X[k]` the long MDCT
coefficients, `k = 0..575`:

```
E[k] = K_long · X[k]²
```

`K_long` normalises to the energy unit ([fixed-point.md](fixed-point.md) §3).
Line `k` covers `[k, k + 1)·fs/1152` Hz.

- Left and right use their own coefficients. Mid and side use the
  coefficients `(X_L ± X_R)/√2`, which are the ones an M/S frame codes.
- The long spectrum of a NORM, START or STOP granule is its coded MDCT. The
  long spectrum of a SHORT granule is an analysis-side long MDCT of
  the same input, with the NORM window. The long state then stays continuous
  across short granules.

`energy[c] = Σ_k E[k]`.

### 1.2 Tonality spectrum

The energy of one MDCT line for a stationary sine varies with the sine's
phase from one granule to the next. Tonality is a peak-to-average measure,
so it is computed from line energies averaged over the last
`TONALITY_SMOOTHING` granules:

```
E_t[k] = (E_g[k] + E_{g−1}[k]) / 2
```

With no previous granule (stream start), `E_t = E_g`. Only the tonality
index ([masking.md](masking.md) §1) reads `E_t`. Whether this smoothing is
enough is a perceptual judgement ([tests.md](tests.md) §4).

## 2. Short spectra

Three windows `j = 0, 1, 2` per granule. `X_s[j][k]`, `k = 0..191`, are the
short MDCT coefficients of window `j`, and line `k` covers
`[k, k + 1)·fs/384` Hz:

```
E_s[j][k] = K_short · X_s[j][k]²
```

- For a SHORT granule they are its coded short MDCTs. For any other granule
  they are analysis-side short MDCTs of the same input. Short spectra exist
  for every granule, so the short pre-echo control always has a real
  previous window ([block-switching.md](block-switching.md) §4).
- Mid and side are formed as for the long spectrum.
- The tonality spectrum of window `j` averages it with window `j − 1`
  (window 2 of the previous granule for `j = 0`).

## 3. Partitions

Partitions are the analysis bands, about `DELBARK` wide, and they MUST nest
in the sfbs: no partition straddles an sfb edge. One layout is built over the
576 long lines with `sfb_l`, and one over the 192 short lines with `sfb_s`.
With `Δ = fs/(2L)` the line width, for `L` lines:

```
i = 0;  k = 0
while k < L:
    start[i] = k
    k2 = k + 1
    while k2 < L and k2 is not an sfb edge
          and bark(k2·Δ) − bark(start[i]·Δ) < DELBARK:
        k2 += 1
    numlines[i] = k2 − start[i]
    i += 1;  k = k2
npart = i
```

Every line belongs to exactly one partition, and every partition to exactly
one sfb. Per partition, with `s = start[i]` and `w = numlines[i]`:

```
bval[i]       = (bark((s + 0.5)·Δ) + bark((s + w − 0.5)·Δ)) / 2
bval_width[i] = bark((s + w)·Δ) − bark(s·Δ)
mld[i]        = stereo_demask((s + w/2)·Δ)
```

Partition energies, from `E` (long) or `E_s[j]` (short):

```
eb[b]  = Σ E over the partition's lines
max[b] = max E over the partition's lines
```

and `max_t[b]`, `avg_t[b] = Σ E_t / numlines[b]` from the tonality spectrum.

## 4. Spreading matrix

For maskee partition `i` and masker partition `j`:

```
S[i][j] = s3(bval[i] − bval[j]) · bval_width[i] · norm[i]
```

`s3` is a density per Bark: its integral is 1. A masker of energy `eb[j]`
puts `eb[j]·s3(z − bval[j])` per Bark at `z`, and the maskee partition
collects that density over its own width. The matrix is thus a
discretisation of the spreading integral over unequal cells, and with
`norm = 1` each column sums to about 1: spreading moves energy, it does not
create it. Weighting by the masker's width instead would count width twice,
since `eb` is an energy, not a density, and a tone's masking would grow with
the width of the partition it falls in. Rows are not normalised to unit sum. For each row `i`, `[lo_i, hi_i]` is the range of `j`
with `S[i][j] > 0`. Only that range is used.

`norm[i]` is the per-partition SNR offset, **tuning**:

- long: 0 dB (`norm = 1`);
- short: −8.25 dB for `bval < 13`, then linear in `bval` up to −4.5 dB at
  `bval = 24`, extrapolated beyond; `norm = 10^(snr/10)`.

## 5. Partitions to scalefactor bands

Because partitions nest in sfbs, a band's energy and threshold are the sums
over its partitions:

```
en[sb]  = Σ eb[b]  over partitions b in band sb
thm[sb] = Σ thr[b] over partitions b in band sb
```

The band energy is exactly the energy of the band's MDCT lines.

## 6. FFT analysis path (MAY)

An implementation MAY compute the line energies with FFTs instead of the
MDCT, at quality levels that can afford it.

- **Long:** a 1024-point FFT with the Blackman window
  `w[i] = 0.42 − 0.5·cos(2π(i+0.5)/1024) + 0.08·cos(4π(i+0.5)/1024)`,
  centred on the input span of the granule's MDCT window. Lines `k = 0..512`,
  line `k` covering `[k − 0.5, k + 0.5)·fs/1024`, clipped to `[0, fs/2]`.
- **Short:** three 256-point FFTs with the Hann window
  `w_s[i] = 0.5·(1 − cos(2π(i+0.5)/256))`, each centred on the input span
  of its short window. Lines `k = 0..128`.
- Energies `|X[k]|²` are normalised to the energy unit. Mid and side are
  formed on the transform coefficients as `(L ± R)/√2`.
- The partitions are built as in §3 over these lines, without the sfb-edge
  condition. A partition's energy and threshold go to each band in
  proportion to the share of the partition's frequency range that lies
  inside the band. This holds when several band edges fall inside one
  partition.
- The tonality smoothing of §1.2 MAY be omitted, since FFT energies do not
  depend on phase.
- The allowed distortion then transfers the ratio `thm/en` onto the MDCT
  energy ([allowed-distortion.md](allowed-distortion.md) §2).

The choice of window matters to the tonality estimate, because window
families trade sidelobe level against main-lobe width. Which window serves
it better is a perceptual judgement ([tests.md](tests.md) §4).
