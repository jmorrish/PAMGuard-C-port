# Whistle Delay Foundation

Date: 2026-07-06

## Purpose

Whistle/moan localisation was the last untouched detector-maths Gap in the parity ledger. PAMGuard localises whistles by estimating inter-channel delays from a connected region's contour: `whistlesAndMoans.WhistleDelays.DelayMeasure` accumulates the cross spectrum over the contour's peak bins across slices, then finds the correlation peak. This slice ports that delay core with Java fixture parity — the first step of the whistle localisation path.

## Reference semantics ported

`cpp-engine/src/localisation/WhistleDelayEstimator.h/.cpp`:

- Per-bin accumulation exactly as `DelayMeasure.addFFTData`: cross spectrum `ch1 * conj(ch2)` plus per-channel magnitude-squared scales over contour bins.
- `getDelay`'s scale (`sqrt(scale1*scale2)*2/fftLength`) and correlation peak via the shared `Correlations.getInterpolatedPeak` port (promoted to a public static on `CorrelationDelayEstimator` for reuse).
- The inverse transform replicates JTransforms `DoubleFFT_1D.realInverse(a, true)` packing: `a[0] = Re[0]`, `a[1] = Re[n/2]`, pairs thereafter. PAMGuard stores the accumulated bin-zero imaginary part in the `a[1]` slot; contour bins never include bin zero in practice, and the port documents and matches that layout.

## Fixture

`WhistleDelayFixtureExporter.java` transcribes the `DelayMeasure` accumulation (WhistleDelays is control/data-block coupled) and drives the real `FastFFT.realInverse` and `Correlations.getInterpolatedPeak`. Synthetic per-slice spectra apply a pure spectral delay plus deterministic phase jitter to channel 2. Five cases:

| Case | Pins |
| --- | --- |
| `zero-delay` | Near-zero recovered delay with jitter. |
| `fractional-positive` / `fractional-negative` | Sub-sample interpolated delays (+3.5 / -2.25 samples). |
| `beyond-window-ambiguity` | True delay outside the search window: the narrowband contour's periodic correlation makes a near-zero sidelobe win (score 0.96) — whistle delays are inherently ambiguous for narrowband contours, faithful to PAMGuard. |
| `rising-contour` | Delay recovered across a frequency-rising contour with per-slice bin movement. |

`whistle_delay_basic_parity` mirrors the catalogue and compares delay and score within `1e-8` (naive-DFT inverse vs JTransforms split-radix rounding).

## Validation

- `whistle_delay_basic_parity` passes across all five cases.
- Full CTest suite passes `54/54`.

## Claim boundary

This ports the delay measurement core only. The surrounding PAMGuard plumbing — backwards FFT-block search matching slices to FFT data units, `Correlations.getMaxDelays` from the array manager, bearing conversion via the bearing localiser chain, and `WhistleBearingInfo` outputs — is not yet ported; the engine's whistle path also does not yet retain complex FFT data per channel for regions. Ledger status moves from Gap to Foundation with fixture parity for the delay core.
