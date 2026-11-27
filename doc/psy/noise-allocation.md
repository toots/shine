# Noise allocation

For each granule and coded channel, in the stereo mode chosen for
the frame, the allocation chooses the quantiser step size and the
scalefactors so that the quantisation noise of each band stays under its
allowed distortion `xmin` ([allowed-distortion.md](allowed-distortion.md)
§2) wherever the bit budget allows, and is spread in proportion to it where
it does not.

§1 to §5 are written for long blocks; a SHORT granule is
allocated by the same rules on the bands of §6. START and STOP granules are
long. The bits of a granule come from the reservoir ([reservoir.md](reservoir.md)
§3); §3 shares them between its channels.

## 1. Quantisation

A decoder reconstructs line `k` of long band `sb` as

```
xr'[k] = sign(ix[k]) · |ix[k]|^(4/3) · 2^((global_gain − 210)/4)
                     · 2^(−(1 + scalefac_scale)/2 · (sf[sb] + preflag · pretab[sb]))
```

(ISO 11172-3 §2.4.3.4, 13818-3 §2.4.3.4). `sf[sb]` is the band's
scalefactor and `scalefac_scale` ∈ {0, 1} the granule's scalefactor step: a
scalefactor amplifies its band by 1.5 dB, or 3 dB. `preflag` ∈ {0, 1} adds
the table `pretab` of ISO 11172-3 (table B.6) to the scalefactors of a
long granule, at the cost of its one bit (§5.1):

| long sfb | 0–10 | 11–14 | 15–16 | 17–19 | 20 | 21 |
|---|---|---|---|---|---|---|
| `pretab` | 0 | 1 | 2 | 3 | 2 | 0 |

The encoder quantises each band with the step that reconstruction implies,
the granule's step size `q` lowered by the band's amplification:

```
q_sb = q − 2 · (1 + scalefac_scale) · (sf[sb] + preflag · pretab[sb])        in quarter steps
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

A granule of two coded channels shares its bits between them by their
perceptual entropy ([perceptual-entropy.md](perceptual-entropy.md)), the
model's estimate of the bits each one needs, raised to a power `p`: in an
M/S frame `PE_SPLIT_POWER`, a parameter of the bitrate
([parameters.md](parameters.md) §1), and 1 in a frame coded as left and
right:

```
w[c] = pe[c]^p
max_bits[0] = int(granule_bits · w[0] / (w[0] + w[1]))
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

A power above 1 gives the channel that needs more a larger share than its
PE: when bits are short, PE underestimates how much more the mid channel
gains from a bit than the side. Left and right have no such imbalance, and
share in proportion to PE.

With a power of 1 an encoder MUST compute the share exactly. With another
power the weights come from the model's `log2` and its inverse
([fixed-point.md](fixed-point.md)), and the share MUST be within 2 bits of
the formula.

`PE_SPLIT_POWER` is at most 2.4. A larger power takes more bits from the
side channel, and the score, which rates each channel by itself, does not
hear the stereo image ([tests.md](tests.md) §4): a larger value MUST be
confirmed by listening.

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
  at 0, so the allocation never ends with more distortion than a step size
  search with the granule's `preflag` (§5.1).

`OUTER_ATTEMPTS` bounds the number of inner loops per granule, and so the
encoding time. It is a parameter of the quality level
([parameters.md](parameters.md) §2). Without it the loop still ends, since
every attempt raises a scalefactor and scalefactors are bounded.

A granule whose coefficients are all 0 skips the allocation: its step size
and scalefactors are 0.

### 4.1 Bits a channel does not need

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
on it, **as measured**: a bound counted from the channel's mean, which
gives back all of a share above the mean, scores lower
([tests.md](tests.md) §4). A channel with a band over leaves nothing: the
reservoir is filled only with bits whose absence the model cannot tell.

### 4.2 Huffman regions of the chosen quantisation

A long granule without window switching divides its big values into three
regions at scalefactor band edges, set by `region0_count` ∈ 0..15 and
`region1_count` ∈ 0..7, each region with its own code table
(ISO 11172-3 §2.4.2.7). The split does not change what is decoded, only
the bits it takes.

The inner loop (§3.2) MAY count `huffman_bits` with any split. For the
quantisation the allocation ends with, after §4.1, the encoder MUST send
the split whose regions, each with its cheapest table, take the fewest
bits among all pairs (`region0_count`, `region1_count`); a tie keeps the
smallest `region0_count`, then the smallest `region1_count`. A boundary
past the big values is taken at their end. `part2_3_length` counts the
granule with that split, and the bits it saves against the count of the
loop stay in the reservoir.

START, STOP and SHORT granules have fixed boundaries (§6): nothing to
choose.

The search is made once per channel, not in the loop: refitting the step
size to the split inside the loop saves no more bits.

