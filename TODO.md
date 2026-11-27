# TODO

Work towards the next major release. Breaking the API is acceptable.

## Quality gates

Every change is one of three kinds, each with a gate that fails when the
change does more than it claims. The commit message says which kind it is.

- **Bit-exact** (formatting, dead code, refactors, API renames): golden hashes
  of the encoded stream, for every configuration and synthetic signal, are
  unchanged.
- **Decode-exact** (stream changes that decode identically, e.g. `scfsi`
  flags while scalefactors are all zero): golden hashes of the decoded PCM
  are unchanged.
- **Audio-changing**: a metric report on a corpus, compared with the
  previous commit: output level, gain-corrected SNR, perceptual score. No
  regression beyond tolerance on any item. Goldens are updated in the same
  commit, with the numbers in the message.

- [x] `tests/golden`: hash the stream of every configuration for a set of
      deterministic synthetic signals (tones, sweep, noise, transients,
      silence, full scale) against `tests/golden.txt`, with an environment
      variable to regenerate.
- [ ] Decoded-PCM goldens, using FFmpeg when available, skipped otherwise.
- [x] Quality report script (`tools/quality-report.sh`): level and
      gain-corrected SNR per item, on synthetic signals plus local music
      (e.g. FATE samples), with LAME at the same bitrate as reference. Not
      run in CI.
- [x] Perceptual score in the report: the 2f-model, an estimate of the
      MUSHRA score from basic PEAQ, with GstPEAQ, validated on the ODAQ
      listening test; the ODG is reported beside it.
- [x] Gate script (`tools/quality-gate.sh`) on two reports, and
      `tools/quality-corpus.sh` to derive every sample rate family from
      music files.
- [x] Decode with both FFmpeg and mpg123 in the report, to catch streams
      only one decoder tolerates.
- [ ] Listening check before release for psychoacoustic changes. Round 4
      tests the retuned cutoffs. Not heard yet: the spreading as a plain
      sum and MASK_ADJUST +3 to +6 dB at 32 to 64 kbps; the stereo image of
      wide mixes at 64 to 96 kbps, which the score hardly sees.
- [x] Confirm the goldens are identical on Linux, macOS and other
      architectures: CI checks x86-64 and arm64 natively, armv7 and s390x
      emulated.

## Correctness

- [x] Output is +1.02 dB too loud (gain 1.1246, about 9/8) at every rate and
      bitrate: find the factor in the polyphase/MDCT scaling, then pin the
      level in a test (`tests/level`). Full-scale input clips in the decoder.
- [x] The fixed-point chain has no headroom: MDCT sums of full scale input
      in phase across subbands exceed 32 bits and are saturated. Fix with
      the gain above.
- [x] 16 kHz scale factor band table typos (`tables.c`): `45` must be `54`,
      `248` must be `284`.
- [x] Prefix the global `samplerates` and `bitrates` symbols with `shine_`;
      make `granules_per_frame` const.

- [x] Quality 0 counted and wrote the Huffman tables of a granule with no
      big values from the region addresses of the previous granule: its
      `table_select` held stale values. The addresses are now cleared.

## Fixed point

The library used floating point while encoding (arm64 disassembly). It uses
none now, checked in CI by `tools/check-no-float.sh`.

- [x] `calc_scfsi` / `calc_xmin`: `log()` and `fdiv` every granule, on
      all-zero thresholds. Delete with the dead psychoacoustic stubs.
- [x] Frame padding: `slot_lag` / `frac_slots_per_frame` are doubles,
      updated every frame. Use an integer accumulator.
- [x] `shine_max_reservoir_bits`: `pe * 3.1`. Dead while the reservoir is
      disabled; integer when restored.
- [x] `quantize()` beyond the lookup table: double `sqrt(sqrt(x) * x)`,
      about 1 coefficient in 10^4-10^5 on music. Use an integer x^(3/4).
- [x] Generate or precompute the init tables (`cos`, `sin`, `pow`, `modf`)
      without libm, for bit-exact output across platforms.
- [x] CI check that the encoding path has no floating point, e.g. building
      the library with `-mgeneral-regs-only` on arm64.

## API (major release)

- [x] Prefix the public enums and constants (`NONE`, `MONO`, `STEREO`,
      `MPEG_I`, `PCM_MONO`, `LAYER_III`, ...) with `SHINE_`.
- [x] Take input samples as `const`.
- [x] Quality setting in the config, added with the psychoacoustic model,
      which is the first thing it would select. Its lowest level keeps
      today's bit-exact fast path.
- [x] Bump the version to 4.0.0.

## Psychoacoustics

Each phase is fixed point, behind the quality setting, and measured with the
quality report before the next one starts.

- [x] Decide the perceptual metric: the 2f-model (`doc/psy/tests.md` §4).
- [x] Phase 1: masking model in the MDCT domain (band energies, spreading,
      absolute threshold of hearing, tonality), integer `log2`, M/S.
- [x] Phase 2: noise allocation: outer loop raising scalefactors where noise
      exceeds the threshold (`doc/psy/noise-allocation.md`), with MPEG-1
      `slen` and MPEG-2 LSF `scalefac_compress` coding, and the bits of a
      granule shared between its channels by perceptual entropy.
- [x] Retune the per-bitrate table on the 2f score, on two thirds of the
      corpus sources with one third held out: cutoffs, `MASK_ADJUST` and
      `MSFIX`. `ATH_LOWER` and `ATH_CURVE` move no score by more than 0.1.
- [x] 96 kbps stereo trailed LAME by 6 points: its stream is at 32 kHz and
      was cut at 14.5 kHz. A stream at 32 kHz or below now codes its whole
      band (`bandwidth.md` §1); the cutoff only chooses its rate.
