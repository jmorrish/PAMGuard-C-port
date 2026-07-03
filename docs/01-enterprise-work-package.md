# Enterprise Work Package

## Objective

Deliver a scalable web platform that provides PAMGuard-equivalent processing for these modules:

- FFT / spectrogram engine
- Click detector
- Whistle and moan detector
- Click localisation and tracking where supported by source/channel metadata

The platform must support many concurrent users and many live or offline audio sources while preserving scientific defensibility and reproducible outputs.

## Non-Negotiable Requirements

- PAMGuard Java source is the reference implementation.
- C++ outputs must be compared against PAMGuard outputs using golden fixtures.
- Configurability must match PAMGuard for supported modules unless a documented exception is approved.
- Detector state must be isolated per source/config analysis session.
- Multi-user viewing of the same source/config should share one engine session where possible.
- Every result must be traceable to source audio, engine version, config version, and processing time range.
- Live streams must handle reconnects, dropouts, jitter, and clock/sample continuity explicitly.

## Phases

1. Requirements and scope freeze
2. PAMGuard reference harness
3. C++ DSP foundation
4. Stream/file ingest platform
5. Click detector port
6. Whistle/moan detector port
7. Backend services
8. Web application
9. Scaling and deployment
10. Validation and release

## Acceptance Criteria

- A frozen target PAMGuard version is declared.
- Supported module settings have a one-to-one mapping.
- Golden fixture comparisons pass for FFT, click detection, and whistle/moan contours.
- Known differences are documented with reason, impact, and approval status.
- Load tests demonstrate target concurrency for live streams and offline jobs.
- Operational monitoring exposes stream health, worker health, queue depth, latency, CPU, memory, and failure rates.

## Initial Delivery Milestone

```text
PCM/WAV input
-> C++ FFT/spectrogram
-> PAMGuard FFT parity comparison
-> result JSON/tile-ready output
```

This milestone validates the foundation before detector porting begins.