## 5. Scalefactor coding

`scfsi` is 0 in every band: each granule sends its own scalefactors. It
MAY be used later, to save bits; it does not change what this section asks
of the scalefactors. `preflag` is chosen by §5.1, and the scalefactors
coded are those of §4, without `pretab`.

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

**MPEG-2 and 2.5** (ISO 13818-3 §2.4.3.2, without intensity stereo). With
`preflag` 0, the 21 bands form four groups of 6, 5, 5 and 5 bands, sent with
`slen1..slen4` bits each, where `slen1, slen2` ∈ 0..4 and `slen3, slen4` ∈
0..3:

```
scalefac_compress = ((5·slen1 + slen2) << 4) + (slen3 << 2) + slen4      (< 400)
part2_length = 6·slen1 + 5·slen2 + 5·slen3 + 5·slen4
```

The largest scalefactors are 15 in bands 0..10 and 7 in bands 11..20, as for
MPEG-1.

With `preflag` (§5.1), the form from 500 up: bands 0..10 are sent with
`slen1` ∈ 0..3 bits and bands 11..20 with `slen2` ∈ 0..2:

```
scalefac_compress = 500 + 3·slen1 + slen2
part2_length = 11·slen1 + 10·slen2
```

and the largest scalefactors are 7 in bands 0..10 and 3 in bands 11..20.
The forms from 400 to 499, with other group sizes, are not used.

At fs ≤ 8 kHz, bands 17..20 have no `xmin` and their scalefactors stay 0.

### 5.1 `preflag`

A long granule is coded with `preflag` = 1 when, of its bands from 11 up
that have an allowed distortion, at least `PREFLAG_SHARE` = 0.8 can be
over (§2: `xmin[sb] < en0[sb]`), and with 0 otherwise; in an MPEG-2 or 2.5
stream, only at a bitrate up to `PREFLAG_LSF_KBPS` = 40. It is chosen
before the outer loop, which then runs from scalefactors 0 with the steps
of §1.

`pretab` amplifies the upper half of the spectrum by 1 to 3 scalefactor
steps for one bit. The loop reaches the same steps by raising ten
scalefactors, which it only does for bands that are over, and pays for in
`part2_length`. Starting from them leaves less noise in the upper bands of
every granule, those the allowed distortion already covers included. A
granule whose upper bands are under the threshold of hearing or fully
masked has nothing to gain there, and loses the bits: hence the share of
bands that can be over.

SHORT granules have no `preflag`.

In MPEG-2 and 2.5 `preflag` is carried by `scalefac_compress` (§5), in a
form that codes smaller scalefactors: the loop has less room to shape the
noise. That only pays at low bitrates, where few bands can be raised
anyway (`PREFLAG_LSF_KBPS`).

## 6. Short blocks

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

### 6.1 The scalefactors a SHORT granule starts from

In an MPEG-1 stream, the outer loop of a SHORT granule does not start from
scalefactors 0. For
each band that can be over (§2), `need[b]` is the largest step at which
the band alone, quantised at that step, keeps its noise within its allowed
distortion, found by bisection over the encoder's range of steps: the
lowest step counts as keeping it, a step past the largest as not, and a
step too small for `IX_MAX` as keeping it. With `reference` the largest
`need` of the granule and `u = 2·(1 + scalefac_scale)`:

```
sf[b] = (reference − need[b] + u) / (2·u)        integer division
```

`SHORT_START` = 1/2 of the amplification that would put every band as far from its
allowed distortion as the band that needs least, rounded to the nearest
scalefactor. `scalefac_scale` is 0, unless a scalefactor then passes its
largest value: they are computed again with 1, and what still passes is
set to the largest value. A band that cannot be over starts at 0. Attempt
0 of §4 has these scalefactors; if the granule does not fit its bits with
them at any step, or their coding alone exceeds its bits, it starts from
scalefactors 0 instead.

The loop of §4 raises every band that is over by the same step, and stops
when all of them have been raised: when the whole granule is over, as
around an attack, it leaves the noise as flat as the quantiser makes it,
well under the spectrum's peak and too high where the signal is weak.
Starting half way to an even noise-to-mask ratio moves noise from the bands
where the signal is weak to those where it is strong, which the loop
cannot do. **As measured**, it helps attacks (castanets, claves) and
leaves the rest where it was; the whole of that amplification, or the same
start on long granules, scores lower, and MPEG-2 and 2.5 streams gain
nothing from it, so they start from 0.

## 7. Determinism

The allocation is integer arithmetic like the model
([fixed-point.md](fixed-point.md) §1). Noise and `xmin` are compared in the
same representation, and `D` is a sum of the same `log2` the model uses, so
the choice of the best attempt is the same on every host.
