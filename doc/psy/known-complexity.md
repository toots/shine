# Known complexity

Pitfalls of psychoacoustic models of this family. Each entry is stated
without reference to any implementation: the situation, what an observer saw
go wrong, why it is easy to get wrong again, and a check by principle. Counts
are the number of separate corrections the difficulty needed. **Answered by**
links the rule that handles it now. Every entry is a conformance check
([tests.md](tests.md)).

## 1. Partition bands and scalefactor bands do not nest

- **Situation.** Thresholds come from perceptual partitions and are mapped
  onto the codec's sfbs, at every sample rate and for both block sizes. It
  matters where a partition straddles an sfb edge, at the last sfb, and at the
  top partition.
- **What went wrong.** Reads past the last partition fed garbage into
  energies and thresholds, and sometimes crashed. Uninitialised values
  reached some sfbs. The short-block mapping disagreed with the long one. A
  straddling partition was split 50/50 instead of by its frequency share, or
  not weighted at all. Partition construction sometimes returned one
  partition too many. Count: 6.
- **Why it is hard.** Two grids, both derived from the sample rate and the
  transform size, with no containment relation. Off-by-one at the last band
  is natural. Errors move thresholds by a few dB only, so they stay inaudible
  on most material.
- **How to tell.** Per rate and layout: line counts sum to the layout size,
  each line is in one partition, band energy sums equal the partition total
  and the direct line sum, and a tone swept across every sfb edge moves
  smoothly between bands.
- **Answered by.** Partitions nest in sfbs on the MDCT path, so the mapping
  is a plain sum ([spectral-analysis.md](spectral-analysis.md) §3, §5). On
  the FFT path, shares by frequency overlap (§6).

## 2. ATH in short blocks: wrong grid, wrong scale

- **Situation.** Short-block thresholds floored or compared with the
  threshold of hearing.
- **What went wrong.** A per-partition ATH built on the long grid was indexed
  with short partition numbers. A transform-size scale ratio computed in
  integer arithmetic was exactly 0, so the short ATH silently vanished. Once
  the floor was in force it dominated short blocks and was clamped by an
  admitted heuristic. Count: 4.
- **Why it is hard.** The ATH is a power per frequency. Per partition it must
  account for the transform's energy scale and the partition's frequency
  span. Short partitions span other frequencies than long ones, so the floor
  cannot be shared. A vanished floor leaves no trace other than cheaper short
  blocks.
- **How to tell.** For a stationary tone at ATH level, long and short
  analyses agree on the floor at its frequency within tolerance. The short
  floor is strictly positive in every partition and band.
- **Answered by.** One energy unit for every transform and block length
  ([conventions.md](conventions.md)), one placement
  ([ath.md](ath.md) §1), and the same per-layout rule with no width factor
  (§2). The ATH is not a floor on partition thresholds. It enters through the
  M/S correction and the allowed distortion only.

## 3. Integer truncation in derived constants

- **Situation.** Constants derived at initialisation: line spacing, scale
  ratios between transforms, table indices from logarithms.
- **What went wrong.** A line spacing lost its fraction (43 Hz for 43.07). A
  scale ratio became 0. Implicit conversions in table lookups. Count: 3.
- **Why it is hard.** The result is a plausible number. In fixed point,
  every such constant is a deliberate choice of rounding and format, so the
  class of bug is more likely, not less.
- **How to tell.** Recompute every table in high precision and compare. Line
  centre frequencies match their formula within one LSB at the highest line,
  for every rate.
- **Answered by.** All such constants are generated tables
  ([fixed-point.md](fixed-point.md) §3).

## 4. Ratios of energies on silence and one-sided signals

- **Situation.** All-zero input, a band with no energy, one silent channel,
  L = R (side exactly zero), an impulse, the first granule.
- **What went wrong.** Division by zero or 0/0 in the attack ratio, the
  tonality index, the masking addition law, the log interpolation, the M/S
  sum limit and the PE smoothing normaliser. Combining a contribution with a
  zero one returned zero, which zeroed thresholds next to silent partitions.
  Tests of "not equal to zero" let negative and denormal values through.
  Count: about 8.
- **Why it is hard.** Almost every quantity is a ratio of energies, and
  silence is ordinary input. Each guard picks a fallback, and the fallback is
  behaviour.
