# Whistle/Moan Contour Output

Date: 2026-07-01

This checkpoint makes connected-region whistle/moan detections directly usable by the web UI and API clients.

## Implemented

- Added per-region `contourPoints` to JSON output.
- Each point includes:
  - FFT slice number;
  - start sample;
  - time in milliseconds;
  - min/peak/max FFT bins;
  - min/peak/max frequency in Hz when the serializer has sample rate and FFT length.
- Added Hz fields for whistle peak detector outputs:
  - `minFreqHz`;
  - `peakFreqHz`;
  - `maxFreqHz`.
- Added `freqRangeHz` on whistle regions.
- Added per-contour timing summary fields:
  - `lastStartSample`;
  - `timeSpanSamples`;
  - `durationSamples`;
  - `timeSpanMs`;
  - `timeSpanSeconds`;
  - `durationSeconds`.
- Added named frequency/shape summary fields:
  - `minFrequencyBin` / `maxFrequencyBin` / `frequencySpanBins`;
  - `minFrequencyHz` / `maxFrequencyHz` / `frequencySpanHz`;
  - `minPeakBin` / `maxPeakBin` / `meanPeakBin`;
  - `minPeakHz` / `maxPeakHz` / `meanPeakHz`;
  - `startPeakBin` / `endPeakBin`;
  - `startPeakHz` / `endPeakHz`;
  - `peakSweepRateBinsPerSecond`;
  - `peakSweepRateHzPerSecond`.
- Added spectrogram overlay rendering in the browser console for returned contour points.
- Added a browser contour-summary panel showing duration, sample span, frequency envelope, mean peak, sweep rate, contour points, and pixel count for returned contours.
- Result archives now include the same contour Hz fields as live HTTP responses.

## Parity note

The existing bin-level fields remain unchanged:

```json
{
  "freqRange": [5, 11],
  "timesBins": [30, 31, 32],
  "peakFreqsBins": [6, 6, 11]
}
```

The Hz fields are derived presentation metadata using:

```text
frequencyHz = bin * sampleRateHz / fftLength
```

Contour `durationSamples` is computed as first-to-last slice start span plus the inferred final slice step. `timeSpanSamples` remains the first-to-last start-sample span for clients that need the exact observed contour baseline.

## Remaining contour UI work

- Add selectable channel overlays.
- Add contour table/export controls.
- Add localisation overlays once whistle/moan localisation is ported.
