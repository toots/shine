# Fixed-point mapping

The library is strictly fixed point: CI rejects any floating point or libm
use in it (`tools/check-no-float.sh`). Its output MUST be bit-exact on every
host. This file says how each part of the spec maps onto integer arithmetic.
The representations are recommendations. The binding parts are §1 and the
tolerances of [tests.md](tests.md) §3.

## 1. Binding rules

- Every run-time operation is integer arithmetic with defined behaviour on
  every C11 host. No result may depend on the width of `long`, on the
  rounding of a division of negative operands, or on evaluation order.
- Every constant that needs a transcendental function is a generated table
  (§3), computed offline and checked in. A table is never computed at run time
  by a host's libm.
- Overflow MUST be impossible for any input, including full-scale clipped
  input. Every accumulator's headroom is stated next to it in the code.
- A saturating or underflowing step MUST give the value the spec gives at
  that limit: a silent band is a band of energy 0, not of a residual
  rounding value.

## 2. Representations

**Log domain for energies and thresholds.** Most of the model is products,
ratios, powers, minima and comparisons of energies spanning more than 200 dB:
spreading weights, tonality offsets, masking lowering, ATH adaptation,
`interp`, PE, the band ratio transfer. In a signed 32-bit `log2` with 16
fractional bits (about 0.0001 dB resolution), all of them are additions,
multiplications by constants, and integer compares:

| Spec operation | Log-domain form |
|---|---|
| `a · b`, `a / b` | `la + lb`, `la − lb` |
| `x^r` | `r · lx` |
| `interp(x, y, r)` | `r·lx + (1 − r)·ly` |
| `10^(dB/10)` factor | add the constant `dB · log2(10)/10` |
| `log10(r)` (PE, `mask_add` index) | `lr · log10(2)` |
| `athAdjust` | `l_floor + w·(lx − l_floor) + gain` |
| zero | a reserved minimum value, compared before any arithmetic |

**Linear domain for sums.** Line energies, partition sums, spreading sums and
band sums are additions of many terms. They are accumulated linearly in 64
bits, then converted to the log domain once. Squares of 32-bit MDCT
coefficients fit in 64 bits after a shift fixed by the MDCT's headroom.
`mask_add` is a two-operand addition with a ratio-dependent gain: compute the
ratio as a log difference, look up `T`, and add in the linear domain, or use
a log-add table `log2(1 + 2^−d)`.

**Block floating point** is the alternative for the linear sums: one shared
exponent per granule and channel, 32-bit mantissas. It suits the spreading
sums, where every term of a row shares a scale.

**Integer log2.** A count of leading zeros gives the integer part. A table of
the mantissa's top bits, linearly interpolated, gives the fraction: 9 bits
and 513 entries hold the error under 0.0001 dB. The table is generated (§3).
`2^x` for the return to linear is the same table, inverted.

**Ratios as products.** Comparisons of ratios against a constant (attack
intensity, `MS_SIMILARITY`, `PERIODIC_RATIO`, `ADD_NEAR_LIMIT`) need no
division: `p/q > k` is `p > k·q` with `k` in a fixed Q-format and 64-bit
products.

## 3. Generated tables

Like `tools/generate_tables.c`, a generator computes in double precision and
prints C tables. It covers every sample rate and both layouts:

- partitions: `start`, `numlines`, `bval`, `bval_width`, `mld`, `npart`;
- spreading rows: `lo`, `hi`, and `log2 S[i][j]`;
- per partition: `log2 minval`, `log2 masking_lower`, `log2 ath_part`;
- per band: `log2 ath_band`, the band offsets;
- the equal-loudness weights, `K_loud`;
- `K_long`, `K_short`, the transform normalisations, derived from the MDCT
  scaling of the library (its headroom and gain);
- `tab`, `T`, the `log2` mantissa table, the PE coefficients and smoothing
  taps, the attack filter taps, the short-window `decay`;
- the attack-slot alignment offset.

The generator is the only place where the spec's formulas meet floating
point. Its output is the shared input of the library and of the float oracle
([tests.md](tests.md) §3).

## 4. Per part

| Part | Mapping |
|---|---|
| Line energies | `X²` in 64 bits, accumulated linearly per partition, then `log2`. |
| Tonality index | `(n·m − a)·20 / (a·L)` in 64-bit integers from the linear partition values. The result is a small integer. |
| Spreading | linear 64-bit or block floating point accumulation in partition order, `mask_add` per step. |
| Limits, clamps, lowering | log-domain additions and minima. |
| ATH and adaptation | log domain. `adjust_factor` in Q31, its update in 64-bit products. |
| M/S correction | log domain, except `m + s`, which is a linear two-term addition. |
| PE | log differences times Q-format coefficients, summed in 32 bits. |
| PE smoothing | Q15 taps, a 64-bit sum, one 64-bit division. |
| Attack filter | Q15 taps on 16-bit input samples, 32-bit accumulators, exact. |
| Attack peaks and ratios | integers; ratios as cross-products. |
| Allowed distortion | log domain, returned to the loop in the form the loop quantises against. |
| Noise allocation | reconstruction from a generated `ix^(4/3)` table and the step's `2^(q/4)`; squared errors summed in 64 bits per band, then `log2`, compared with `xmin` and summed into `D` in the log domain. |