- [x] `TONALITY_SCALE` tuned on the 2f score: 15.
- [x] A granule's bits are shared between its channels by PE to a power
      set per bitrate (`noise-allocation.md` §3.1), and `MASK_ADJUST` has a
      value per channel count.
- [x] The cheapest Huffman region split of the final quantisation
      (`noise-allocation.md` §4.2): 1.1 to 1.7% of the bits.
- [x] `preflag` on the long granules whose upper bands are audible
      (`noise-allocation.md` §5.1), in MPEG-2 and 2.5 up to 40 kbps.
- [ ] Measured and not worth their code: `scfsi` would save 0.1% of the
      bits; a tax on steady granules to fill the reservoir, more outer
      attempts, a closed-loop split of the bits between channels, a wider
      quantiser dead zone, a smoothing of the thresholds over time and a
      margin under the thresholds before bits are given back all lose or
      gain nothing.
- [ ] MPEG-2.5 rates as resampling targets: 11.025 kHz gains 1.5 to 1.9
      at 16 and 24 kbps over 16 kHz. `RESAMPLE_MIN_RATE` is a compatibility
      choice (`resampling.md` §1).
- [ ] Stereo scores 2.4 higher at 64 kbps and 1.2 to 2.0 higher at
      112 kbps on a 32 kHz stream, with an item or two past the tolerance
      (a violin, down 6.5 at 64 kbps; clapping and claves, down 6 to 6.7 at
      112): for a listening round. Mono keeps its rates.
- [ ] Castanets and claves at 128 kbps trail LAME by 6 and 12 points
      (84.9 against 90.6, 84.6 against 96.8), after the start of SHORT
      granules half way to an even noise-to-mask ratio
      (`noise-allocation.md` §6.1) gained 4 and 3. LAME also gives its
      attack frames more bits: a reservoir filled by a tax on steady
      granules gains 3 more on these items and costs every other 0.2 to
      0.7.
- [ ] Dropping the count1 values that cost more than they are worth.
- [x] Masking spread as a plain sum of energies, in the linear domain.
- [x] The step size search starts where the previous attempt ended.
- [x] Resampling at low bitrates, in the library (`doc/psy/resampling.md`).
- [x] The quantiser rounded the scaled coefficient before taking its 3/4
      power, which moved the boundary between 1 and 2 from 1.86 to 1.5.
      Values up to 64 now follow the exact rule, at both quality levels.
- [x] Huffman table choice: the search stopped at table 13 for any value
      below 15, so tables 1..12 were never used, and a largest value of 15
      went to the escape tables. Each region now gets the cheapest table
      that codes it.
- [x] Phase 3: the bit reservoir, at quality 1 (`doc/psy/reservoir.md`):
      a granule draws on it by its perceptual entropy, and a channel with no
      band over leaves the bits it does not need.
- [x] Phase 4: block switching (short blocks) for transients: attack
      detection with a 256-sample look-ahead, start, short and stop windows,
      the short-window model and noise shaping on the short bands.
- [x] The float oracle served phases 1 to 3 and is deleted, as
      `doc/psy/tests.md` §3 asks once a phase has its goldens and metric
      report; formula checks in `tests/psy_model.c` cover phase 4.
- [ ] Phase 5 lowpass (`doc/psy/bandwidth.md`): cutoffs retuned on the 2f
      score, which follows band limitation closely and may favour bandwidth
      more than listeners do. Round 4 preferred the retuned cutoffs from 48
      to 80 kbps. The full band of streams at 32 kHz and below is not
      heard yet, and under 32 kbps the score is outside the range it was
      compared with listeners in.
- [ ] `subblock_gain` is always 0 (`doc/psy/noise-allocation.md` §6).
- [ ] A listening round on transients with the reservoir and short blocks,
      and on the items the metric gate flagged.
- [ ] A listening round on the stereo image of wide mixes up to 96 kbps:
      `PE_SPLIT_POWER` takes bits from the side channel, and the score
      rates each channel by itself. It is capped at 2.4; the score asks
      for 3.2 under 64 kbps.
- [ ] A build option to leave the model out of the library, for small
      targets: quality 0 does not use it.

## Speed

- [x] The filter bank built for AVX2 on x86-64, chosen at run time
      (`src/lib/kernels.c`): baseline x86-64 has no signed widening vector
      multiply. arm64 compilers vectorize it with NEON already.
- [ ] GCC builds run 20 to 35% slower than clang builds on x86-64, with or
      without AVX2: find which loops.
- [ ] Hand-written AVX2 for the filter and MDCT, still 24% of quality 1 on
      x86-64 with GCC.

## Cleanup and formatting

Bit-exact; formatting commits separate from functional ones.

- [x] Add `.clang-format` (LLVM, what the code already follows), normalize
      CRLF line endings (12 files), reformat, check formatting in CI.
- [x] Remove `register`, `__BORLANDC__` branches, RISC OS leftovers.
      `README.old` stays, for its history.
- [x] Fix `-Wextra` warnings (unused parameters, sign comparisons, `rzero`).

## CI and packaging

- [x] Run the tests on 32-bit and big endian hosts: armv7 and s390x under
      QEMU. The ARM and MIPS assembly is removed and byte order handled
      portably, so no code is specific to them.
- [x] Update `actions/checkout`.

## Release

- [x] Native `shineenc` against LAME, at each quality level (`README.md`,
      November 2026).
- [ ] The JavaScript package in Node.js and in browsers through
      `js/test/browser` (`js/README.md`), and speed against the 3.1.1
      release.
