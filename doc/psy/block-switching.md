# Attack detection, block types, short-block pre-echo

## 1. Attack detection

The detector runs in the time domain on the input samples, ahead of the MDCT
([signal-path.md](signal-path.md) §2).

### 1.1 High-pass filter

Per coded channel, a linear-phase half-band high-pass, 21 taps, cutoff
`fs/4`. With `f[n]` the input, output `n` is:

```
h[n] = f[n] + Σ_{j=0..9} c[j] · (f[n − 10 + j] + f[n + 10 − j])
```

| j | 1 | 3 | 5 | 7 | 9 |
|---|---|---|---|---|---|
| `c[j]` | −0.0170317 | 0.0418072 | −0.0876324 | 0.186348 | −0.627638 |

`c[j] = 0` for even `j`. The taps are symmetric about `n`, so the filter has
no phase distortion. Its DC gain is `1 − 1.008`, about −42 dB. Its gain is
about 2 near `fs/2`. **Essential**: the filter shape defines which events
count as attacks.

For the mid and side analysis channels, `h_M = (h_L + h_R)/√2` and
`h_S = (h_L − h_R)/√2` ([conventions.md](conventions.md)), so that every
level constant below means the same thing on every analysis channel.

### 1.2 Sub-block peaks

Each granule's 576 filtered samples are split into nine sub-blocks of 64. For
each analysis channel `c`:

```
P[i] = max(PEAK_FLOOR, max_{n in sub-block i} |h[n]| / FS)      i = 0..8
```

`P[−2]`, `P[−1]` are the last two sub-block peaks of the previous granule,
`PEAK_FLOOR` at the stream start. Peaks are amplitudes relative to full
scale. Grouped sums per short window:

```
Esh[s] = P[3s−3] + P[3s−2] + P[3s−1]        s = 1, 2, 3   (slot s)
Esh[0] = the previous granule's Esh[3]
```

`Esh[s]` is the level of slot `s` (§1.4): by the alignment of §1.4, slot
`s` is short window `s` of the coded granule for `s = 0, 1, 2`, and slot 3
is window 0 of the next one.

### 1.3 Attack intensity

Each sub-block is compared with the sub-block two before it, in rise and in
decay, the same for all nine:

```
for i = 0..8:
    p = P[i];  q = P[i−2]
    A[i] = p / q                      if p > q
         = q / (ATTACK_DECAY · p)     if q > ATTACK_DECAY · p
         = 0                          otherwise
```

A drop of more than 20 dB is an event, as a rise is. A decay ends a note
that the next note's attack would otherwise be measured against.

### 1.4 Attack slots

A granule has four slots. Slot `s ∈ {1, 2, 3}` covers sub-blocks
`3(s−1)..3s−1`. Its value is `t + 1` for the first `t ∈ {0, 1, 2}` with
`A[3(s−1) + t] > thr_c`, and 0 if there is none:

- `thr_c = ATTACK_RATIO` for c ∈ {0, 1, 2};
- `thr_c = ATTACK_RATIO_SIDE` for the side channel (c = 3).

**Alignment.** The detector's granule is placed in input time so that slot
`j` (j = 0, 1, 2) covers the input samples that short window `j` of the
coded granule codes with most weight: the middle 192 of its 384-sample span,
after the filterbank delay. Slot 0 is the previous detector granule's
slot 3, as computed before the rules of §1.5, so slot 3 is the look-ahead
into window 0 of the next coded granule.
The offset is a generated constant. The click test of [tests.md](tests.md) §2
checks it.

### 1.5 Suppression rules, in order

1. **Periodic signals.** A periodic signal whose period is near the
   sub-block length gives large ratios with no transient. For `s = 1..3`,
   with `u = Esh[s−1]` and `v = Esh[s]`: if `max(u, v)/3` is below
   `PERIODIC_CEILING`, and `u < PERIODIC_RATIO·v`, and
   `v < PERIODIC_RATIO·u`, then clear slot `s`. If `s = 1` and
   `attacks[0] ≤ attacks[1]` (before clearing), also clear slot 0. The
   ceiling keeps loud events eligible.
2. **Already handled.** If `attacks[0] ≤ last_attacks[c]`, clear slot 0.
   `last_attacks[c]` is slot 2 of the previous granule.
