# Click Detector Settings Parity

Date: 2026-07-23

Authority: pinned PAMGuard `2.02.18e` / commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

## Scope

This ledger covers settings that change click-detector processing or emitted
data. Swing/JavaFX layout and colours, RainbowClick legacy files, alarms, and
Java-only display preferences are explicitly out of scope.

The compatibility policy is intentionally simple while the port is in active
development: new C++ sessions use PAMGuard defaults. There is no legacy C++
session migration layer.

## Settings matrix

| PAMGuard surface | PAMGuard default | Port status |
|---|---:|---|
| Source channel bitmap | channels 0 and 1 (`3`) | Implemented in engine, service, importer, and browser |
| Grouping | all selected channels together | Implemented as independent detector instances; each group owns trigger, IIR, waveform history, and echo state |
| Trigger bitmap | all channels | Implemented |
| Minimum trigger channels | `1` | Implemented |
| Threshold | `10 dB` | Java-fixture parity |
| Long trigger filter | `0.00001` | Java-fixture parity |
| Long trigger filter 2 | `0.000001` | Exposed and round-tripped; deliberately a runtime no-op because pinned Java never passes it to the live `TriggerFilter` |
| Short trigger filter | `0.1` | Java-fixture parity |
| Pre/post samples | `40` / `40` | Java-fixture parity |
| Minimum separation | `100` samples | Java-fixture parity |
| Maximum length | `1024` samples | Java-fixture parity |
| Pre-filter | Butterworth high-pass, order 4, 500 Hz | Implemented and now the C++ default |
| Trigger filter | Butterworth high-pass, order 2, 2000 Hz | Implemented and now the C++ default |
| IIR types/bands | None, Butterworth, Chebyshev; all four bands | Java-fixture parity and browser/API exposure |
| FIR window / arbitrary / FFT filters | selectable in PAMGuard's shared filter pane | Implemented, exposed, and validated sample-by-sample against Java for all three runtimes |
| Online echo detector | off | Implemented with per-channel-group state |
| Discard echoes | off | Implemented |
| Simple echo maximum interval | `0.1 s` | Java-fixture parity and browser/API exposure |
| Sample noise waveforms | on, every `5 s` | Implemented and emitted as `clickNoiseSamples` |
| Store trigger background | on, every `5000 ms` | Implemented and emitted as `clickTriggerBackground` |
| Publish trigger function | off | Implemented and emitted as `clickTriggerFunction` when enabled |
| Classifier selection | sweep classifier | Basic, Sweep, and none are implemented in engine, API, importer, and browser |
| Run classification online | off | Implemented policy |
| Discard unclassified | off | Implemented for Basic and Sweep classifiers |
| Basic classifier types and criteria | empty type list | Runtime and Java fixtures implemented; presets and JSON editor exposed |
| Sweep classifier sets and criteria | empty set list | Runtime, all criteria, all channel modes, presets, `.psfx` import, API, and browser JSON editor implemented; real `SweepClassifierWorker` fixture parity |
| Default delay measurement | raw waveform, no filter, no envelope, no upsampling, unrestricted | Implemented in engine, service, importer, and browser |
| Delay filter | off | All four FFT bands implemented; Java-oracle parity |
| Envelope / leading edge | off / off | Implemented; Java-oracle parity |
| Delay upsampling | `1` | Implemented with Java's zero-stuff + sixth-order Butterworth path; Java-oracle parity |
| Restricted delay bins | off; stored value `80` | Implemented; Java-oracle parity |
| Per-click-type delay overrides | use default | Implemented and selected from online classification species code |
| Instant click delays and bearings | geometry constrained | Foundation and multiple Java fixtures implemented |
| Angle vetoes | none | Implemented as the separate `Detector Vetoes` list in engine, service, `.psfx` importer, OpenAPI, and browser; inclusive bounds and legacy first-pair bearing have real-Java fixture parity |
| Event/group localisation limits and algorithms | first algorithm selected; 20 km range; depth -5 to 5000 m | Separate click-event localisation path; not yet equivalent |

## Required claim boundary

The click detector must not be called a full PAMGuard clone while any row above
is marked as a gap. Unsupported settings are rejected or omitted from the UI;
they are never accepted and silently ignored.