- **How to tell.** Run the failure modes of [tests.md](tests.md) §2. Every
  intermediate and output is finite and ≥ 0. A silent band's threshold
  equals its floor. Combining with a zero contribution returns the other.
- **Answered by.** Each rule states its zero case:
  [masking.md](masking.md) §1, §2, §3.1; [conventions.md](conventions.md)
  (`interp`); [block-switching.md](block-switching.md) §1.2 (peak floor);
  [stereo.md](stereo.md) §2; [perceptual-entropy.md](perceptual-entropy.md)
  §3.

## 5. Non-finite and saturating input spreads through the model

- **Situation.** Floating-point input with NaN or infinities; in integer
  input, full-scale or clipped samples driving energies to their maximum.
- **What went wrong.** One bad sample spread across band energies and turned
  the surrounding frames into noise. A screening check was removed by the
  compiler under aggressive float optimisation. Count: 2.
- **Why it is hard.** The model's recursions carry a bad value forward for
  many granules. In floating point, the usual finiteness tests are exactly
  what aggressive optimisation removes.
- **How to tell.** Inject saturating input then silence: the state returns to
  the silence state within a bounded number of granules. No accumulator
  overflows.
- **Answered by.** Integer input only. No overflow for any input
  ([fixed-point.md](fixed-point.md) §1). Bounded state
  ([signal-path.md](signal-path.md) §4).

## 6. Evaluation order and absolute epsilons

- **Situation.** Quiet passages near the threshold floor. Energies compared
  with thresholds at very small magnitudes. Different compilers and
  optimisation flags.
- **What went wrong.** Bitrate bloat on near-silent input under fast float
  math. Equality tests against tiny absolute constants broke. Reordering a sum
  changed some encodes. Count: 4.
- **Why it is hard.** At these magnitudes, absolute epsilons depend on the
  input's scale. A band "above the cut" in one build is below it in another,
  and that flips whole decisions.
- **How to tell.** −90 dBFS noise and digital silence give the same
  bitrate. The same input gives the same decisions on every host.
- **Answered by.** Integer arithmetic, bit-exact on every host
  ([fixed-point.md](fixed-point.md) §1). Floors are levels relative to full
  scale (`XMIN_FLOOR`, `PEAK_FLOOR`, [parameters.md](parameters.md) §2).

## 7. Attack detection: periodic signals against real transients

- **Situation.** Block switching decided from sub-block levels of a
  high-passed signal. Problem material: trumpet and other strongly periodic
  tones; castanets, snaps and fast attacks.
- **What went wrong.** Detection was tuned back and forth: too sensitive gave
  spurious short blocks and bit starvation on periodic signals; too strict
  missed attacks and gave pre-echo. A rule on the level change between whole
  short windows was added, retuned, and restricted by an absolute level
  guard. An alternative rule was tried and removed. Count: about 7, plus the
  threshold churn below.
- **Why it is hard.** A periodic signal with a period near the sub-block
  length gives large ratios with no transient. The fix-ups use absolute
  levels, so the decision depends on input gain. Each test sample pulls the
  tuning another way.
- **How to tell.** A held tone and a trumpet-like harmonic tone give only
  long blocks. Castanets give short blocks with the attack in the right slot.
  A sine stepped ×10 gives a short block containing the step, at every rate.
  The decisions at −20 and −40 dB differ only where the level guard
  predicts.
- **Answered by.** [block-switching.md](block-switching.md) §1. Level
  constants in dBFS ([conventions.md](conventions.md)).

## 8. Attack detection across channels, M/S and mono

- **Situation.** Joint stereo with attacks detected on mid and side as well,
  dual channel, mono.
- **What went wrong.** An attack found on M/S did not force short blocks on
  the coded pair. Channels of different block types broke several hardware
  decoders. The pre-echo control read the wrong channel's previous type. On
  mono, the decision read the absent channel's uninitialised state and could
  force short blocks. Count: 4.
- **Why it is hard.** The model analyses up to four channels and codes two.
  Every decision is mapped from analysis channels to coded channels, and an
  absent channel must be neutral.
- **How to tell.** An anti-phase click gives short blocks on both channels.
  Both channels always share a type. A mono encode after a stereo encode in
  the same process gives the same output as alone.
