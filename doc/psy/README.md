# Psychoacoustic model: specification

This specifies shine's psychoacoustic model: the masking thresholds,
perceptual entropy and block-switching decisions that the noise allocation,
the bit reservoir and short blocks are built on. It is the spec for the
Psychoacoustics section of `TODO.md`. It is detailed enough to implement and
test the model without any other source.

MUST, SHOULD and MAY are used as in RFC 2119. Each rule lives in one file,
and the others link to it.

## Frozen surface

- The MPEG-1/2/2.5 Layer III bitstream.
- The output of the lowest quality level: it does not run the model, and its
  golden hashes do not change.

Nothing the model produces is frozen. Output at the other quality levels is
bit-exact on every host ([fixed-point.md](fixed-point.md) §1) and pinned by
its own golden hashes once each phase lands.

## Phases

Each phase is fixed point, behind the quality setting, and lands with its
metric report before the next one starts.

| Phase | Parts |
|---|---|
| 1. Model | long analysis, partitions, spreading, tonality, threshold limits, ATH and its adaptation, M/S thresholds and decision, the PE formula |
| 2. Noise allocation | allowed distortion per band, band offsets, band ATH, the outer loop and scalefactor coding |
| 3. Reservoir | PE smoothing, the bit reservoir, the bits of a granule from its PE |
| 4. Short blocks | attack detection, block types, short analysis and thresholds, short pre-echo control, temporal masking between short windows |
| 5. Bandwidth | the lowpass cutoff by bitrate per channel, applied to the MDCT before the model |
| 6. Resampling | the encoding rate by bitrate per channel, the sample rate converter |

Every file and section names its phase.

## Documents

Read in this order:

1. [conventions.md](conventions.md): requirement words, units and the
   full-scale convention, notation, and the shared formulas (Bark, ATH
   curve, spreading function, demasking, interpolation, masking tables).
2. [signal-path.md](signal-path.md): where the model sits in the encoder, the
   look-ahead, the interface, state, initialisation.
3. [spectral-analysis.md](spectral-analysis.md): MDCT line energies, the
   tonality spectrum, partitions, spreading matrix, band sums, the optional
   FFT path.
4. [masking.md](masking.md): tonality, spreading with non-linear addition,
   pre-echo limit, tonal ceiling, masking lowering.
5. [ath.md](ath.md): threshold of hearing placement, per partition and per
   band, adaptation to loudness.
6. [stereo.md](stereo.md): M/S thresholds and the M/S decision.
7. [perceptual-entropy.md](perceptual-entropy.md): PE and its smoothing.
8. [block-switching.md](block-switching.md): attack detection, coupling,
   block types, short-block pre-echo control.
9. [allowed-distortion.md](allowed-distortion.md): the boundary with the
   noise allocation, temporal masking between short windows.
10. [noise-allocation.md](noise-allocation.md): quantisation, noise, the
    inner and outer loops, scalefactor coding.
11. [bandwidth.md](bandwidth.md): the lowpass cutoff and filter.
    [resampling.md](resampling.md): the encoding rate and the sample rate
    converter.
12. [reservoir.md](reservoir.md): the reservoir in the bitstream, its limit,
    the bits of a granule, delayed output.
13. [parameters.md](parameters.md): every constant, essential or tuning, and
    the per-bitrate set.
14. [fixed-point.md](fixed-point.md): integer arithmetic, representations,
    generated tables.
15. [tests.md](tests.md): invariants, the development oracle, the metric
    gate.
16. [known-complexity.md](known-complexity.md): known pitfalls, each linked
    to the rule that answers it, and each a conformance check.

## Analysis domain

The model works on the MDCT the encoder codes: it costs no extra transform,
and its thresholds describe the coded granule by construction. An FFT path
MAY replace it ([spectral-analysis.md](spectral-analysis.md) §6). The
attack detector works in the time domain either way.

## Calibration

The thresholds are not absolute physical quantities. Their level rests on an
assumed playback level ([ath.md](ath.md) §1) and on offsets tuned together
with the allocator ([allowed-distortion.md](allowed-distortion.md)). Every
**tuning** parameter is a recommended starting point, to be confirmed by the
metric report ([tests.md](tests.md) §4), whose perceptual score comes from
PEAQ. Rules that need a perceptual judgement say so.
