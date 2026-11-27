# Conformance

Three kinds of check, with different lifetimes:

1. **Invariants** (§1, §2): stated by principle, run in CI, kept.
2. **The float oracle** (§3): a development aid, used while a phase is built
   and deleted once that phase lands.
3. **The metric report** (§4): the gate for every audio-changing change.

Every check here MUST be seen to fail against a deliberately broken build
before its pass counts. A suite MUST report how many checks ran, and fail if
none did.

## 1. Invariants, by part

Run at every sample rate in {8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48} kHz,
for mono and stereo.

| Part | Invariants |
|---|---|
| [spectral-analysis.md](spectral-analysis.md) | Partition line counts sum to 576 and 192. Every line is in exactly one partition and every partition in exactly one sfb. Band energies equal the direct sum of their MDCT line energies. A tone swept across an sfb edge moves its energy between the two bands with no jump and no loss. A full-scale sine has a total line energy of 1 within the stated tolerance, on every transform. |
| [masking.md](masking.md) | Thresholds are finite and ≥ 0 on every input. With a lowering ≤ 1, the threshold is ≤ the partition energy, and equals energy × lowering when the spread threshold exceeded the energy. `mask_add(x, 0) = x`, and the result lies between the larger operand and the boosted sum. A flat spectrum gives tonality 0, a pure tone on a line centre gives the maximum in its partition. On white noise the mean index falls as partitions widen. The threshold of a pure tone sits below the tone's energy. |
| [ath.md](ath.md) | Band ATH > 0 in every band. A fade-in and a fade-out trace `adjust_factor` as in §4.2: it reaches 1 within one frame of a loud frame, and falls gradually. The same input at 0 dB and −20 dB gives the factor the curve predicts. |
| [stereo.md](stereo.md) | L = R gives finite S thresholds with zero S energy. M/S thresholds never exceed their own energy. Mono and dual-channel output does not depend on any M/S parameter. |
| [perceptual-entropy.md](perceptual-entropy.md) | PE is finite and ≥ the base on silence and on full-scale noise. It is continuous across the ratio cap. The smoothing factor is finite and positive on alternating silence and full-scale noise every frame. |
| [block-switching.md](block-switching.md) | A steady tone and a held trumpet-like harmonic tone give only long blocks. A sine stepped ×10 at a known sample gives a short block whose attack slot contains the step. An anti-phase click (in L − R only) gives short blocks on both channels. Coupled channels always share a type. The type sequence obeys §3. The decisions are the same at 0, −20 and −40 dB input, or differ only where `PERIODIC_CEILING` predicts. |
| [allowed-distortion.md](allowed-distortion.md) | `xmin` ≥ `XMIN_FLOOR` in every band that has one, and none in the others. A band whose energy is below its adapted ATH may be all noise: its `xmin` is its energy. Digital silence gets `XMIN_FLOOR`. Once bits follow demand (phase 3), −90 dBFS noise and digital silence take the same bits. After a decaying click, short-window `xmin` decays and never exceeds the click window's. |
| [noise-allocation.md](noise-allocation.md) | Every `ix` ≤ `IX_MAX`, and every channel fits its share of the granule's bits, the shares following the channels' PE and adding up to the granule's bits. Every scalefactor is within what its band's format codes, and the chosen `scalefac_compress` is the cheapest that codes them all. The chosen attempt's `D` is ≤ that of attempt 0. A granule with no band over keeps every scalefactor at 0. The region split of a long granule takes no more bits than any other pair of region counts, each with its cheapest table. A SHORT granule's chosen attempt has a `D` ≤ that of its starting scalefactors (§6.1), which are within their format. `preflag` follows §5.1 on every granule, and the noise measured with it matches the reconstruction of the decoder's formula. Silence quantises with step size and scalefactors 0. A tone in each band, at both MPEG versions, decodes at the level of the quality 0 stream within 0.1 dB, so scalefactors are decoded with the amplification they were encoded with. A SHORT granule is checked on its `3·13` bands: every scalefactor within its short format, the cheapest short coding, `xmin` per window matching the formulas of allowed-distortion.md §2, §3, and some short granules amplified in each format. |
| [reservoir.md](reservoir.md) | Read back from the stream: every frame's `main_data_begin` is the capacity earlier frames left unused, at most `RESV_MAX / 8`, and no frame uses more than its capacity plus that. The stream is the same whether the input is given in one call or frame by frame, and the bytes returned add up to whole frames once flushed. A steady tone gets the mean in every granule once the PE history is full. No granule gets less than the mean. A burst after a quiet passage gets more than the mean in its first granule. A channel uses fewer bits than its share only with no band over, and then one step size further either puts a band over or leaves more than the reservoir can take; a channel with a band over uses the smallest step size that fits. Full-scale noise at 32 kbps, silence, and the two alternating every frame give budgets within 0 and `nch · PART2_3_MAX`. Two independent decoders decode the stream to the same samples as each other. The lowest quality level writes `main_data_begin` 0 in every frame. |
| [bandwidth.md](bandwidth.md) | At every sample rate, bitrate and channel count, every coded and analysis line at or above the cutoff is zero, the lines below the pass edge are unchanged, and the gains in between follow the taper. Mono at 22.05 kHz and 64 kbps, and every stream from 128 kbps per channel, are not filtered. |
| [signal-path.md](signal-path.md) | A click at a known sample shows up in the thresholds of the granule whose MDCT codes it and in the attack slot of the short window that codes it. The first three granules are finite and come from their own analysis. A stream with and without one leading granule of silence converges from the third granule on. After full-scale clipped input then silence, the state returns to the silence state within a bounded number of granules. Two encoders initialised in parallel threads give the same output as serial runs. |