- **Answered by.** [block-switching.md](block-switching.md) §1.7, §2;
  `ch01` ([conventions.md](conventions.md)); per-encoder state
  ([signal-path.md](signal-path.md) §5).

## 9. Recursive state: initial values and missing history

- **Situation.** State carried across granules: thresholds one and two back,
  the previous block type, sub-block peaks and attack slots, the ATH
  adaptation, the PE history. At stream start, and after a granule whose
  analysis of one block size was skipped.
- **What went wrong.** The initial block type was changed for header-frame
  compatibility. Previous short thresholds started at an arbitrary value
  that pre-echo control then clamped to. The ATH adaptation's start value
  was changed. With no long history after a short granule, a limit was
  guessed from the current energy, and nothing validated the guess. Count:
  about 5.
- **Why it is hard.** Initial state is audible behaviour, a muted or
  pre-echoed first frame, that no steady-state test exercises. Skipping work
  for speed removes history a later granule needs.
- **How to tell.** A stream starting with a full-scale transient at sample
  0, and one starting with silence then a step. The first three granules are
  finite and come from their own analysis. With and without one leading
  granule of silence, thresholds converge from the third granule on.
- **Answered by.** "No history" is a defined state, never a placeholder
  ([signal-path.md](signal-path.md) §4). The long analysis runs on every
  granule ([spectral-analysis.md](spectral-analysis.md) §1.1), so there is no
  guessed limit ([masking.md](masking.md) §3.1).

## 10. Short pre-echo control depends on the previous granule's last window

- **Situation.** A long → short transition with the attack in the first
  window of the new granule; consecutive short granules.
- **What went wrong.** Window 0 interpolates toward the threshold of the
  window before it, which is window 2 of the previous granule. The current
  granule's own values were used instead, the wrong time. Real short
  thresholds of long granules were needed to fix it, at a speed cost, and
  skipping them was tempting again. How the two interpolations combine was
  changed. Count: 4.
- **Why it is hard.** A hidden cross-granule dependency that a speed-up
  breaks, and the order of two limits changes the result.
- **How to tell.** On a long → short transition, window 0's limit comes from
  a real short analysis of the previous granule: the result does not depend
  on how the previous granule was coded. A two-attack sequence (window 2 of
  granule n, then window 0 of granule n+1) checks the combination order.
- **Answered by.** Short spectra and thresholds for every granule
  ([spectral-analysis.md](spectral-analysis.md) §2,
  [masking.md](masking.md) §4). Sequential order of the limits
  ([block-switching.md](block-switching.md) §4).

## 11. Long pre-echo control after a short granule

- **Situation.** The sequence quiet granule → loud onset coded short →
  steady loud granule.
- **What went wrong.** The steady granule's threshold was limited by the
  quiet granule two back, as if it were pre-echo, although the onset was in
  the short granule. A variant replaced the limit by a fixed floor. The
  joint-stereo lookup of the previous type was wrong. Count: 3.
- **Why it is hard.** The limit is relative to history, and which history is
  valid depends on the previous block type.
- **How to tell.** In the three-granule sequence the third granule is not
  limited by the first. Its threshold equals the steady-state value reached
  after ten loud granules, within tolerance.
- **Answered by.** [masking.md](masking.md) §3.1.

## 12. Mid/side thresholds

- **Situation.** Joint stereo with near-mono material (side tiny or zero),
  L/R thresholds more than about 2 dB apart, mono and dual-channel encodes.
- **What went wrong.** Faulty stereo demasking, M/S masking-ratio errors,
  M/S switching that ignored single-granule frames. The ATH term of the M/S
  correction was changed from a per-channel floor to the smaller of the two.
  An inter-channel masking effect leaked into mono and dual-channel encodes,
  saved few bits, raised the risk of artefacts, and was dropped. Count:
  about 8.
- **Why it is hard.** M/S thresholds cannot be derived by transforming L/R
  ones: noise in M and S reappears in both L and R. The demasking factor and
  the sum limit are heuristics valid only when L and R thresholds are close.
- **How to tell.** L = R gives finite S thresholds. The L/R noise implied by
  the M/S thresholds does not exceed the smaller L/R threshold times
  `MSFIX`. Mono and dual-channel output does not depend on any M/S
  parameter.
