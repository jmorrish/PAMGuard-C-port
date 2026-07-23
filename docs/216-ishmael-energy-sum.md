# Ishmael Energy-Sum Detector

Date: 2026-07-23

## Purpose

Ports PAMGuard's `IshmaelDetector` energy-sum chain — `EnergySumProcess` (the detection function: band energy per FFT slice) feeding `IshPeakProcess` (the peak picker: threshold, duration and refractory gating). Item 5 of the low-hanging-fruit list. This is the classic general-purpose tonal/energy detector for low-frequency work (baleen whale moans and the like) where the click detector and whistle tracker don't fit.

## Reference semantics ported

**Detection function** (`EnergySumProcess`): per FFT slice, the mean magnitude-squared over the band's bins — `loBin = max(1, floor(len·f0/(sr/2)))`, `hiBin = min(len−1, ceil(len·f1/(sr/2)))`, bin 0 excluded ("FFT bin 0 has 0's in it"), and the reference divides its float sampleRate by 2 in float precision. With `useLog`, each bin contributes `log10(max(magsq, 1e-9)) + 5` — the floor hack verbatim. Optional ratio band (dB → subtraction, linear → division). Then, in this order: output smoothing, adaptive noise floor (`nf = longFilter·r + (1−longFilter)·nf`, halved whenever `nf > spikeDecay·r`), and with the adaptive floor the served value is `result − nf` with the floor and raw value carried alongside (`detData[3][1]` exactly).

**Peak picker** (`IshPeakProcess`): strictly-above-threshold values open/extend an event (capped at `maxTimeN+1` slices); below-threshold values wait out the refractory gap before closing; a closed event needs `duration ≥ minTimeN` and `nOverThresh ≤ maxTimeN` to emit, and the inter-detection interval must clear the refractory time (or start earlier than the last event). The event reports its raw-sample span, peak value and peak sample, `startMsec`, and the configured band.

**Quirks preserved and pinned:**

- `minTime`/`refractoryTime` convert to **detection-rate slices** but are compared against **raw-sample** durations and inter-detection intervals — the reference mixes the units, making `minTime` far easier to satisfy than its name suggests.
- The smoothing state is written **once** (the first result) and never updated: "smoothing" blends every later result with the *first* one.
- The noise floor and smoothing state are **single fields shared across every channel** the process serves; the engine keeps one energy sum per session and feeds frames slice-major across channels, the order PAMGuard's FFT units arrive in.
- The `-Double.MIN_VALUE` "unset" sentinels are kept verbatim.
- The reference computes an `endMsec` whose formula multiplies (rather than divides) by the detection rate — and then `IshDetection`'s constructor **discards** it. The port carries no end time rather than reproducing a value the reference itself throws away.
- No flush: an event still open at stream end is dropped, exactly as `IshPeakProcess` behaves at pamStop.

## Configuration and results

`ishmael`: `{enabled, f0, f1, ratioF0, ratioF1, useRatio, useLog, adaptiveThreshold, longFilter, spikeDecay, outputSmoothing, shortFilter, thresh, minTimeSeconds, maxTimeSeconds, refractoryTimeSeconds}` (`EnergySumParams` + `IshDetParams` defaults). Runs over the session FFT stream.

Results: `ishmaelDetections` at schema v25 — `{channel, startSample, durationSamples, peakTimeSample, peakHeight, startTimeMs, lowFreqHz, highFreqHz}`.

The importer maps `EnergySumParams` (skipping `f1 ≤ f0` with a printed reason; a null pre-upgrade `shortFilter` gets the 0.1 the runtime's clone() would restore), and the sample `.psfx` carries one.

## Validation

`ishmael_parity` (new, suite `79/79`): `IshmaelEnergySumFixtureExporter` drives the **real** `EnergySumProcess` and `IshPeakProcess` end to end — allocated the deserialisation way with the field-initialiser sentinels restored (skipping initialisers once produced a state real PAMGuard never has, caught because the first adaptive value differed), output blocks capture-subclassed, `prepareMyParams` running for real against a seeded source block so the bin mapping is reference bytecode too. Four cases × 160 slices: **640 detection-function values and 11 detections match bit-exactly (maxRelError 0)** — linear and dB/ratio sums, adaptive floor with smoothing, refractory merging of nearby bursts, the maxTime rejection path, two interleaved channels sharing the noise floor, and the dropped still-open tail event.

## Claim boundary

`SgramCorrProcess` (spectrogram correlation) and `MatchFiltProcess` (waveform matched filter) — the other two Ishmael detectors — are not ported; nor are `IshPeakProcess`'s downstream localisation hooks (`IshLocPairProcess` etc.). The `smoothing` field of `IshDetParams` (distinct from `outPutSmoothing`) is dead in the reference's energy-sum path and has no engine counterpart. Detection acceptance mirrors the reference's silent refractory rejection: rejected events update the last-event bookkeeping but are not reported anywhere.
