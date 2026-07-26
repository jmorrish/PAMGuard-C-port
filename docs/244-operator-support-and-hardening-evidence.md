# Operator support and hardening evidence

Date: 2026-07-24

Status: complete

## Operator-support graph nodes

- Level Meter publishes interval per-channel RMS and peak dBFS.
- Sound Recorder writes standards-compliant IEEE-float WAV with patched RIFF
  sizes and continuous or segmented operation.
- Clip Generator joins raw audio and click triggers, retains bounded pre-roll,
  waits for post-roll, and publishes typed waveform clips with explicit
  incomplete/discontinuity state.
- Alarm and Event Counter publishes rolling-window click count and alarm state.
- Effort Monitor, Aural Listening, and User Input accept audited time-stamped
  browser/API observations into typed blocks.
- Storage Health publishes capacity, free/available bytes, percentage, and
  healthy/warning/unavailable status periodically for recording or backup
  destinations.
- Module status and Data Map displays expose node state, block retention,
  subscribers, publication, queue, drop, and observer-error counters.

These are web-adapted PAMGuard operator semantics, not claims of numerical Java
fixture parity.

## Focused evidence

`module_graph_and_typed_data_blocks` verifies:

- exact level RMS/peak result shape;
- WAV header/data length and completion event;
- pre/post-trigger clip sample range and waveform length;
- rolling alarm activation;
- default effort category and event publication; and
- real filesystem capacity reporting.

`module_runtime_http_smoke` verifies effort entry through HTTP plus level and
storage histories through the normal block API. It also stops and restarts the
service to prove stable graph/block identities, then checks executable-settings
preflight, raw and framed audio, shaped streams, and direct graph acquisition.

Additional deterministic hardening covers queued-subscriber self-unsubscribe
lifetime, observer exception isolation, drop-oldest pressure, reverse-order
lifecycle teardown, start rollback, unordered graph documents, transactional
invalid-graph rejection, and workspace display-ID validation.

## Concurrency gate

`module-runtime-soak.ps1` is duration-configurable. The final workload runs one
acquisition, a full-band FFT with Blackman-Harris window and Java click
removal, a decimator and low-frequency FFT, standalone spectrogram noise
reduction, click detection with trigger publication, and level monitoring.
It simultaneously serves a fast FFT client, a deliberately throttled
noise-reduced FFT client, a deliberately throttled binary audio client, and a
deliberately throttled click-trigger client.

The automated five-second gate requires:

- responsive synchronous PCM ingest;
- all module nodes still running;
- zero observer errors;
- bounded history and queue depth; and
- working-set growth below 256 MiB.

Observed exact-workload 60-second gate on 2026-07-24:

- 1,245 PCM chunks / 2,549,760 frames;
- four simultaneous clients;
- 62.5 ms maximum ingest request;
- 22.1 MiB maximum working-set growth.

The exact same binary and workload then passed the two-hour gate:

- 154,042 PCM chunks / 315,478,016 frames;
- seven continuously running graph nodes and four simultaneous clients;
- 112.6 ms maximum ingest request;
- 29.3 MiB maximum working-set growth; and
- no queue, history, observer, memory, or ingest invariant failure.

The complete configured suite passes `101/101` tests on the same candidate
binary, including every Java-oracle fixture, all service modes, graph-runtime
HTTP, and the five-second concurrency gate. The provider workspace additionally
passes its live headless-Chrome validation.

The exact two-hour validation can be repeated with:

```powershell
.\cpp-engine\scripts\module-runtime-soak.ps1 -DurationSeconds 7200
```

## Claim boundary

Sound Recorder and Clip Generator are intentionally portable engine
implementations. RainbowClick legacy files, Java Swing alarms/preferences, and
legacy PAMGuard colour/layout serialisation remain non-targets.
