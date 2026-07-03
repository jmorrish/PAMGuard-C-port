# PCM timeline continuity

`POST /sessions/{sessionId}/pcm-f32le` now rejects empty bodies and reports chunk timeline continuity in every successful response.

Response fields:

- `expectedStartSample`: the sample index the service expected before this chunk.
- `nextExpectedStartSample`: `startSample + inputFrames`, used as the expected start for the next chunk.
- `sampleDelta`: `startSample - expectedStartSample`.
- `sampleContinuity`: `first`, `contiguous`, `gap`, or `overlap`.

Runtime session status also includes `sampleDiscontinuities`, `lastSampleDelta`, and `lastSampleContinuity`. Prometheus exposes `pamguard_session_sample_discontinuities`.

This is deliberately reporting, not automatic correction. Stateful PAMGuard-style detectors depend on the PCM sequence they receive, so ingest workers should keep posting monotonic contiguous chunks or explicitly start a new session when a stream resets.
