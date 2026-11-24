# Noise allocation

Phase 2. For each granule and coded channel, in the stereo mode chosen for
the frame, the allocation chooses the quantiser step size and the
scalefactors so that the quantisation noise of each band stays under its
allowed distortion `xmin` ([allowed-distortion.md](allowed-distortion.md)
§2) wherever the bit budget allows, and is spread in proportion to it where
it does not.

§1 to §5 are written for long blocks; a SHORT granule (phase 4) is
allocated by the same rules on the bands of §6. START and STOP granules are
long. The bits of a granule come from the reservoir ([reservoir.md](reservoir.md)
§3; the frame's mean before phase 3); §3 shares them between its channels.

## 1. Quantisation

A decoder reconstructs line `k` of long band `sb` as

```
xr'[k] = sign(ix[k]) · |ix[k]|^(4/3) · 2^((global_gain − 210)/4)
                     · 2^(−(1 + scalefac_scale)/2 · sf[sb])
```

(ISO 11172-3 §2.4.3.4, 13818-3 §2.4.3.4, with `preflag` 0). `sf[sb]` is the
band's scalefactor and `scalefac_scale` ∈ {0, 1} the granule's scalefactor
step: a scalefactor amplifies its band by 1.5 dB, or 3 dB.

The encoder quantises each band with the step that reconstruction implies,
the granule's step size `q` lowered by the band's amplification:

```
q_sb = q − 2 · (1 + scalefac_scale) · sf[sb]        in quarter steps
ix[k] = nint(|xr[k] / 2^(q_sb/4)|^(3/4) − 0.0946)
```

`xr` is in the encoder's coefficient scale, which `q` and `global_gain`
relate by a constant offset. The encoder SHOULD round as this formula does.
Rounding the scaled coefficient to an integer before taking its power moves
the boundaries of small values (the one between 1 and 2 from 1.86 to 1.5),
which costs bits and adds error; an encoder MUST decide the values up to
`QUANT_EXACT_MAX` = 64 by the formula, within the resolution of its scaled
coefficient. Above that, rounding first moves a boundary by less than 1% of
the step. The noise of §2 is measured on the `ix` produced. A band MUST NOT be amplified past the range
where the encoder represents its step exactly: an approximated step would
be decoded with the exact one, and the band would be reconstructed at the
wrong level.

Every `ix` MUST be at most `IX_MAX` = 8192, within the largest value the
Huffman codes represent (15 plus the 13 linbits of the widest table, 8206).

## 2. Noise

The noise of band `sb` is the energy of the reconstruction error, in the
energy unit of [conventions.md](conventions.md):

```
noise[sb] = K · Σ_k (|xr[k]| − |ix[k]|^(4/3) · 2^(q_sb/4))²
```

A band is **over** when `noise[sb] > xmin[sb]` and `xmin[sb] < en0[sb]`,
with `en0` the band's energy ([allowed-distortion.md](allowed-distortion.md)
§2). Only the bands that have an `xmin` count (long sfbs 0..20, 0..16 at
fs ≤ 8 kHz); sfb21 has no scalefactor and is never over.

A band whose allowed distortion is its whole energy, below the threshold of
hearing or fully masked, may be coded as silence. Its noise is then its
energy, exactly its allowed distortion: without the second condition, the
comparison of two equal quantities would be decided by rounding, and the
loop would spend bits amplifying bands that need none.

The distortion of a quantisation is

```
D = Σ over the bands that are over of log2(noise[sb] / xmin[sb])
```

the total excess of noise over the allowed distortion, in octaves of energy.
`D = 0` when no band is over.

## 3. Inner loop: fitting the bits

### 3.1 The budget of a channel

A granule of two coded channels shares its bits between them in proportion
to their perceptual entropy ([perceptual-entropy.md](perceptual-entropy.md)),
the model's estimate of the bits each one needs:

```
max_bits[0] = int(granule_bits · pe[0] / (pe[0] + pe[1]))
max_bits[1] = granule_bits − max_bits[0]
```

`pe` is that of the coded channels: `pe_MS` in an M/S frame
([stereo.md](stereo.md) §3). `granule_bits` is what the reservoir gives the
granule for both channels ([reservoir.md](reservoir.md) §3). Neither share may exceed `PART2_3_MAX` = 4095,
the largest `part2_3_length`: the excess goes to the other channel. A mono
granule keeps all its bits.

