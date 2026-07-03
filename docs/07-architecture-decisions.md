# Architecture Decisions

## ADR-001: Use PAMGuard Java As The Oracle

Decision:

The Java PAMGuard implementation is the oracle for maths and detector behaviour.

Reason:

The project requirement is industry-proven PAMGuard correctness, not merely similar acoustic processing.

Implications:

- C++ code must be parity-tested against PAMGuard fixtures.
- Intermediate fixtures are required, not just final detections.
- Known differences must be explicitly documented and approved.

## ADR-002: One AnalysisSession Per Source And Config

Decision:

The C++ engine is instanced per unique source/config/array combination.

Reason:

PAMGuard-style processing is stateful. FFT overlap, filters, trigger backgrounds, contour linking, localisation, and tracking all depend on source history.

Implications:

- Multiple users can subscribe to one session.
- Different detector settings create separate sessions.
- Session state is isolated and replayable.

## ADR-003: Normalize All Inputs To Timestamped PCM

Decision:

Input adapters normalize files and streams to timestamped PCM chunks before they reach the engine.

Reason:

The detector engine should be independent of transport and codec details.

Implications:

- Icecast, BUTT, MP3, WAV, FLAC, and Ethernet inputs use separate adapters.
- Stream health and decode status are represented outside detector maths.
- Replay uses normalized PCM plus original source references where needed.

## ADR-004: Prefer Lossless/Synchronized Audio For Localisation

Decision:

Localisation and click tracking require synchronized multi-channel PCM or lossless equivalents for trusted results.

Reason:

Lossy codecs can alter transient timing, phase, and high-frequency click structure.

Implications:

- MP3/AAC may be supported, but outputs should carry source-quality warnings.
- Direct Ethernet and multi-channel WAV/BWF/FLAC are preferred for click localisation.
- Missing array metadata produces detection-only mode unless explicitly overridden.

## ADR-005: Build Shared Upstream Pipelines

Decision:

One source running multiple detector modules should share upstream PCM buffering and FFT/spectrogram work where settings match.

Reason:

This reduces CPU cost and ensures downstream modules see consistent data.

Implications:

- FFT output can feed whistle/moan and display pipelines.
- Click detector keeps its own raw waveform path where required.
- Pipeline reuse must not merge detector states across configs.

