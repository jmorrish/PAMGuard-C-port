# Whistle contour summary metrics

Date: 2026-07-01

## Implemented

- Bumped result `schemaVersion` to `2`.
- Added detector-level timing summaries to connected-region whistle/moan results:
  - `lastStartSample`
  - `timeSpanSamples`
  - `durationSamples`
  - `timeSpanMs`
  - `timeSpanSeconds`
  - `durationSeconds`
- Added detector-level contour frequency/shape summaries:
  - `minFrequencyBin`
  - `maxFrequencyBin`
  - `frequencySpanBins`
  - `minPeakBin`
  - `maxPeakBin`
  - `meanPeakBin`
  - `startPeakBin`
  - `endPeakBin`
  - `peakSweepRateBinsPerSecond`
- Added HTTP/API Hz projections for the same contour summaries when sample rate and FFT length are available:
  - `minFrequencyHz`
  - `maxFrequencyHz`
  - `frequencySpanHz`
  - `minPeakHz`
  - `maxPeakHz`
  - `meanPeakHz`
  - `startPeakHz`
  - `endPeakHz`
  - `peakSweepRateHzPerSecond`

## Semantics

- Raw PAMGuard-style contour arrays remain unchanged: `freqRange`, `timesBins`, `peakFreqsBins`, and `contourPoints`.
- `timeSpanSamples` is the first-to-last contour slice start-sample span.
- `durationSamples` is `timeSpanSamples` plus the inferred final slice step from the observed contour slice starts.
- Archive detector-event projection uses `durationSamples` to populate `whistle-contour` `endSample` values.
- Hz fields use the same conversion as existing contour point fields:

```text
frequencyHz = bin * sampleRateHz / fftLength
```

## Validation

- `connected_region_fixture_check` now asserts the new timing, frequency envelope, mean peak, and sweep-rate metrics against the existing connected-region parity fixture.
- This change originally introduced result schema version `2`; the service smoke now asserts the latest result schema version on health, live PCM responses, and archived result records.
- The browser dashboard renders contour-summary cards for live/flush results using these fields.
