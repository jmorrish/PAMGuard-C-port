# Web Console Spectrogram Preview

Date: 2026-06-30

This checkpoint connects the browser console to the new opt-in spectrogram response data.

## Implemented

- Added preview controls:
  - `Preview bins`
  - `Bin stride`
- The synthetic PCM send action now requests:

```text
includeSpectrogram=true
spectrogramMaxBins=<Preview bins>
spectrogramBinStride=<Bin stride>
```

- Added a canvas heat-map preview for the first returned channel.
- Added metrics for:
  - click features;
  - click classifications.
- Demo session creation now enables click feature extraction with two configurable energy bands.

## Notes

This remains a proof console, not the final analyst workstation UI. The important architectural step is that the browser now consumes actual C++ engine spectrogram frame output rather than only count metadata.