An equal split wastes on the channel that needs few bits what the other
one lacks. In an M/S frame the side channel often needs a fraction of what
the mid does, and the bits it leaves unused are stuffing. PE never falls
below its base, so no channel gets nothing.

### 3.2 The step size

Given the scalefactors, the inner loop chooses a step size `q` whose granule
fits the budget while the next smaller step does not:

```
bits(q) = part2_length + huffman_bits(ix(q)) ≤ max_bits < bits(q − 1)
```

`part2_length` is the scalefactor bits of §5, `huffman_bits` the count of the
encoder's code-table selection for `ix(q)`. `q` also MUST keep every `ix` at
most `IX_MAX`; a step that does not counts as not fitting. Bits fall as `q`
rises, though not strictly: in the rare granule where several such steps
exist, any of them is valid, so the search MAY be a bisection. If no `q` in
the encoder's range fits, the attempt is not made (§4).

## 4. Outer loop: shaping the noise

```
sf[*] = 0;  scalefac_scale = 0
best = none
for attempt = 0, 1, ...:
    if part2_length > max_bits: stop
    inner loop (§3)
    if the granule does not fit at the largest step size: stop
    measure noise (§2)
    if best is none or D < D(best): best = this attempt
    if no band is over: stop
    if attempt + 1 = OUTER_ATTEMPTS: stop
    for each band that is over and can be amplified (below):
        sf[sb] += 1
    if no band was amplified: stop
    if every band that can be over now has sf > 0: stop
use best
```

- **Amplified once per attempt.** Every band that is over gains 1.5 dB (or
  3 dB) at a time. The inner loop then raises `q` to pay for it, which adds
  noise to every band, so the attempt can be worse than the one before: the
  loop keeps the best one, not the last.
- **All bands amplified** is a uniform gain the global step size already
  expresses. The loop stops rather than walking the step size down by
  scalefactors. Only the bands that can be over count: those whose `xmin`
  is below their energy (§2). A band allowed its whole energy, or with no
  energy, is never amplified, and would otherwise keep the loop going to
  `OUTER_ATTEMPTS`.
- **A band can be amplified** while its scalefactor stays within the largest
  value its format codes (§5) and its step within the range of §1. When a
  band reaches its largest scalefactor at `scalefac_scale = 0`, the granule
  switches to `scalefac_scale = 1` and every scalefactor becomes
  `(sf + 1) / 2`, rounded down. The amplifications are then kept or raised by
  at most 1.5 dB, and the loop continues with twice the range. At
  `scalefac_scale = 1`, a band at its largest scalefactor is no longer
  amplified.
- **Budget.** Amplified bands cost scalefactor bits and keep lines non-zero
  at large step sizes. An attempt whose scalefactors alone, or whose
  coefficients at the largest step size, exceed the budget is not made.
- **Ties** in `D` keep the earlier attempt. Attempt 0 has every scalefactor
  at 0, so the allocation never ends with more distortion than a plain step
  size search.

`OUTER_ATTEMPTS` bounds the number of inner loops per granule, and so the
encoding time. It is a parameter of the quality level
([parameters.md](parameters.md) §2). Without it the loop still ends, since
every attempt raises a scalefactor and scalefactors are bounded.

A granule whose coefficients are all 0 skips the allocation: its step size
and scalefactors are 0.

### 4.1 Bits a channel does not need (phase 3)

When the best attempt leaves no band over, its noise is under the allowed
distortion everywhere, and the bits that would push it further under are
worth more to a later granule. With `room = RESV_MAX − R`, `R` the
reservoir when the channel is allocated ([reservoir.md](reservoir.md) §2,
§3), and `used` the bits of the best attempt, the channel keeps the
scalefactors of that attempt and, when `room > 0`, raises its step size to
a `q` at which

- no band is over, and
- it uses at least `used − room` bits, and no more than `used`: a larger
  step size does not always cost fewer bits,

and at which `q + 1` fails one of the two, or is past the largest step
size. This is what a bisection from the attempt's step size guarantees.
Neither condition is monotonic in `q`, so it is not always the largest such
step size.

A channel leaves at most what the reservoir lacked before the granule drew
on it. Counting from the channel's mean instead, so that a channel given
more than its mean gives all of it back, scored worse
([tests.md](tests.md) §4): the bound is the measured one. A channel with a
band over leaves nothing: the reservoir is filled only with bits whose
absence the model cannot tell.

