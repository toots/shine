# Parameters

Every constant of the model, with its recommended value. **Essential** values
define the model's structure: changing one changes what the model computes,
not only how strongly. **Tuning** values set the strength of an effect. A
tuning value may change with a metric report ([tests.md](tests.md) §4). The
churn table in [known-complexity.md](known-complexity.md) is the evidence for
which is which. A value with no stated derivation is tuning.

## 1. Per-bitrate set

The bitrate selects a row: the nearest table bitrate, with ties going to the
higher one. All columns are **tuning**.

| kbps | `MSFIX` | `ATTACK_RATIO` | `ATTACK_RATIO_SIDE` | `MASK_ADJUST` (dB) | `ATH_LOWER` (dB) | `ATH_CURVE` |
|---|---|---|---|---|---|---|
| 8 | 3.5 | 6.60 | 145 | 0 | −30 | 11 |
| 16 | 3.5 | 6.60 | 145 | 0 | −25 | 11 |
| 24 | 3.5 | 6.60 | 145 | 0 | −20 | 11 |
| 32 | 3.5 | 6.60 | 145 | 0 | −15 | 11 |
| 40 | 3.5 | 6.60 | 145 | 0 | −10 | 11 |
| 48 | 3.5 | 6.60 | 145 | 0 | −10 | 11 |
| 56 | 3.5 | 6.60 | 145 | 0 | −6 | 11 |
| 64 | 3.5 | 6.60 | 145 | 0 | −2 | 11 |
| 80 | 3.5 | 6.60 | 145 | 0 | 0 | 8 |
| 96 | 2.50 | 6.60 | 145 | 0 | 1 | 5.5 |
| 112 | 2.25 | 6.60 | 145 | 0 | 2 | 4.5 |
| 128 | 1.95 | 6.40 | 140 | 0 | 3 | 4 |
| 160 | 1.79 | 6.00 | 135 | −2 | 5 | 3.5 |
| 192 | 1.49 | 5.60 | 125 | −4 | 7 | 3 |
| 224 | 1.25 | 5.20 | 125 | −6 | 9 | 2 |
| 256 | 0.97 | 5.20 | 125 | −8 | 10 | 1 |
| 320 | 0.90 | 5.20 | 125 | −10 | 12 | 0 |

Derived per stream:

- `MASK_ADJUST_SHORT = 0.9·MASK_ADJUST` if `MASK_ADJUST > 0`, else
  `1.1·MASK_ADJUST`.
- `ATH_OFFSET = 10^(−ATH_LOWER/10)`.
- `MINVAL_DEPTH = 5 · row_kbps / 320` dB, from the row's bitrate.

The model MUST NOT scale the input samples. The output level of the encoder
does not depend on the quality setting or the bitrate.

The ATH values in this table (`ATH_LOWER`, `ATH_CURVE`) were set against a
band ATH with no width factor, so they MUST be retuned together with the band
ATH rule of [ath.md](ath.md) §2. The allocator offsets (`MASK_ADJUST`) MUST
be retuned together with the allocator of phase 2.

## 2. Model constants

