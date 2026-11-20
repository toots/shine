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
- [ ] Perceptual score in the report: ViSQOL (audio mode) or PEAQ.
- [x] Decode with both FFmpeg and mpg123 in the report, to catch streams
      only one decoder tolerates.
- [ ] Listening check before release for psychoacoustic changes.
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
- [ ] Bump the version: `AC_INIT` still says 3.1.1.

## Psychoacoustics

Each phase is fixed point, behind the quality setting, and measured with the
quality report before the next one starts.

- [ ] Decide the perceptual metric (ViSQOL proposed, PEAQ as alternative).
- [ ] Phase 1: masking model in the MDCT domain (band energies, spreading,
      absolute threshold of hearing, tonality), integer `log2`.
- [ ] Phase 2: noise allocation: outer loop raising scalefactors where noise
      exceeds the threshold. Needs real scalefactor coding: MPEG-1
      `slen`/`scfsi` and MPEG-2 LSF `scalefac_compress`, untested so far.
- [ ] Phase 3: restore the bit reservoir (`ResvMax` is always 0), with
      perceptual entropy driving allocation. Remove or define the
      `shine_ResvFrameBegin` declaration.
- [ ] Phase 4: block switching (short blocks) for transients.

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

- [ ] Run new benchmarks once the changes above are done: native `shineenc`
      against LAME (`README.md`, last run December 2022), the JavaScript
      package in Node.js and in browsers through `js/test/browser`
      (`js/README.md`), and speed against the 3.1.1 release, at each
      quality setting.
