# Bit reservoir

A frame's bits are fixed by the bitrate, but its granules do not
need the same number. The reservoir lets a granule that does not need all
its bits leave some for a later one that needs more: an attack, a dense
passage. A granule leaves bits when its noise is already under its allowed
distortion ([noise-allocation.md](noise-allocation.md) §4.1). Perceptual
entropy ([perceptual-entropy.md](perceptual-entropy.md)) says which granules
take them.

The lowest quality level has no reservoir: every frame holds exactly its own
main data.

## 1. The reservoir in the bitstream

Layer III separates the frame grid from the data. Every frame has a slot of
fixed length: header and side information, then `capacity` bytes that carry
main data. The main data of all frames forms one continuous byte stream,
laid into the capacities in order. A frame's main data may begin before its
own header: `main_data_begin` in its side information is the distance in
bytes, counted in main data only, from the start of its main data back from
its header (ISO 11172-3 §2.4.2.7, 13818-3 §2.4.2.7).

The reservoir `R`, in bits, is the capacity of the slots written so far
that no main data has used. At the start of frame `n`:

```
main_data_begin[n] = R / 8
R after frame n    = R + 8 · capacity[n] − used[n]
```

`used[n]` is everything frame `n` puts in the main data stream: the sum of
its `part2_3_length`, and any ancillary bits after them. `R` MUST be a
multiple of 8 at the end of every frame, and MUST NOT be negative: a frame
MUST NOT use more than `R + 8 · capacity[n]`.

## 2. The limit

At the end of every frame

```
R ≤ RESV_MAX = max(0, min(8 · MAIN_DATA_BEGIN_MAX, BUFFER_BITS − frame_bits))
```

- `MAIN_DATA_BEGIN_MAX` is the largest value the field codes: 511 for
  MPEG-1 (9 bits), 255 for MPEG-2 and 2.5 (8 bits).
- `BUFFER_BITS` = 7680 is the main data a decoder is required to hold: one
  frame at 320 kbps and 48 kHz. A frame and the reservoir it draws from MUST
  fit in it together, and the frame that draws from a reservoir is the next
  one: `frame_bits` is the length of the stream's longest frame, the padded
  one where the stream has padding. Where a frame alone is longer (320 kbps
  at 32 kHz), `RESV_MAX` is 0.

Bits above the limit, and those that would leave `R` short of a whole byte,
MUST be spent as stuffing in the frame that would exceed it, written as
ancillary bits after its last granule, and set to zero. They MUST NOT be
added to a `part2_3_length`: a decoder reads every bit of a granule's
`part2_3_length` as coefficients, and bits that do not form whole codes of
the granule's count1 table decode as nonzero values, at the granule's
gain.

## 3. Bits of a granule

Before the first granule of a frame, PE is smoothed
([perceptual-entropy.md](perceptual-entropy.md) §3). For each granule in
order, with `nch` coded channels, `mean` the frame's main data bits divided
by its granules, `pe` the sum of the smoothed PE of its coded channels, and
`R` the reservoir at that point, bits left by the frame's earlier granules
included:

```
demand = mean · pe / (PE_STEADY · nch)

granule_bits = mean + min(max(0, demand − mean), RESV_SPEND · R)
granule_bits = min(granule_bits, nch · PART2_3_MAX)
for each coded channel, once allocated:
    R += mean / nch − (bits the channel used)
```

`PE_STEADY` is what the smoothing gives a granule and channel of a signal
whose PE does not change: `670 · 5 / T`, with `T = 1 + 2 · Σ k[i]` the sum
of the filter's taps, about 739.4. So a steady signal demands the mean, and
only the ratio of a granule's PE to the recent average matters: an
implementation MAY compute `demand = mean · T · mode_gr · pe_raw / F` from
the unsmoothed PE of the granule and the filter output `F`. When `F ≤ 0`
the demand is the mean.

- A granule takes at most the fraction `RESV_SPEND` of what the reservoir
  holds, so two demanding granules in a row both get some.
- A granule whose PE is below the average still gets the mean. PE ranks the
  granules that need more, but it is an estimate: taking bits from a
  granule on a low PE alone costs passages whose PE understates them. Bits
  are left only by the allocation, where the measured noise shows they are
  not needed ([noise-allocation.md](noise-allocation.md) §4.1).
- All quantities are bounded before any conversion to an integer: `demand`
  is unbounded above, `granule_bits` is not.

`granule_bits` is the budget the noise allocation shares between the
channels ([noise-allocation.md](noise-allocation.md) §3.1). What the
allocation leaves ([noise-allocation.md](noise-allocation.md) §4.1), and
what the inner loop could not use, stays in the reservoir, up to the limit
(§2).

## 4. Output

A frame's slot is complete only when main data has filled its capacity,
which may take the following frames. An encoder therefore emits bytes later
than it codes them: a call that codes one frame MAY return no bytes, or the
slots of several frames. The byte stream MUST NOT depend on how the input
was cut into calls.

At the end of the stream every slot begun MUST be completed: capacity no
main data used is filled with zero bytes.

An encoder that cannot lay out a frame, out of memory for instance, MUST
end the stream with the last complete slot: the `main_data_begin` of every
later frame counts the bytes of the missing one.

## 5. Determinism

`R`, the demand and the budgets are integers
([fixed-point.md](fixed-point.md)). Two encoders given the same input
produce the same bytes.
