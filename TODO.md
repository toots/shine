# TODO

Work towards the next major release. Breaking the API is acceptable. Done
work is in `ChangeLog`.

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
  previous commit: output level, gain-corrected SNR, perceptual score
  (`doc/psy/tests.md` §4). No regression beyond tolerance on any item.
  Goldens are updated in the same commit, with the numbers in the message.

Tools: `tools/quality-report.sh`, `tools/quality-gate.sh`,
`tools/quality-corpus.sh`.

- [ ] Decoded-PCM goldens, using FFmpeg when available, skipped otherwise.

## Listening before release

The score is a measure, not listeners (`doc/psy/tests.md` §4.1). Not heard
yet:

- [ ] The retune on the 2f score: the spreading as a plain sum,
      `MASK_ADJUST` by channel coding and by bits per sample, `preflag`,
      the start of SHORT granules, the 32 kHz stream of joint stereo at
      64 kbps.
- [ ] The full band of streams at 32 kHz and below (`bandwidth.md` §1),
      and the cutoffs under 32 kbps, where the score is outside the range it
      was compared with listeners in.
- [ ] The stereo image of wide mixes up to 96 kbps: `PE_SPLIT_POWER` takes
      bits from the side channel, and the score rates each channel by
      itself. It is capped at 2.4; the score alone asks for 3.2 under
      64 kbps and 2.8 at 64.
- [ ] Transients with the reservoir and short blocks, and the items the
      metric gate flagged.

## Psychoacoustics

- [ ] Stereo scores 1.2 to 2.0 higher at 112 kbps on a 32 kHz stream, with
      clapping and claves down 6 to 6.7: for a listening round.
- [ ] MPEG-2.5 rates as resampling targets: 11.025 kHz gains 1.5 to 1.9 at
      16 and 24 kbps over 16 kHz. `RESAMPLE_MIN_RATE` is a compatibility
      choice (`resampling.md` §1).
- [ ] Castanets and claves at 128 kbps trail LAME by 6 and 12 points (84.9
      against 90.6, 84.6 against 96.8). LAME gives its attack frames more
      bits: a reservoir filled by a tax on steady granules gains 3 on these
      items and costs every other 0.2 to 0.7.
- [ ] Native 11.025 to 22.05 kHz joint stereo from 1.45 bits per sample and
      channel scores 0.4 to 0.7 higher with `MASK_ADJUST` 1.5 to 2 dB lower
      (11.025 kHz at 32 kbps, 16 kHz at 48, 22.05 kHz at 64 and 96), but
      16 kHz at 64 kbps then loses 9 on one item: no rule fits yet.
- [ ] Dropping the count1 values that cost more than they are worth.
- [ ] `subblock_gain` is always 0 (`noise-allocation.md` §6).
- [ ] A build option to leave the model out of the library, for small
      targets: quality 0 does not use it.

Measured and not worth their code: `scfsi` (0.1% of the bits); a tax on
steady granules to fill the reservoir; more or fewer outer attempts; a
closed-loop split of the bits between channels; a wider quantiser dead
zone; smoothing the thresholds over time; a margin under the thresholds
before bits are given back; a direct allocation without the outer loop; a
lowpass on the L/R frames of the 32 kHz stream at 64 kbps. Rows 64 and 96
are at an optimum of every per-row parameter in joint stereo (masking and
ATH offsets, `MSFIX`, the M/S decision, spreading, tonality, reservoir,
attempts, band tilt).

## Speed

- [ ] GCC 13 builds run 24% slower than clang 18 builds at quality 0 on
      x86-64, and 7% slower at quality 1. At `-O2` GCC's cost model leaves
      loops scalar that clang vectorises; `-fvect-cost-model=dynamic`
      vectorises them.

## Release

- [ ] The JavaScript package in Node.js and in browsers through
      `js/test/browser` (`js/README.md`), and speed against the 3.1.1
      release.