- **Answered by.** [stereo.md](stereo.md) §2–§3. There is no inter-channel
  masking parameter.

## 13. Combining simultaneous masking from neighbouring partitions

- **Situation.** A partition's threshold accumulates spread contributions
  with a non-linear addition law depending on their ratio, their distance
  and tonality.
- **What went wrong.** Division by zero on a zero operand. The ATH was
  included in the addition, then removed. The law was dropped and
  reinstated. The distance window became tonality-dependent. Accumulating in
  both directions and keeping the larger was tried and withdrawn. Count:
  about 10.
- **Why it is hard.** The law is order-dependent: low-to-high does not give
  the high-to-low result. It is a fitted heuristic, and small changes move
  tonal thresholds by dB.
- **How to tell.** Two maskers spread to an energy equal to the sum of
  their spread energies, within the fixed-point tolerance, in any order.
  A zero masker adds nothing.
- **Answered by.** [masking.md](masking.md) §2: a plain sum of energies,
  with no order, no distance window and no ratio-dependent gain.

## 14. Spreading function discretisation and normalisation

- **Situation.** The spreading function evaluated on partition centres of
  unequal Bark width, for both layouts and every rate.
- **What went wrong.** Widths computed between line centres instead of
  edges. Contributions weighted by a nominal Bark step instead of each
  partition's width. Normalisation added, removed and re-added. Short-block
  spreading errors. Count: about 7.
- **Why it is hard.** It is a discretised integral over unequal cells.
  Normalising rows to unit sum shifts absolute levels by several dB, which
  tuned offsets elsewhere then absorb.
- **How to tell.** White noise gives per-partition spread energy that follows
  the analytical integral at every rate. A 1 kHz tone's masking curve, in
  Bark, does not move with the sample rate beyond the partition
  quantisation.
- **Answered by.** Widths from line edges, weights by the maskee partition's
  width, rows not normalised
  ([spectral-analysis.md](spectral-analysis.md) §3–§4).

## 15. Tonality on few lines; strongly tonal signals

- **Situation.** Tonality from peak against average over two or three
  partitions. At low frequencies and low rates partitions hold one or two
  lines. Problem material: lead voice, tonal test signals, sustained wind
  instruments.
- **What went wrong.** Over-optimistic masking on strongly tonal signals made
  the quantiser fail. Thresholds were capped at the peak line times a
  frequency-dependent factor. Shortcuts for single-line partitions, blends
  for small partitions, and borrowing long tonality for short blocks were
  each tried and withdrawn. Count: about 8.
- **Why it is hard.** A peak/mean ratio over two lines is noise, and the
  tone/noise masking difference turns estimator variance into threshold
  variance. MDCT line energies add phase variance to that.
- **How to tell.** A pure tone at each sfb gets a threshold below its energy,
  never above band energy. A flat spectrum gives tonality 0. On a stationary
  tone, one-line partitions keep a stable index from granule to granule.
- **Answered by.** [masking.md](masking.md) §1, §3.3 (ceiling only with at
  least `MINVAL_MIN_LINES`), the tonality smoothing
  ([spectral-analysis.md](spectral-analysis.md) §1.2). Short windows estimate
  their own tonality ([masking.md](masking.md) §4).

## 16. Low sample rates (MPEG-2 and 2.5)

- **Situation.** 8–24 kHz: one granule per frame, single-line partitions,
  upper sfbs unusable or empty.
- **What went wrong.** The model did not work at these rates at first. A
  short-block SNR error. M/S switching that assumed a second granule. Band
  tables wrong for MPEG-2.5. A low-frequency ceiling removed below 44 kHz
  because only single-line partitions were affected. A cut at the last
  usable sfb. Count: about 6.
- **Why it is hard.** Grids degenerate at these rates, and assumptions that
  hold at 44.1 kHz break quietly: two lines per partition, a second granule,
  a top sfb with signal. Test corpora are mostly 44.1 kHz.
- **How to tell.** The invariants of [tests.md](tests.md) §1 at every rate.
  A tone in the top usable sfb at 8 kHz gets a finite, non-zero threshold.
