# Execution Backlog

## Work Package 0: Programme Setup

Status: started

Deliverables:

- Enterprise workspace
- Architecture documents
- C++ engine scaffold
- Parity test structure
- Initial FFT/spectrogram vertical slice

## Work Package 1: PAMGuard Reference Harness

Deliverables:

- Java-side fixture exporter for window functions
- Java-side fixture exporter for FFT frames
- Golden WAV fixtures
- C++ fixture reader
- Numeric comparison report

Acceptance:

- Window fixtures match exactly within floating point precision.
- FFT fixture comparison identifies and resolves packing/scaling semantics.
- Frame sample indices match PAMGuard exactly.

## Work Package 2: Production C++ DSP Foundation

Deliverables:

- PAMGuard-compatible window library
- FFT adapter abstraction
- Production FFT backend selection
- Streaming spectrogram engine
- Multi-channel sample timeline handling
- Channel grouping primitives

Acceptance:

- FFT/spectrogram output matches PAMGuard for supported configs.
- Engine handles chunk boundaries without changing output.
- Output is stable under long-running stream simulation.

## Work Package 3: Audio Ingest

Deliverables:

- WAV/BWF file reader
- Icecast/BUTT HTTP connector
- Decoder adapter for MP3/AAC/FLAC via FFmpeg or GStreamer
- Direct Ethernet connector interface
- Reconnect/dropout/jitter policy
- Audio archive/replay store

Acceptance:

- All sources normalize to timestamped PCM.
- Dropouts are visible in stream health events.
- Replay produces identical PCM chunks for deterministic analysis.

## Work Package 4: Click Detector

Deliverables:

- PAMGuard ClickParameters config mapping
- Trigger filters and background state
- Group trigger logic
- Click extraction windows
- Click spectra/features
- Echo rejection and classifier extension points
- Click localisation hooks

Acceptance:

- Click times and sample windows match PAMGuard on golden fixtures.
- Detector state is isolated per AnalysisSession.
- Multi-channel click snippets preserve channel alignment.

## Work Package 5: Localisation And Click Tracking

Deliverables:

- Array metadata schema
- Channel-to-hydrophone mapping
- Time delay estimation
- Bearing/range/localisation outputs
- Click train/event association
- Tracking state persistence for live sessions

Acceptance:

- Localisation outputs match PAMGuard fixtures for supported geometries.
- Tracking is deterministic under replay.
- Missing array metadata degrades to detection-only mode with explicit status.

## Work Package 6: Whistle And Moan Detector

Deliverables:

- WhistleToneParameters config mapping
- Spectrogram noise reduction path
- Connected-region engine
- Shape linking and 4/8 connectivity
- Fragmentation/relinking/stub removal
- Contour and grouped detection outputs

Acceptance:

- Contour time/frequency bins match PAMGuard fixtures.
- Region filtering follows PAMGuard settings.
- Outputs are suitable for browser overlays and export.

## Work Package 7: Backend Services

Deliverables:

- Job queue
- Worker scheduler
- Session lifecycle API
- Result/event storage
- Config versioning
- Engine binary versioning
- Audit logging

Acceptance:

- Offline jobs can be queued and replayed.
- Live sessions survive transient ingest reconnects.
- Multiple subscribers can watch one shared session.

## Work Package 8: Web Application

Deliverables:

- Source management
- Detector settings UI
- Live spectrogram viewer
- Detection tables
- Click waveform/spectrum view
- Whistle contour overlays
- Export workflows
- Admin operations dashboard

Acceptance:

- Supported module settings are configurable from the browser.
- Large detections remain navigable and searchable.
- Session health is visible to operators.

## Work Package 9: Scale, Security, And Operations

Deliverables:

- Containerized services
- Horizontal worker scaling
- Resource quotas
- Metrics and tracing
- Load testing
- Security review
- Backup/retention policies

Acceptance:

- 50+ concurrent users are supported under agreed session/source mix.
- Resource isolation prevents one job from destabilizing the platform.
- Operational runbooks cover failure and recovery scenarios.