**Binding:** decisions (block type, M/S choice), counts, equalities, which
bands hold, and the bit-exact golden hashes of each quality level on every
CI architecture. **Incidental:** the names of the test signals, the report
format, timings.

## 2. Failure modes the harness simulates

Digital silence, one silent channel, L = R, L = −R, a full-scale impulse at
sample 0, DC, full-scale clipped input, a single non-zero sample at the very
first input position, every sample rate, both MPEG versions. Each exists
because a ratio of energies has no natural value there
([known-complexity.md](known-complexity.md) entry 4).

The click test places a single full-scale sample at a known input index,
in every slot position of a granule, and checks the alignment of
[block-switching.md](block-switching.md) §1.4 and
[signal-path.md](signal-path.md) §2.

## 3. Float oracle (development only)

While a phase is built, a floating-point implementation of this spec, in
`tools/` and outside the library, is the numeric reference. It reads the same
generated tables. Against it, on the synthetic signals and a music corpus:

- decisions (block type, M/S choice) MUST agree on every synthetic decision
  test, and on at least `DECISION_AGREEMENT` = 99% of granules of the corpus;
- band thresholds MUST agree within `THRESHOLD_TOLERANCE` = 0.5 dB in at
  least `NUMERIC_AGREEMENT` = 99.9% of the bands whose energy is above their
  ATH;
- PE MUST agree within 1% in at least `NUMERIC_AGREEMENT` of granules and
  channels.

The model has steps: the tonality index ([masking.md](masking.md) §1), and the L/R similarity test of
the M/S correction ([stereo.md](stereo.md) §2). Where an operand sits at a
step, one LSB of difference moves a threshold by several dB, and by more
than 10 dB for a side threshold, so two correct implementations disagree
there. The share outside tolerance is the measure of
how often that happens. A systematic error shows up well above it.

The oracle checks that the fixed-point arithmetic implements the spec. It
does not judge quality. Once a phase lands with its goldens and its metric
report, the oracle is deleted. From then on the golden hashes pin the
fixed-point behaviour.

## 4. Metric report

