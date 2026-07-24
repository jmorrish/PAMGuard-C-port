# Noise Band Monitor and Amplitude Calibration

Date: 2026-07-22

## Purpose

Ports PAMGuard's `noiseBandMonitor` — octave-family band noise levels, the soundscape/regulatory workhorse (MSFD indicator 11 runs on third-octave levels) — and, because its output is meaningless without it, PAMGuard's amplitude calibration (`AcquisitionProcess.rawAmplitude2dB`), which turns −1..1 samples into **dB re 1 µPa**. Items 2 and 3 of the low-hanging-fruit list, built directly on the `docs/213` filter port.

## Reference semantics ported

**Band tables** (`BandData`): ANSI band ladders anchored at the reference frequency (1 kHz), walked in band-ratio steps to bracket the requested range — octave, third-octave, decidecade, decade, tenth- and twelfth-octave, each with its own ratio and edge half-band. Getting this wrong silently shifts every reported level to the wrong frequency, which is why it carries the fixture.

**The processing chain** (`NoiseBandControl`/`NoiseBandProcess`): decimation groups **chain** — each group lowpass-filters (Butterworth, order `iirOrder + 2`, corner at the next stage's Nyquist) and keeps every Nth sample (N = 2, or 10 for decade bands), feeding the next group; the first group has no decimator. Band filters (Butterworth bandpass, order `iirOrder`) attach to the deepest group whose decimator corner clears the band's high edge by the √2 band gap, and run at that group's decimated rate. The pick-every-Nth **offset carries across chunks**, so odd-length chunks stay sample-exact — the reference comments on exactly this. Bands are built descending in frequency and reported ascending, as `newData`'s reversal does.

**Measurement**: per band per output interval, linear RMS (`sqrt(sumSquared/n)`) and max-abs, then through the calibration:

```
dB = 20·log10(raw · voltsPeak2Peak / 2) − (hydrophone sensitivity + hydrophone preamp gain + system preamp gain)
```

with non-finite results forced to 0, exactly as `rawAmplitude2dB` does. The interval is sample-counted rather than wall-clock so job replays are deterministic.

## Configuration

- `noiseBand`: `{enabled, bandType: octave|thirdOctave|decidecade|decade|tenthOctave|twelfthOctave, minFrequencyHz, maxFrequencyHz (0 = Nyquist), referenceFrequencyHz, iirOrder, outputIntervalSeconds}`.
- `acquisition`: `{voltsPeak2Peak, preampGainDb}` — defaults (2 V, 0 dB) make the formula collapse to plain `20·log10(raw)`, so unconfigured sessions get honest relative dB rather than false absolutes.
- `array.hydrophones[].preampGainDb` joins `sensitivityDb` (which existed but was never *used* until now — every click amplitude and noise level before this was relative).

Results: `noiseBands` at schema v23 — per channel per interval, `rmsDb` and `peakDb` arrays ascending in frequency.

The importer maps `NoiseBandSettings` (skipping non-Butterworth filter configurations with a printed reason), `AcquisitionParameters.voltsPeak2Peak` + preamplifier gain (skipped with a reason when the file carries no voltage, since zero would be an invalid config, not a calibration), and the sample `.psfx` now carries both.

## Validation

`noise_band_parity` (new, suite `77/77`): **138 band definitions across all six band types match the real `BandData` exactly** (7 cases). Then a runtime check over the parity-proven filters: a 1 kHz tone of amplitude 0.5, fed in deliberately odd-length chunks (4801 samples, exercising the decimator offset carry), lands in the 1 kHz third-octave band at RMS 0.3525 against the theoretical 0.3536 — the 0.3 % gap is the bandpass's passband ripple, not an error — with >30× rejection two bands away on both sides.

## Claim boundary

The band *tables* have real-class fixture parity; the *runtime chain* is validated by construction (the filters inside it have 2.4e-15 parity from `docs/213`) plus the physical tone test, not by a Java end-to-end fixture — `NoiseBandProcess` is PamController-coupled, and its per-band threading is an execution detail with no numeric effect. FIR-windowed decimators (`NoiseBandSettings.filterType = FIRWINDOW`) are not ported; the importer says so per file rather than silently substituting.

The interval is sample-counted; PAMGuard's is wall-clock (`timeMillis`), so a PAMGuard run over gappy audio bins measurements slightly differently than a replayed engine run. Deterministic replay was judged worth that divergence, and this is where it is recorded.

`noiseMonitor` (the FFT-based noise statistics module with mean/median/percentile aggregation) is a separate PAMGuard module, now ported in `docs/230`.
