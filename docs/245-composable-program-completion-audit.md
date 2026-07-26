# Composable programme completion audit

Date: 2026-07-24

Status: **complete**

Java authority: PAMGuard `2.02.18e`,
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Work-package audit

| Package | Result | Primary evidence |
| --- | --- | --- |
| A — typed runtime | Complete | Typed units/blocks, stable metadata, fan-out, bounded history, synchronous scientific and queued presentation subscribers, explicit drops/errors, self-unsubscribe safety |
| B — registry/graph | Complete | Compiled-in type registry, named instances, full schemas/defaults, typed/capability validation, cycle/dependency checks, optimistic transactional apply, stable JSON persistence |
| C — signal routing | Complete | Acquisition, amplifier, patch panel, IIR filter, Java-pinned decimator, and unlimited independent FFT instances |
| D — detector migration | Complete for every engine-supported detector | Click/features/classifiers/trains/MHT/localisation, Whistles & Moans, noise/noise-band, LTSA, all Ishmael paths, and matched-template nodes reuse the existing parity-tested kernels |
| E — graph/stream API | Complete | Catalogue, validation/apply, compatible sources, lifecycle/health, history, selected NDJSON, raw/PGA1 audio, operator entry, capture, and workspace routes |
| F/G — workspace/spectrogram | Complete | Seven-provider shell, multiple displays/workspaces, service restore, grid/tabs/resize/full-screen/pop-out, independent spectrogram controls, overlays, cursor/time groups, marks, and explicit source loss |
| H — Sound Output | Complete for live operation | Any raw block, source channels, direct/mono/stereo, gain/mute, high-pass, resampling, latency target, device selection, PGA1 timing, underrun and separate transport/output-drop health |
| I — operator support | Complete for the planned web-safe slice | Typed Level Meter, WAV recorder, trigger clips, alarm/counter, status/remedial navigation, Data Map time navigation, effort, aural listening, user input, and periodic storage health |
| J — hardening | Complete | Deterministic lifecycle/pressure/persistence/browser tests, `101/101` CTest, and the exact-candidate two-hour multi-client soak are green |

## Definition-of-done audit

| # | Requirement | Evidence |
| --- | --- | --- |
| 1 | Typed inspectable graph is the primary runtime | `/module-graph`, `/module-runtime/status`, `/data-blocks`; browser capture defaults to graph Acquisition |
| 2 | Multiple named instances | Graph editor add/name/duplicate/remove; no artificial FFT/display singleton |
| 3 | Shared live source catalogue | Typed/capability-aware graph choices and per-display block choices |
| 4 | Independent routing branches | Acquisition → full FFT and Acquisition → Decimator → low FFT is exercised continuously |
| 5 | Two spectrograms plus click display | Headless browser smoke restores and renders that service-saved workspace |
| 6 | Independent spectrogram controls | Source/channel/frequency/dB/colour/mode/cadence/time/freeze/waveform/overlay/sync settings persist per instance |
| 7 | Custom persisted workspaces | Named layouts survive service restart with stable display/source IDs |
| 8 | Selectable Sound Output | Any playable raw block plus channel/mix/gain/mute/filter/rate/latency/device controls |
| 9 | Detector maths retains Java authority | Existing fixture corpus stays green; graph wrappers reuse kernels; decimator adds Java cross-chunk parity |
| 10 | Changes/loss/pressure/dropouts explicit | Structured graph issues, missing-source UI, revision-ending streams, counters/discontinuities, PGA1 transport drops, AudioWorklet health |
| 11 | Graph/workspace stable save/reload | Both are process-restart tested; graph retains module/block/connection revision and identities |
| 12 | Complete vertical slice validated | Focused graph test, module-runtime HTTP smoke, headless Chrome, exact 60-second gate, full CTest, and exact two-hour multi-client soak all pass |
| 13 | Honest API/documentation | OpenAPI 3.1 plus `docs/241`–`docs/245` and updated README/status |

## Validation snapshot

- Build: MSVC/CMake/Ninja clean.
- CTest: `101/101` passed, zero failures.
- Module graph focused test: passed.
- Module-runtime HTTP smoke: passed.
- Headless-Chrome workspace smoke: passed.
- Exact-workload 60-second gate: 1,245 chunks / 2,549,760 frames, four
  clients, 62.5 ms maximum ingest, 22.1 MiB working-set growth.
- Two-hour exact-candidate soak: 154,042 chunks / 315,478,016 frames, seven
  live nodes, four simultaneous clients, 112.6 ms maximum ingest, and
  29.3 MiB peak working-set growth. No queue, history, observer, memory, or
  ingest invariant failed.
- Java FFT click-removal fixture: maximum absolute error `1.42109e-13`;
  disabled-removal negative control diverges by 613.

## Boundaries after completion

- The fixed `AnalysisSession` API remains a compatibility, import, archive,
  and regression surface. It is not the browser's composition model.
- The module registry is compiled in; a stable public DLL/plugin ABI is a
  later design decision.
- PAMGuard Swing layout/colour/preferences, RainbowClick files, Java alarms,
  and Java serialisation are non-targets.
- Live Sound Output follows source time. Playback-speed control belongs to a
  later offline viewer.
- Storage health is active and periodic. Destructive copy/move/FTP backup
  policies require a separate explicit destination/credential/retention
  design.
- Scientific parity remains exactly as stated in the parity ledger; wrapping a
  foundation-level algorithm in a graph node does not promote its claim.
