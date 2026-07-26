# Operator workspace, streaming, and audio evidence

Date: 2026-07-24

## Claim

The browser now provides a PAMGuard-style composable operator workspace rather
than one fixed spectrogram/click page.

## Module graph editor

The Module Graph tab discovers the service catalogue and presents it through a
PAMGuard-style visual editor. It supports:

- a searchable categorized palette and drag/drop module creation;
- a pannable/zoomable canvas with draggable named module nodes;
- typed output-to-input socket dragging with compatible-target highlighting;
- right-click Configure/Inspect/Rename/Duplicate/Enable/Reset/Remove actions;
- guided PAMGuard-ordered settings forms with Advanced JSON as a fallback;
- blue processing, plum display, and orange output connection lines;
- undo/redo, fit, automatic layout, keyboard deletion, and saved positions;
- local JSON validation, server graph validation, and transactional apply;
- visible authoritative revision and unapplied-change state; and
- start, stop, flush, and reset controls.

The legacy fixed-session sidebar is hidden in this view so it cannot be
mistaken for part of the composable workflow. See `docs/246`.

Sound-card or URL capture can target a running Acquisition graph instance.
The FFmpeg bridge accepts `--module` and posts directly to that instance.
The legacy session target remains available but is not the default when a
graph acquisition exists.

## Display-provider workspace

Registered browser providers are:

- spectrogram;
- click/event list;
- raw waveform;
- generic numeric time plot;
- level meter;
- module status; and
- data map/block health.

Operators can add unlimited supported displays, rename/duplicate/remove them,
choose sources/channels, split-grid or tab them, resize/full-screen/pop out
panels, and save named layouts locally and in the service. A fresh browser
restores the first service workspace when it has no newer local layout. Missing
sources stay visible as missing and are never silently rebound. The Level
Meter display accepts either raw audio or the typed Level Meter module output.
Module-status rows open the affected graph card, while keyboard-accessible
Data Map rows expose retained time/sample bounds and navigate their
synchronized display group.

Each spectrogram independently stores source/channel, frequency and amplitude
range, colour map, scroll/wrap mode, cadence, time span, live/frozen state,
waveform, overlays/style, height, and synchronized-time group. Pointer cursors
are shared by group. Clicking a spectrogram can publish a typed display mark
through an Effort, Aural Listening, or User Input module.

## Streaming

NDJSON block subscriptions select channels, sequences, FFT-bin range, cadence,
history, and format. Raw-audio payloads are physically trimmed to requested
channels. Subscriber queues and transport queues are bounded; a transport
overflow marks the next unit discontinuous and reports cumulative
`presentationDropped`. Graph replacement ends old-revision streams.

Binary audio streams select any playable raw block and channels. The browser
AudioWorklet supports direct/mono/stereo mix, gain, explicit mute, high-pass,
output resampling, selectable latency target, output device where supported,
and visible buffered time and underruns. Its PGA1 stream framing adds per-chunk
Unix time, start sample, and cumulative server-transport frame drops, allowing
the workspace to show estimated ingest-to-output latency and separate
transport/output drop counts. Unframed raw f32le remains the compatibility
default.

## Evidence

- `workspace-browser-smoke.ps1` runs Chrome headlessly against a live service
  and verifies graph discovery, the visual graph editor, all seven providers,
  split/tab controls, two independent spectrograms, click and typed-level
  displays, explicit source loss, status/Data Map actions, and a service-saved
  workspace restored after process restart.
- `visual-graph-browser-smoke.ps1` uses Chrome DevTools to exercise module
  creation, typed socket dragging, the right-click Configure action, guided FFT
  editing, validation, transactional Apply, physical node dragging, and
  browser-reload layout restoration.
- `module_runtime_http_smoke` verifies FFT/event history, selected raw-channel
  payload trimming, raw and PGA1 audio framing/timing/drop metadata, persisted
  graph restoration, executable-settings preflight, and direct
  FFmpeg-to-module ingest.
- OpenAPI 3.1 describes module, graph, block, history, stream, audio, workspace,
  operator-input, lifecycle, and capture contracts.

## Claim boundary

Browser pop-outs use normal browser windows and therefore depend on the
operator allowing pop-ups. Output-device selection depends on
`AudioContext.setSinkId`. Playback speed remains an offline/viewer extension;
live monitoring intentionally tracks source time. The displayed latency begins
at the acquisition timestamp supplied to the graph; FFmpeg device/codec
buffering before that timestamp is not measurable by this transport.
