# Ishmael Spectrogram Correlation Detector

Date: 2026-07-23

## Purpose

Ports PAMGuard's `SgramCorrProcess` — the Mellinger & Clark (2000) spectrogram correlation detector, the second of the three Ishmael detectors. A time-frequency kernel built from line segments is dot-multiplied against a sliding spectrogram window; the result feeds the **same** `IshPeakProcess` peak picker already ported for the energy sum (`docs/216`), so this slice reuses `IshmaelPeakPicker` unchanged.

## Reference semantics ported

**Kernel** (`makeKernel`): segments are `{t0, f0, t1, f1}` lines. The bin range spans the segments' frequency extent padded by `4 × spread` (four standard deviations), with `minBin` clamped at 0 and — a preserved quirk — `maxBin` clamped at **`gramHeight/2`**, half the spectrum, so kernels never extend above sr/4. `durN = max(1, ceil((maxT−minT)·frameRate))` time slices. Each cell sums, over segments covering its time, the Mexican-hat `hat((f − axisF)/spread)` where `axisF` is the segment's linear interpolation at that time (`PamUtils.linterp`), and `hat(x) = (1−x²)·e^(−x²/2)` — un-normalised, exactly as the reference notes ("should divide by sqrt(2*pi)?").

**Runtime**: per channel, a circular buffer of `durN` gram slices holding `magsq` (log mode: `log10(max(1, magsq))` — floored at 1, not 1e-9 as the energy sum does) over the kernel's bin range, packed bin 0 included when `binOffset` is 0. Once `nFramesIn` reaches the kernel length, every slice yields `gramDotProd` with kernel row 0 aligned to the **oldest** stored slice. The value drives the shared peak picker; the reported detection band is the kernel's frequency span (`getLoFreq`/`getHiFreq` as floats).

## Configuration and results

`sgramCorr`: `{enabled, segments: [[t0, f0, t1, f1], ...], spread, useLog, thresh, minTimeSeconds, maxTimeSeconds, refractoryTimeSeconds}` — the `SgramCorrParams` fields plus the `IshDetParams` peak-picking base. Runs over the session FFT stream; channels keep independent buffers and picker state.

Results: `sgramCorrDetections` at schema v27, same fields as `ishmaelDetections`.

The importer maps `SgramCorrParams` (empty segment lists skipped with a printed reason) and the sample `.psfx` carries a 500→1500 Hz upsweep.

## Validation

`sgram_corr_parity` (new, suite `81/81`): `SgramCorrFixtureExporter` drives the **real** `SgramCorrProcess` — allocated the deserialisation way with `savedGramHeight`/`perChannelInfo` restored (the field-initialiser lesson from `docs/216`), `prepareMyParams` running for real against a seeded source block so `makeKernel` and `renewPerChannelInfo` are reference bytecode — chained into the **real** `IshPeakProcess`. The kernel is exported **row by row** and compared directly. Three cases (single-segment upsweep, two-segment up/down contour in log mode across two interleaved channels, and a wide-spread kernel driving the `gramHeight/2` top clamp): **1717 kernel values, 303 detection-function values, and 7 detections match to 2.2e-16** — machine epsilon.

## Claim boundary

`MatchFiltProcess` (the waveform matched filter) is now the only unported Ishmael detector. The detection-function path shares `IshPeakProcess`'s recorded unit quirks (`docs/216`) — slice-unit times against raw-sample durations, no flush at stream end. `pamStart`'s buffer renewal maps to session construction (one run per session); mid-run FFT-length changes cannot arise in the engine and are not claimed.