- **Answered by.** Rules stated for every rate: the tonal ceiling by line
  count, not by rate ([masking.md](masking.md) §3.3); the M/S decision over
  `mode_gr` granules ([stereo.md](stereo.md) §3); the band limits of
  [allowed-distortion.md](allowed-distortion.md) §2.

## 17. Order of the threshold limits

- **Situation.** After spreading, a partition threshold is limited by
  pre-echo history, capped by the tonal ceiling, scaled by the masking
  adjustment, and capped at partition energy.
- **What went wrong.** The order is not commutative. Folding one factor into
  another moved it across the energy cap and was withdrawn. The adjustment
  was applied a second time downstream. Count: 2.
- **Why it is hard.** Each clamp is reasonable alone. Their order decides
  whether a raised threshold can exceed the energy, and whether a lowered
  one is lowered from the capped value.
- **How to tell.** An adjustment above 1 and one below 1, on a partition
  whose spread threshold exceeds its energy: above 1 the result equals the
  energy, below 1 it equals energy × adjustment.
- **Answered by.** [masking.md](masking.md) §3 order, adjustment applied once
  (§3.4, [perceptual-entropy.md](perceptual-entropy.md) §1).

## 18. ATH adaptation to programme loudness

- **Situation.** The ATH scaled per frame by an equal-loudness-weighted
  loudness estimate, by up to about 32 dB on quiet material.
- **What went wrong.** Introduced, withdrawn for breaking tests and for
  mixing dB with linear units, reintroduced. The loudness source changed
  three times. The maximum adjustment was raised. The start value changed.
  Low-volume problems needed further fixes. Its value was renamed between dB
  and linear. Count: about 18, the most churned part.
- **Why it is hard.** It encodes an assumption about playback level. It is a
  stateful attack/release filter that mixes dB and linear forms. In fixed
  point the loudness normalisation must be kept exactly or the adaptation
  shifts by a constant.
- **How to tell.** A fade-in and a fade-out, with the factor logged per
  frame: it rises within one frame and falls gradually. 0 dB and −20 dB
  inputs give the factor the curve predicts. Units are the same at every
  interface.
- **Answered by.** [ath.md](ath.md) §3–§4: one linear factor, one use
  (`athAdjust`), one value per frame, a full-scale-relative loudness
  reference, a switch to turn it off.

## 19. Hidden absolute-level assumptions

- **Situation.** Every level-dependent constant assumes an input scale: the
  attack peak floor, the periodicity ceiling, the loudness normalisation,
  the ATH placement.
- **What went wrong.** Nothing broke at once. The constants were introduced
  one by one (entries 7, 9, 18), and the scale assumption is implicit in all
  of them.
- **Why it is hard.** A port that changes the sample scale or headroom
  silently changes block switching and the ATH, and still sounds plausible.
- **How to tell.** The decision corpus and ATH tests give the same results
  when the input is supplied at another internal scale with the constants
  converted.
- **Answered by.** Every level constant in dBFS or relative to full scale
  ([conventions.md](conventions.md), [parameters.md](parameters.md) §2).

## 20. Analysis transform: window, alignment, filters

- **Situation.** The spectra that feed energies and tonality, the high-pass
  that feeds attack detection, and their time alignment with the coded
  granule.
- **What went wrong.** A wrong window formula. A window changed without
  measurement, although windows differ in exactly what tonality depends on.
  DC and top-line energies halved by mistake. A high-pass that did not reject
  DC. A coefficient table read one past its end, and a filter with an
  asymmetric tap layout. A miscomputed look-ahead. Window tables shared
  between concurrent encoders, a data race. Count: about 8.
- **Why it is hard.** Window and line normalisation shift every partition
  energy. A misaligned analysis gives thresholds for the wrong time, a
  pre-echo problem invisible on stationary tests.
- **How to tell.** A stationary tone on a line centre, between lines, at DC
  and at Nyquist gives the expected normalised energy. A click at a known
  sample is flagged in the slot of the window that codes it, and shows in the
  thresholds of the granule that codes it. Parallel initialisation gives the
  serial output.
- **Answered by.** The coded MDCT is the analysis
  ([spectral-analysis.md](spectral-analysis.md) §1), so thresholds describe
  the coded granule by construction. A symmetric attack filter
  ([block-switching.md](block-switching.md) §1.1). Alignment by rule and
  click test ([block-switching.md](block-switching.md) §1.4). No shared mutable state
  ([signal-path.md](signal-path.md) §5).

