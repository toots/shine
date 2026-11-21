# Stereo: mid/side thresholds and the M/S decision

Phase 1. It applies to joint stereo, the default for two channels. In other
channel modes only analysis channels 0 and 1 exist, and no rule of this file
applies.

Coding a frame as M/S needs the bitstream's mode extension and the M/S
transform of the MDCT coefficients, `(X_L ± X_R)/√2`. Both are part of this
phase.

## 1. Mid and side analysis channels

Mid and side line energies come from the M/S coefficients
([spectral-analysis.md](spectral-analysis.md) §1). Each goes through the full
masking procedure of [masking.md](masking.md) on its own. For its pre-echo
history it uses the block type of coded channel 0 (mid) or 1 (side).

## 2. M/S threshold correction

Noise coded in M and S reappears in both L and R after decoding, so M/S
thresholds cannot simply be the thresholds of the M and S signals. This
correction keeps the L/R noise that M/S thresholds imply within what L and R
can mask.

It runs per partition on partition thresholds, after masking
([masking.md](masking.md) §3) and before the band sums: on the long layout
every granule, and in phase 4 on the short layout every granule and window,
whatever the block type. The short thresholds of a long granule are history
for the next granule's pre-echo control
([block-switching.md](block-switching.md) §4), so they are corrected like
any other.

Inputs: `ebM = eb[2][b]`, `ebS = eb[3][b]`, `tL, tR, tM, tS = thr[0..3][b]`,
`mld = mld[b]` ([spectral-analysis.md](spectral-analysis.md) §3), and
`ath = athAdjust(adjust_factor, ath_part[b])` ([ath.md](ath.md) §2–§3):

```
if tL ≤ MS_SIMILARITY·tR and tR ≤ MS_SIMILARITY·tL:
    rM = max(tM, min(tS, mld·ebS))
    rS = max(tS, min(tM, mld·ebM))
else:
    rM = tM;  rS = tS
tLR = min(max(tL, ath), max(tR, ath))
m = max(rM, ath);  s = max(rS, ath)
if tLR·2·MSFIX < m + s:
    f = tLR·2·MSFIX / (m + s);  m = m·f;  s = s·f
thr[2][b] = min(m, rM, ebM)
thr[3][b] = min(s, rS, ebS)
```

1. **Demasking.** When the L and R thresholds are within 2 dB, the sound is
   nearly centred. Each of M and S is then raised toward the other's energy
   lowered by the binaural masking level difference.
2. **Sum limit.** The sum of the M and S thresholds may not exceed `2·MSFIX`
   times the smaller L/R threshold, every term floored at the ATH. `m + s > 0`
   always holds there, since `ath > 0`.
3. **Clamp.** Neither threshold may exceed its own energy.

## 3. M/S decision

Per frame, after the model:

```
use M/S  if  Σ_{g,c} pe_MS[g][c] ≤ Σ_{g,c} pe[g][c]
else     L/R
```

When M/S is used, the M/S thresholds and `pe_MS` go to the allocator, which
shares the granule's bits between mid and side by their PE
([noise-allocation.md](noise-allocation.md) §3.1). The comparison is between
PE totals, so ties go to M/S.

M/S coding requires both coded channels to have the same block type in every
granule of the frame. Coupling ([block-switching.md](block-switching.md) §2)
guarantees that. An implementation that does not couple MUST then use L/R for
any frame where the types differ.

Analysis channels 2 and 3 are needed only for this decision and for M/S
frames. A quality level that never uses M/S MAY skip them.