Every audio-changing change, including a change to any **tuning** parameter,
comes with the report of `TODO.md`: output level, gain-corrected SNR and the
perceptual score per item, compared with the previous commit.

### 4.1 The score

The perceptual score of an item is the 2f-model (Kastner and Herre, WASPAA
2019): an estimate of the MUSHRA score, from 0 (bad) to 100 (excellent),
from two model output variables of the basic version of PEAQ (ITU-R
BS.1387), with the decoded stream time-aligned to its source:

```
score = 56.1345 / (1 + (−0.0282·AvgModDiff1 − 0.8628)²) − 27.1451·ADB + 86.3515
```

limited to 0..100, in the parameters calibrated for a public PEAQ
implementation. The Objective Difference Grade of PEAQ (the lower of its
advanced and basic versions, 0 to −4) is reported beside it.

The score is a measuring instrument with a known resolution, not a listener.
It was validated on a public listening test of codec-like artefacts with
expert listeners (ODAQ: 30 trials, 26 listeners, MUSHRA scores), and the
rules below come from that validation:

- **Where it follows listeners.** Its scores correlate with the listeners'
  overall at 0.85 (the ODG: 0.80), and on band limitation (0.97), pre-echo
  (0.95), spectral holes (0.84), unmasked noise (0.81) and tonality mismatch
  (0.76). The ODG follows spectral holes poorly (0.36), grading them between
  −3.1 and −3.6 whether listeners found them bad or good: that is why it is
  not the score any more. Over seven listening tests of coded audio, the
  2f-model ranks first of 13 measures and the ODG eighth (Torcoli et al.,
  IEEE TASLP 2021).
- **Its resolution.** Between two versions of one item that listeners
  clearly separate, the scores picked the same winner in every pair (50 of
  50) when they differed by 5 or more, in 81% when they differed by 2 to 5,
  in 64% below 2. A difference below 5 on one item says little.
- **What it does not see.** Neither it nor the ODG is validated for the
  stereo image: changes to stereo coding are checked by listening.
- **Implementations** differ, and the open ones do not meet the standard's
  conformance tolerances. Scores are compared only between runs of one
  implementation, never with published figures.

### 4.2 The gate

Between the previous commit and the new one, and between each quality level
and the next one:

- **Per item.** The score MUST NOT fall by more than `SCORE_TOLERANCE` = 5,
  except where both scores are 0, the bottom of the scale.
- **Per configuration.** The mean score over the items of one sample rate,
  channel layout and bitrate MUST NOT fall by more than
  `SCORE_MEAN_TOLERANCE` = 0.75. A mean over many items resolves what one
  item cannot.
- **Misjudged items.** An item whose score falls by more than
  `SCORE_TOLERANCE` when the bitrate rises, at one quality level, is one the
  score misjudges. It is reported, and left out of both rules. It is a
  candidate for a listening check.

A tuning decision rests on means over a corpus, on items the parameter was
not tuned on. The corpus MUST cover every sample rate family and mono, and
SHOULD include speech, solo instruments and percussive material beside
music: a corpus of music at 44.1 kHz alone hid every effect that depends on
the sample rate or on sparse signals.

Rules that say they need a perceptual judgement are settled by it:

- `TONALITY_SCALE` on noise-like spectra ([masking.md](masking.md) §1);
- the tonality smoothing of the MDCT path
  ([spectral-analysis.md](spectral-analysis.md) §1.2) and the FFT windows
  ([spectral-analysis.md](spectral-analysis.md) §6);
- the ATH placement and its retuning with the band ATH rule
  ([ath.md](ath.md) §1–§2, [parameters.md](parameters.md) §1);
- the PE coefficients at rates other than 44.1 kHz
  ([perceptual-entropy.md](perceptual-entropy.md) §2);
- every per-bitrate value, and the allocator offsets against the phase 2
  loop ([allowed-distortion.md](allowed-distortion.md)).

The score does not replace listening: a release that changes the model is
listened to (`TODO.md`).
