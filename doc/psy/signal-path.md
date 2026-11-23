# Signal path, interface and state

## 1. Where the model sits

The model is active above the lowest quality level. At the lowest level the
encoder MUST NOT run the model, and its output MUST stay bit-exact with the
existing golden hashes ([README.md](README.md), frozen surface).

Per frame, in this order:

1. **Block types** (phase 4): attack detection on the time-domain input
   through the look-ahead (§2), then the final block type of every granule
   of the frame ([block-switching.md](block-switching.md)). Before phase 4,
   every granule is NORM.
2. **Filterbank and MDCT** of every granule of the frame, with the window of
   its block type. In phase 4, also the short analysis spectra of every
   granule, whatever its block type ([spectral-analysis.md](spectral-analysis.md) §2).
3. **Loudness** of every granule, then the **adaptive ATH update**, once
   ([ath.md](ath.md) §4). The updated factor is the only one used anywhere in
   this frame.
4. **Model**, per granule, per analysis channel
   ([spectral-analysis.md](spectral-analysis.md), [masking.md](masking.md)),
   then the M/S threshold correction ([stereo.md](stereo.md) §2), then the
   short pre-echo control (phase 4, [block-switching.md](block-switching.md) §4),
   then PE
   ([perceptual-entropy.md](perceptual-entropy.md)).
5. **M/S decision** for the frame ([stereo.md](stereo.md) §3).
6. **PE smoothing** (phase 3, [perceptual-entropy.md](perceptual-entropy.md) §3),
   then per granule its **bits** (phase 3, [reservoir.md](reservoir.md) §3)
   and the **noise allocation** (phase 2, [allowed-distortion.md](allowed-distortion.md),
   [noise-allocation.md](noise-allocation.md)).

Thresholds describe the granule whose MDCT they were computed from, so they
are used in the same frame without delay. Only the block type needs look-ahead.

## 2. Look-ahead (phase 4)

The final type of granule `g` depends on whether granule `g + 1` is short
(START must precede SHORT). That depends on the attack slots of `g + 1`,
including its look-ahead slot, which covers window 0 of `g + 2`
([block-switching.md](block-switching.md) §1.4).

The encoder MUST hold enough input to run the attack detector through window
0 of granule `g + 2` before it computes the MDCT of granule `g`. Where this
adds delay, the encoder delay it reports (`shine_encoder_delay()`) MUST
include it. The added delay MAY depend on the quality level.

Attack slots are placed in input time by the rule in
[block-switching.md](block-switching.md) §1.4. The offsets that follow from
the filterbank delay are generated constants, checked by the click test of
[tests.md](tests.md) §2.

## 3. Interface

### 3.1 Inputs per granule

- The MDCT coefficients of each coded channel (§1 step 2) and, in phase 4,
  its short analysis spectra.
- The final block type of each coded channel.
- The adaptive ATH factor `adjust_factor` of the frame.
- Persistent state (§4), and the per-stream parameters
  ([parameters.md](parameters.md)).

### 3.2 Outputs per granule

For each analysis channel `c`:

- `en.l[0..21]`, `thm.l[0..21]`: energy and masking threshold per long sfb.
- In phase 4, `en.s[0..12][0..2]`, `thm.s[0..12][0..2]`: the same per short
  sfb and short window.
- `pe[c]` for `c` ∈ {0, 1}. With M/S, `pe_MS[0]` (mid) and `pe_MS[1]`
  (side).
- `loudness_sq[c]` for `c` ∈ {0, 1} ([ath.md](ath.md) §4.1).
- `energy[c]`: total energy of the channel's long spectrum.

All values are in the energy unit of [conventions.md](conventions.md).

## 4. State

The model's state is a function of a bounded number of past granules, except
the adaptive ATH factor ([ath.md](ath.md) §4.2) and the PE smoothing history
([perceptual-entropy.md](perceptual-entropy.md) §3), which are bounded by
their own clamps. A stream that returns to digital silence MUST reach the
model's silence state after a bounded number of granules, whatever came
before.

Per analysis channel `c`:

| State | Meaning | Initial value | Phase |
|---|---|---|---|
| `nb_1[b]`, `nb_2[b]` | long spread threshold of the last two granules, before limiting | none | 1 |
| tonality history | the previous granule's long line energies, and in phase 4 its window 2 short line energies, for the smoothing of [spectral-analysis.md](spectral-analysis.md) §1.2, §2 | none | 1 |
| `blocktype_prev[c]`, c ∈ {0, 1} | final type of the previous granule | NORM | 4 |
| `last_thm.s[sb][j]` | processed short thresholds of the previous granule | none | 4 |
| attack history | the last two sub-block peaks of the previous granule, its `Esh[3]`, its slot 3 before suppression, and its final slot 2 (`last_attacks`) | peaks `PEAK_FLOOR`, `Esh` `3·PEAK_FLOOR`, slots 0 | 4 |

"None" means no history: a rule that reads it MUST behave as stated for that
case, and MUST NOT read a placeholder value. With no history:

- the long pre-echo limit does not apply ([masking.md](masking.md) §3.1);
- the tonality smoothing uses the current granule only;
- the short pre-echo control uses the current granule's own window 0 as the
  previous window ([block-switching.md](block-switching.md) §4).

The first granule's thresholds therefore come from its own analysis. Its
allowed distortion is the masking threshold, like any other granule's.

## 5. Initialisation

All constants are functions of `fs`, the bitrate row and the quality level.
They MUST be generated tables, or integer computations, so that they are
identical on every host ([fixed-point.md](fixed-point.md) §3):

1. partitions, Bark values, `mld`, for the long and the short layout
   ([spectral-analysis.md](spectral-analysis.md) §3);
2. spreading matrices ([spectral-analysis.md](spectral-analysis.md) §4);
3. per partition: `minval`, `masking_lower`, the partition ATH
   ([masking.md](masking.md) §3, [ath.md](ath.md) §2);
4. per band: the band ATH, the band offsets
   ([ath.md](ath.md) §2, [allowed-distortion.md](allowed-distortion.md) §1);
5. equal-loudness weights ([ath.md](ath.md) §4.1);
6. the partition-to-band mapping ([spectral-analysis.md](spectral-analysis.md) §5);
7. the short-window temporal decay ([allowed-distortion.md](allowed-distortion.md) §3).

Two encoders initialised at the same time MUST NOT share mutable state.