## 5. Scalefactor coding

`scfsi` is 0 in every band: each granule sends its own scalefactors.
`preflag` is 0. Both MAY be used later, to save bits; they do not change
what this section asks of the scalefactors.

The encoder MUST choose the scalefactor coding with the fewest bits that
codes every scalefactor of the granule. Its bits are `part2_length`.

**MPEG-1.** `scalefac_compress` selects `(slen1, slen2)`: `slen1` bits for
each of bands 0..10, `slen2` bits for each of bands 11..20
(ISO 11172-3 §2.4.2.7):

| `scalefac_compress` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `slen1` | 0 | 0 | 0 | 0 | 3 | 1 | 1 | 1 | 2 | 2 | 2 | 3 | 3 | 3 | 4 | 4 |
| `slen2` | 0 | 1 | 2 | 3 | 0 | 1 | 2 | 3 | 1 | 2 | 3 | 1 | 2 | 3 | 2 | 3 |

`part2_length = 11·slen1 + 10·slen2`. The largest scalefactors are 15 in
bands 0..10 and 7 in bands 11..20.

**MPEG-2 and 2.5** (ISO 13818-3 §2.4.3.2, without intensity stereo, `preflag`
0). The 21 bands form four groups of 6, 5, 5 and 5 bands, sent with
`slen1..slen4` bits each, where `slen1, slen2` ∈ 0..4 and `slen3, slen4` ∈
0..3:

```
scalefac_compress = ((5·slen1 + slen2) << 4) + (slen3 << 2) + slen4      (< 400)
part2_length = 6·slen1 + 5·slen2 + 5·slen3 + 5·slen4
```

The largest scalefactors are 15 in bands 0..10 and 7 in bands 11..20, as for
MPEG-1. The forms of `scalefac_compress` from 400 up, which carry `preflag`
or other group sizes, are not used.

At fs ≤ 8 kHz, bands 17..20 have no `xmin` and their scalefactors stay 0.

## 6. Short blocks (phase 4)

A SHORT granule codes three windows of 192 lines. Its 576 coefficients are
in coded order, by short sfb, then window, then line (ISO 11172-3
§2.4.3.4.10.1). The allocation works on `3 · 13` bands: band `3·sb + j` is
short sfb `sb` of window `j`, and carries that window's `xmin` and energy
([allowed-distortion.md](allowed-distortion.md) §2, §3). The rules of §1 to
§4 apply unchanged on these bands, in this order.

- **Step of a band:** `q − 2·(1 + scalefac_scale)·sf[sb][j]`, as in §1.
  `subblock_gain` is 0 for every window: a window's level is set by its
  scalefactors alone. An encoder MAY use `subblock_gain`; it is not
  specified here.
- **Step range.** The short transform keeps more headroom than the long
  one, which a SHORT granule's `global_gain` gives back as a fixed number
  of extra steps. Its largest step size is smaller by that number, so that
  `global_gain` stays within 255.
- **Bands with an allowed distortion:** short sfbs `0..11` (`0..8` at fs ≤
  8 kHz), each window. sfb12 has no scalefactor and is never amplified.
- **Largest scalefactor:** 15 in short sfbs `0..5`, 7 in sfbs `6..11`, in
  both formats.
- **Coding.** MPEG-1: sfbs `0..5` on `slen1` bits and `6..11` on `slen2`,
  each with its three windows, `part2_length = 18·(slen1 + slen2)`, the
  cheapest `scalefac_compress` among the 16 that code every value. MPEG-2
  and 2.5 (without intensity stereo, ISO 13818-3 §2.4.3.2): the four
  groups are sfbs `0..2`, `3..5`, `6..8` and `9..11`, each with its three
  windows, on `slen1..slen4` bits; `scalefac_compress` is formed as in §5
  and `part2_length = 9·(slen1 + slen2 + slen3 + slen4)`.
- **Huffman regions.** Region 0 ends after 3 short sfbs (36 lines); there
  is no region 2 (ISO 11172-3 §2.4.2.7).

§4.1 applies to a SHORT granule as to a long one.

## 7. Determinism

The allocation is integer arithmetic like the model
([fixed-point.md](fixed-point.md) §1). Noise and `xmin` are compared in the
same representation, and `D` is a sum of the same `log2` the model uses, so
the choice of the best attempt is the same on every host.