## 21. Perceptual entropy and bits

- **Situation.** PE per granule and channel sets the bit demand. It is
  normalised by a 19-tap smoothing filter over recent frames.
- **What went wrong.** The formula was revised to include the ATH, and again
  for M/S and reservoir decisions. Target bits exceeded the granule maximum.
  The smoothing normaliser went non-positive. The PE-to-bits conversion was
  not clamped before integer conversion. The smoothing's scope was lost and
  restored. Count: about 6.
- **Why it is hard.** PE is unbounded, it meets hard integer limits, and the
  smoothing filter can change sign on bursty material.
- **How to tell.** Full-scale noise at 32 kbps and silence give target bits
  within the granule limits and a finite, positive factor. Silence and noise
  alternating every frame give no non-positive normaliser.
- **Answered by.** A continuous PE cap
  ([perceptual-entropy.md](perceptual-entropy.md) §2), the `F ≤ 0` rule
  (§3), and budgets bounded by the mean, the reservoir and `PART2_3_MAX`
  before conversion ([reservoir.md](reservoir.md) §3).

## 22. The model's output scale is coupled to its consumer

- **Situation.** Thresholds handed to an allocator tuned against a
  particular model, and a model swapped or retuned under an unchanged
  allocator.
- **What went wrong.** A scaling of short thresholds had to be confined to
  one encoding mode because the other relied on the old definition. Moving a
  mode to a new model required retuning its offsets and constants. A noise
  shaping option made higher quality settings sound worse than faster ones,
  because the allocator had not been adapted to what the new model's measure
  meant. Count: 3.
- **Why it is hard.** The threshold is not a physical quantity. Its level is
  calibrated downstream, so a correct model can be wrong against another
  allocator.
- **How to tell.** Model and allocator are validated together on the metric
  corpus. Quality levels are monotonic: a slower level never scores worse.
- **Answered by.** [allowed-distortion.md](allowed-distortion.md)
  introduction, [ath.md](ath.md) §1 (calibration),
  [tests.md](tests.md) §4.

## 23. Temporal masking between short windows

- **Situation.** The allowed noise of a short window is raised toward the
  preceding window's, with a decay derived from its duration.
- **What went wrong.** Added as an experiment, then strengthened. Turned off
  in one mode because it worked against other tunings. Count: 4.
- **Why it is hard.** It pushes against the short pre-echo control across
  the same window boundary.
- **How to tell.** After a decaying click, the following windows' allowed
  noise decays at the stated rate and never exceeds the click window's.
- **Answered by.** [allowed-distortion.md](allowed-distortion.md) §3, applied
  after the pre-echo control.

## Tuning churn

Separate changes to a value or the shape of a curve. Values that moved this
often are tuning, and the spec treats them as parameters with a recommended
value ([parameters.md](parameters.md)).

| Parameter | Changes | Notes |
|---|---|---|
| Attack ratios (L/R/M and side) | ~8 | Values changed by more than an order of magnitude, partly because the measured quantity changed from energy to peak amplitude. Per-bitrate pairs. |
| Short-window level-change rule (ratio and level guard) | ~5 | Ratio 2 → 1.7; guard added and restructured; an alternative rule withdrawn. |
| ATH adaptation (curve, limits, start, loudness source) | ~18 | Entry 18. |
| ATH curve shape and per-bitrate ATH offset | ~10 | Several curve formulas; offsets −30 … +12 dB per bitrate, retuned at high bitrates. |
| Tonal ceiling (minval) | ~5 | Shape rewritten; clamp values changed; restricted by sample rate; made bitrate-dependent. |
| Masking addition law, tables, distance window | ~10 | Entry 13. |
| Tonality → masking offset table | ~4 | |
| Masking adjustment (global long/short, per band) | ~7 | |
| M/S fix strength | ~5 | The 2 dB L/R similarity condition did not change. |
| Pre-echo constants (2, 16; 0.8, 0.6, 0.3; 0.6) | ~3 | Values stable, combination logic changed (entry 10). |
| Spreading function shape and normalisation | ~6 | Entry 14. |
| Temporal masking strength | ~3 | Entry 23. |