3. **Decision.** The channel needs short blocks if `last_attacks[c] = 3` or
   any slot is non-zero. If so, thin adjacent attacks in order, each step on
   the values left by the one before: clear slot 1 if slots 1 and 0 are both
   non-zero; then slot 2 if slots 2 and 1 are; then slot 3 if slots 3 and 2
   are.

The detector runs ahead of the model ([signal-path.md](signal-path.md) §2).
A granule's final slots, and its `last_attacks[c]`, are kept until its short
pre-echo control (§4) has run.

### 1.6 Pulse factor

A short window whose level sits in its first sub-blocks is a pulse, and
gets lower thresholds. For window `j`, `p0, p1, p2` are the three sub-block
peaks of slot `j` (§1.4): `P[3j−3..3j−1]`, with window 0 taking the last
three peaks of the previous detector granule. With `e = p0 + p1 + p2`:

```
pulse[c][j] = 1
if p2 < PULSE_FRACTION · e:
    pulse[c][j] = 0.5
    if p1 < PULSE_FRACTION · e: pulse[c][j] = 0.25
```

### 1.7 Per coded channel

`short[c]` for c ∈ {0, 1} is the decision of §1.5 for analysis channel `c`.
With M/S analysis channels, `short[0] = short[1] = true` when mid or side
needs short blocks.

## 2. Coupling

With two channels, if either coded channel is short, both are, in every
channel mode. Many hardware decoders mishandle channels with different block
types, and M/S coding needs equal types ([stereo.md](stereo.md) §3).

A mono stream has only channel 0. No rule reads the state of an absent
channel.

## 3. Block-type state machine

Per coded channel. `tentative` is the type proposed for the previous
granule, before it is known whether the current granule is short:

```
if short[c]:
    new = SHORT
    if tentative = NORM: tentative = START
    if tentative = STOP: tentative = SHORT
else:
    new = NORM
    if tentative = SHORT: new = STOP
final type of the previous granule = tentative
tentative = new
```

NORM before a short granule becomes START. STOP before a short granule
becomes SHORT, since a STOP window cannot precede a SHORT one. The sequence
of final types never has SHORT after NORM or STOP, nor NORM after SHORT or
START. `tentative` starts at NORM.

## 4. Short-block pre-echo control

Runs per analysis channel on the short band thresholds, after the short
masking and the M/S correction, on every granule whose short thresholds are
computed, whatever its block type ([masking.md](masking.md) §4).

For each short sfb `sb` (0..12) and window `j` (0..2), `prev` is the
processed value of the window before: `new[j−1]` for `j > 0`, and
`last_thm.s[sb][2]` for `j = 0`. `prev2` is the value two windows before:
`last_thm.s[sb][1]` for `j = 0`, `last_thm.s[sb][2]` for `j = 1`, `new[0]`
for `j = 2`. With no previous granule, `last_thm.s[sb][*]` is this granule's
`thm.s[sb][0]`.

```
t = thm.s[sb][j] · PREECHO_ATT0
if attacks[j] ≥ 2 or attacks[j+1] = 1:
    t = min(t, interp(prev, t, PREECHO_ATT1·PREECHO_BLEND))
if attacks[j] = 1:
    t = min(t, interp(prev, t, PREECHO_ATT2·PREECHO_BLEND))
else if (j = 0 and last_attacks[c] = 3) or (j > 0 and attacks[j−1] = 3):
    t = min(t, interp(prev2, t, PREECHO_ATT2·PREECHO_BLEND))
new[j] = t · pulse[c][j]
```

Then `thm.s[sb][0..2] = new[0..2]` and `last_thm.s = thm.s`.

- An attack late in window `j` (position ≥ 2), or at the start of the next
  window, pulls window `j`'s threshold toward the previous window's, in the
  log domain (`r = 0.36`).
- An attack early in window `j` (position 1) pulls it further (`r = 0.18`).
- Otherwise, an attack late in the previous window (position 3) pulls it
  toward the window two before.
- Every short threshold is lowered by about 1 dB (`PREECHO_ATT0`).

The limits are applied one after the other, each to the value the previous
one left. That order is part of the definition. `interp` is defined in
[conventions.md](conventions.md).
