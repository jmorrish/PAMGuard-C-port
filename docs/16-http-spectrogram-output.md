# HTTP Spectrogram Output

Date: 2026-06-30

This checkpoint adds opt-in spectrogram frame output for browser rendering.

## Implemented

The PCM endpoint still returns compact counts by default:

```text
POST /sessions/{sessionId}/pcm-f32le
```

Spectrogram frame data can now be requested with query parameters:

```text
POST /sessions/{sessionId}/pcm-f32le?includeSpectrogram=true
```

Optional response controls:

```text
includeSpectrogram=true
includeSpectrogramComplex=true
spectrogramMaxBins=128
spectrogramBinStride=2
```

Each returned spectrogram item includes:

- `channel`
- `startSample`
- `timeMs`
- `slice`
- `binStride`
- `magnitudeSquared`
- `complexBins`, only when requested

The magnitude values use the same PAMGuard/JTransforms packed real-FFT magnitude-square convention used elsewhere in the parity layer.

## Validation

CTest status after this checkpoint:

```text
19/19 tests passed
```

HTTP spectrogram smoke test:

```json
{"spectrogramFrames":14,"spectrogramItems":14,"firstBins":4}
```

## Notes

Spectrogram arrays are intentionally opt-in because a continuous multi-channel stream can produce large responses. Browser-facing calls should request only the bin density needed for display, while archival or offline export routes can request larger payloads.