| Name | Value | Kind | Where |
|---|---|---|---|
| `DELBARK` | 0.34 Bark | essential | partition width, [spectral-analysis.md](spectral-analysis.md) §3 |
| `TONALITY_SCALE` | 20 | tuning | [masking.md](masking.md) §1 |
| `TONALITY_SMOOTHING` | 2 granules | tuning | [spectral-analysis.md](spectral-analysis.md) §1.2 |
| `tab`, `delta` | table | tuning | [conventions.md](conventions.md) |
| `ADD_NEAR_LIMIT` | 10^(9/16) | tuning | [masking.md](masking.md) §2 |
| `ADD_FAR_LIMIT` | 10^(24/16) | tuning | [masking.md](masking.md) §2 |
| `T` | table | tuning | [masking.md](masking.md) §2 |
| `AVG_MASK_FACTOR` | 0.5 (−3 dB) | tuning | [masking.md](masking.md) §2 |
| `PREECHO_RATIO_1`, `PREECHO_RATIO_2` | 2, 16 | tuning | [masking.md](masking.md) §3.1 |
| `MINVAL_MIN_LINES` | 2 | essential | [masking.md](masking.md) §3.3 |
| `MASKING_TILT` | −7.4 dB | tuning | [masking.md](masking.md) §3.4 |
| short SNR | −8.25 → −4.5 dB over 13 → 24 Bark | tuning | [spectral-analysis.md](spectral-analysis.md) §4 |
| long SNR | 0 dB | tuning | [spectral-analysis.md](spectral-analysis.md) §4 |
| `ATH_FS_SPL` | 96 dB SPL | tuning | [ath.md](ath.md) §1 |
| `ATH_ADAPT_RANGE` | 90.30873362 dB | essential | [ath.md](ath.md) §3 |
| `ATH_ADAPT_GAIN` | −4.51571501 dB | tuning | [ath.md](ath.md) §3 |
| `LOUDNESS_FS_NOISE` | 3.0 | essential | [ath.md](ath.md) §4.1 |
| adaptive ATH | 0.03125, 31.98, 0.000625, 0.075, 0.925; initial 0.01, 1.0 | tuning | [ath.md](ath.md) §4.2 |
| `MS_SIMILARITY` | 1.58 (2 dB) | tuning | [stereo.md](stereo.md) §2 |
| PE coefficients and bases | tables; 1124.23/4, 1236.28/4 | tuning | [perceptual-entropy.md](perceptual-entropy.md) §2 |
| `PE_RATIO_CAP` | 10^10 | tuning | [perceptual-entropy.md](perceptual-entropy.md) §2 |
| PE smoothing | 19 taps; 670·5; initial 700 | tuning | [perceptual-entropy.md](perceptual-entropy.md) §3 |
| attack filter taps | table | essential | [block-switching.md](block-switching.md) §1.1 |
| `PEAK_FLOOR` | −90.3 dBFS | essential | [block-switching.md](block-switching.md) §1.2 |
| `ATTACK_DECAY` | 10 (20 dB) | tuning | [block-switching.md](block-switching.md) §1.3 |
| `PERIODIC_RATIO` | 1.7 | tuning | [block-switching.md](block-switching.md) §1.5 |
| `PERIODIC_CEILING` | −7.8 dBFS | tuning | [block-switching.md](block-switching.md) §1.5 |
| `PULSE_FRACTION` | 1/6 | tuning | [block-switching.md](block-switching.md) §1.6 |
| `PREECHO_ATT0/1/2` | 0.8, 0.6, 0.3 | tuning | [block-switching.md](block-switching.md) §4 |
| `PREECHO_BLEND` | 0.6 | tuning | [block-switching.md](block-switching.md) §4 |
| band offsets | table | tuning | [allowed-distortion.md](allowed-distortion.md) §1 |
| `TEMPORAL_SUSTAIN` | 0.01 s | tuning | [allowed-distortion.md](allowed-distortion.md) §3 |
| `XMIN_FLOOR` | −200 dBFS | essential | [allowed-distortion.md](allowed-distortion.md) §2 |
| `IX_MAX` | 8192 | essential | [noise-allocation.md](noise-allocation.md) §1 |
| `QUANT_EXACT_MAX` | 64 | essential | [noise-allocation.md](noise-allocation.md) §1 |
| `PART2_3_MAX` | 4095 | essential | [noise-allocation.md](noise-allocation.md) §3.1 |
| `OUTER_ATTEMPTS` | 8 per granule, per quality level | tuning | [noise-allocation.md](noise-allocation.md) §4 |
| lowpass cutoffs, `LOWPASS_*` | table | tuning | [bandwidth.md](bandwidth.md) |
| `BUFFER_BITS` | 7680 | essential | [reservoir.md](reservoir.md) §2 |
| `MAIN_DATA_BEGIN_MAX` | 511 (MPEG-1), 255 (MPEG-2, 2.5) | essential | [reservoir.md](reservoir.md) §2 |
| `PE_STEADY` | 670 · 5 / (1 + 2 · Σ k) ≈ 739.4 | essential | [reservoir.md](reservoir.md) §3 |
| `RESV_SPEND` | 1/2 | tuning | [reservoir.md](reservoir.md) §3 |
| `DECISION_AGREEMENT` | 99% | conformance | [tests.md](tests.md) §3 |
| `THRESHOLD_TOLERANCE` | 0.5 dB | conformance | [tests.md](tests.md) §3 |
| `NUMERIC_AGREEMENT` | 99.9% | conformance | [tests.md](tests.md) §3 |
| `SCORE_TOLERANCE` | 5 | conformance | [tests.md](tests.md) §4.2 |
| `SCORE_MEAN_TOLERANCE` | 0.75 | conformance | [tests.md](tests.md) §4.2 |

## 3. Configuration

| Setting | Value | Where |
|---|---|---|
| channel mode, 2 channels | joint stereo | [stereo.md](stereo.md) |
| short blocks | coupled across channels | [block-switching.md](block-switching.md) §2 |
| adaptive ATH | on | [ath.md](ath.md) §4 |
| temporal masking between short windows | on | [allowed-distortion.md](allowed-distortion.md) §3 |

A parameter has one value per stream. No parameter depends on a setting of
another encoding mode.
