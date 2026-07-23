# Live Sound-Card Capture from the Web UI

Date: 2026-07-23

## Purpose

Until now, capturing from a sound card meant hand-assembling an `ffmpeg_stream_ingest` command line with dshow options — accurate but hostile. This slice makes the PAMGuard-style flow work from the browser: open the console, pick an audio device from a dropdown, press **Start capture**, watch live results. It also reorganises the session form, which had grown into a wall of textboxes, into collapsible groups.

## Capture API (service)

Opt-in via `PAMGUARD_CAPTURE_ENABLED=1` — the service spawns no processes unless the operator enables it. Windows/DirectShow only (other platforms answer 501). All endpoints require the API key when one is configured.

- `GET /capture/devices` — enumerates devices via `ffmpeg -list_devices` (`PAMGUARD_FFMPEG_PATH` overrides the executable; default `ffmpeg` from PATH).
- `POST /capture/start` `{sessionId, device}` — the session must already exist; its **own** `sampleRateHz`/`channelCount` drive the capture (no divergent-rate footgun). The service spawns `ffmpeg_stream_ingest.exe` (found next to the service exe, or `PAMGUARD_INGEST_EXE`) with `--restart --resume-from-engine`, so drop-outs reconnect and restarts resume at the engine's expected sample.
- `POST /capture/stop` `{sessionId}` — ends the capture.
- `GET /capture/status` — running captures with pid and liveness. `GET /health` now reports `captureEnabled`.

**Security posture** (extends `docs/212`): no shell is ever invoked — both enumeration and capture use `CreateProcess` with explicitly quoted argument vectors; the device name must **exactly match an enumerated audio device**, so no user-composed string reaches a child command line; the API key travels to the child via inherited environment (`PAMGUARD_CAPTURE_API_KEY`), never on the inspectable command line; the child lives in a kill-on-close **Job Object**, so stop (or service exit) takes the whole ffmpeg tree down — verified: zero `ffmpeg`/`ffmpeg_stream_ingest` processes survive a stop.

## Web UI

- **Live capture panel** at the top: device dropdown (populated from `/capture/devices`, with a clear message when capture is disabled server-side), Refresh / Start / Stop. Start creates the session from the current form if it doesn't exist, then starts capture and begins a 2-second **live poll** of `GET /sessions/{id}/results?sinceSeq=K`, feeding the metric tiles, monitoring cards, and contour summaries continuously. Feed results carry no spectrogram preview, so the canvas keeps its last image instead of blanking.
- **Collapsible config groups** replace the flat form: Array geometry, Test signal & result preview, Click detector, Click features, Whistle & moan peaks, Whistle & moan contours, and Monitoring & extra detectors are `<details>` sections, collapsed by default — the default view is API base, capture panel, session basics, and the action buttons.

## Validation

Suite still `82/82`; service smoke passes (capture defaults off — no behaviour change unless enabled). Live end-to-end check on real hardware: device enumerated ("Microphone (High Definition Audio Device)"), capture started through the API, **6.7 s of real microphone audio ingested** (319,488 samples at 48 kHz), the results feed carried 78 sequenced results at schema v28 for the UI poller, stop left zero child processes. The UI script block parses and all seven `<details>` groups are balanced.

## Claim boundary

Windows/DirectShow only — Linux capture (alsa/pulse) is a config-string change in the ingest spawn but is unimplemented and reports 501 rather than pretending. One capture per session. Device names are matched exactly; two devices with identical dshow names would be indistinguishable (a dshow limitation). The browser cannot pick the capture *sample rate* independently of the session — deliberately, since the session defines the analysis rate; ffmpeg resamples the device to it. The UI reorganisation is grouping, not a redesign: an analyst-grade UI (device level meters, per-detector pages, persistent layouts) remains future work.
