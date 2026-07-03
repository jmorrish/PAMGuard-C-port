# Implementation progress snapshot

Date: 2026-06-30

## Current engine capabilities

- C++ engine builds with MSVC, CMake, and Ninja.
- PAMGuard Java reference tree compiles with the local Maven/JDK tooling.
- Window functions match PAMGuard fixtures, including PAMGuard's Hann denominator behaviour.
- Real FFT output matches PAMGuard/JTransforms packed-bin fixtures through the parity bridge.
- Streaming spectrogram engine supports per-channel state, chunk invariance, and PAMGuard `PamFFTProcess` frame metadata.
- WAV reader supports RIFF PCM and IEEE float WAV inputs.
- Click trigger engine ports PAMGuard's short/long trigger filters, threshold state machine, trigger bitmap, pre/post/minSep/maxLength handling, and waveform capture.
- Click localisation path computes PAMGuard-style pairwise delay vectors from captured multi-channel click waveforms.
- Correlation delay and 3-channel delay-group estimators match PAMGuard delay fixtures.
- Whistle peak detector ports the PAMGuard Better Peak Detector slice logic, including warmup background, local-average behaviour, width filtering, and over-threshold rejection.
- Connected-region tracker ports the first PAMGuard whistle/moan contour-linking foundation: 4/8-connectivity, growing/completed region state, min-pixel/min-length filtering, and condensed slice peak metadata.
- `AnalysisSession` owns isolated per-source detector state.
- `SessionManager` provides thread-safe per-session creation, processing, and removal.
- `raw_pcm_stream_cli` accepts FFmpeg-decoded interleaved `f32le` PCM for Icecast/BUTT/MP3/WAV/network ingest bridges.
- `pamguard_engine_service` exposes a C++ HTTP wrapper for health checks, session creation/removal, and binary `f32le` PCM ingestion.
- `web-ui/index.html` provides a static browser console for health checks, session creation, synthetic PCM posting, and detector count inspection.

## Passing parity and integration checks

Current CTest suite: 17/17 passing.

- `window_hann_1024_parity`
- `window_rectangular_8_parity`
- `window_hamming_8_parity`
- `window_hann_8_parity`
- `window_bartlett_8_parity`
- `window_blackman_8_parity`
- `window_blackman_harris_8_parity`
- `spectrogram_chunking_invariance`
- `fft_hann_8_parity`
- `fft_hann_1024_parity`
- `pamfft_frame_hann_8_hop4_parity`
- `click_trigger_basic_2ch_parity`
- `session_manager_instanced_pipeline`
- `correlation_delay_basic_parity`
- `delay_group_3ch_basic_parity`
- `whistle_peak_basic_parity`
- `connected_region_basic_parity`

Additional smoke test:

- `pamguard_engine_service.exe 18080` health endpoint returned `{"ok":true,"sessions":0}`.
- Service create-session plus binary `pcm-f32le` post returned 16 input frames and 3 spectrogram frames.
- CORS-enabled service smoke repeated successfully after adding the browser console path.
- Region-enabled whistle service smoke accepted `whistle.regionEnabled=true` and returned the `whistleRegions` response field.

## Ingest bridge

Example:

```powershell
ffmpeg -i "<stream-or-file>" -f f32le -acodec pcm_f32le - |
  .\build\raw_pcm_stream_cli.exe 48000 2 4096 1024 512 --click
```

## Remaining enterprise gaps

- Persistent session config storage, authentication, tenant isolation, and validation hardening.
- Full PAMGuard click classification, echo handling, click train tracking, and event grouping.
- Full whistle/moan fragmentation/rejoining, stub removal, event grouping, and contour localisation beyond the initial connected-region tracker.
- Bearing/localisation solvers beyond pairwise delay vectors.
- Filter parity for PAMGuard `Filter` / `FFTFilter` paths.
- MP3/Icecast reconnect supervision and backpressure policies in the ingest worker.
- Production browser UI for live spectrogram rendering, full detector configuration, event review, annotation, and export.
- Large fixture library generated from real PAMGuard modules and real acoustic files.
