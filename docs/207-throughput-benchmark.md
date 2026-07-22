# Throughput Benchmark

Date: 2026-07-22

## Purpose

Meets the original acceptance criterion the status docs have been honest about missing: *"Load tests demonstrate target concurrency for live streams"* (`docs/01`), with WP9's number: *"50+ concurrent users are supported under agreed session/source mix."* The 50-session load smoke was always a functional isolation check; this is the measurement.

## The tool

`cpp-engine/scripts/service-throughput-bench.ps1` starts the service, creates N sessions, and streams synthetic audio (broadband noise + a tone + periodic transients, so the detectors genuinely fire) into all of them in 1-second chunks as fast as the service accepts, measuring:

- **Realtime factor** — aggregate audio-seconds processed per wall second. ≥ 1.0 means the machine sustains N live sessions; the value says by how much.
- **Per-chunk latency** p50/p95/p99/max.

`-Detectors` enables the expensive, honest configuration: click detection + localisation + click trains, and the whistle chain including noise reduction (median filter + threshold). Without it the benchmark measures little beyond HTTP and FFT.

It is a **measurement tool, not a CI gate** — numbers belong in a dated doc, not in an assertion that fails on a slow laptop. It exits 2 when the factor drops below 1.0 so scripted use can still detect "does not sustain".

## Measured baseline — 2026-07-22

Machine: the development workstation (Windows 11, MSVC Release build, service defaults, single-threaded posting client). Config: 48 kHz, 2 channels, FFT 512/256, click detection + localisation + trains, whistle + noise reduction. 20 s of audio per session in 1 s chunks.

| Sessions | Total audio | Wall time | Realtime factor | p50 | p95 | p99 | max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 25 | 500 s (192 MB PCM) | 18.1 s | **27.6×** | 34.6 ms | 47.4 ms | 53.8 ms | 60.0 ms |
| 50 | 1000 s (384 MB PCM) | 37.5 s | **26.7×** | 36.0 ms | 48.1 ms | 56.0 ms | 64.2 ms |

Two readings worth stating:

- **The WP9 criterion is met with a wide margin**: 50 detector-loaded live sessions are sustained at ~27× realtime on one workstation. Naive extrapolation says several hundred sessions before saturation on this machine, before any horizontal scaling.
- **Scaling from 25 → 50 sessions is essentially linear** (27.6× → 26.7×): no contention cliff in the session map, archive-off path, or detector state at this range.

The posting client is single-threaded, so these numbers *understate* the service (which runs an HTTP thread pool); a parallel client would raise the factor further. That direction of error is the safe one.

## Claim boundary

One machine, one mix, one config, 20 s per session. Not measured: multi-hour soak (memory growth over days), archive-enabled throughput (disk-bound), many-channel arrays (the localisation pair count grows quadratically), or genuinely concurrent posting clients. The tool takes parameters for session count, duration, channels, and chunk size precisely so those runs can be made and recorded here when they matter to a deployment.

"Users" in WP9's phrasing conflates sessions and viewers; this measures **sessions** (the expensive dimension). Viewer-side GET load is comparatively trivial and untested here.
