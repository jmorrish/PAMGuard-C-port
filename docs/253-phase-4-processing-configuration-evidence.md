# Phase 4 processing and detector configuration evidence

Status: **Phase 4 exit gate passed**

Date: 2026-07-25

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

Plan: `docs/247-pamguard-authoritative-web-workflow-plan.md`

## Promotion rule

A Phase 4 unit is not complete merely because a runtime type or schema exists.
Its controlled-unit descriptor, dedicated PAMGuard-shaped settings surface,
defaults, source/channel semantics, canonical project round-trip, runtime
projection, and relevant Java/scientific fixtures must all pass. Settings do
not have a second browser-local or low-level runtime authority.

## Integrated units

| Unit | Authority-backed implementation evidence |
|---|---|
| Signal Amplifier | Canonical settings contain exactly 32 absolute-channel `{gainDb, invert}` rows. The Java default is 0 dB and not inverted. Runtime gains use the signed Java conversion `pow(10, dB / 20)`, with inversion represented by a negative sign. The dedicated editor shows only channels supplied by the selected source while preserving canonical hidden-channel values. |
| Patch Panel | Canonical settings contain Java's 32-by-32 boolean input/output routing matrix with an identity default. The dedicated editor presents active source inputs against all output channels and includes identity/clear actions. A nullable arbitrary-gain matrix is retained only as a clearly labelled C++ extension; when present it replaces the unit coefficients implied by Java's routing matrix. |
| Filters (IIR and FIR) | Canonical settings own the channel bitmap and filter parameters. The default is Java's Butterworth band-pass, order 4, 2 kHz high-pass corner, 20 kHz low-pass corner, pass/stop ripple 2, and gamma 3. The editor follows Java's filter-type and band order and conditionally exposes corners, order, ripple, gamma, and arbitrary-FIR points without a raw JSON escape hatch. |
| Decimator | Canonical settings own output sample rate, channel bitmap, interpolation, and the nested anti-alias filter. The default is 2 kHz output with Butterworth low-pass order 6 at 1 kHz and interpolation disabled. The editor derives the input rate from its selected source, validates the output rate, offers Java's default filter action, and normalises interpolation using the exact integral-rate relationship. |
| Noise Monitor | The FFT source and channel map are graph-owned/derived. Canonical settings preserve Java's interval, measure-count/use-all rule, custom bands, and standard octave/decade family generation. The dedicated editor reproduces `NoiseControl.createBands`, including Java's source-FFT resolution limits, without duplicating FFT length or hop. |
| Noise Band Monitor | Canonical settings cover raw-audio channels, interval, band family, frequency limits/reference, Butterworth or FIR-window filtering, IIR/FIR order, and FIR gamma. The dedicated editor preserves Java's band/filter order and even-IIR validation; excluded plot preferences do not leak into scientific settings. |
| LTSA | Canonical settings cover FFT channels, measurement interval, and Java's persisted longer factor. The editor explicitly labels the longer factor as persisted but dormant in the current runtime rather than implying an effect it does not have. |
| Whistle and Moan Detector | The dedicated editor follows Java's `Detection` then `Noise and Thresholding` order, grouped FFT channels, frequency bounds, 4/8 connectivity, region/fragmentation controls, and the supported spectral-noise chain. Runtime projection refuses unsupported noise-reduction combinations instead of silently changing them. |
| Ishmael Energy Sum | The editor follows source, detector, then shared peak-picking order. It derives FFT source geometry and exposes energy-band, ratio, adaptive-threshold, and peak controls through canonical settings. |
| Ishmael Spectrogram Correlation | The editor preserves the grouped FFT source and a structured contour-segment model with an operational preview, spread, and shared peak controls. It does not accept a raw JSON contour. |
| Ishmael Matched Filter | The editor uses raw audio, imports the first channel from WAV/AIFF-compatible input, preserves kernel history and samples, shows a waveform, and exposes the shared peak controls. Host file selection is converted into portable kernel content. |
| Click Train Detector (MHT) | One controlled unit owns the MHT detector/classifier runtime. The dedicated `Detector`, `Pre Classifier`, and `Species Classifiers` tabs preserve exact Java defaults and click-source channel groups. Its required click, feature, localisation, and bearing inputs are explicit. The exact Java `maxChi = 2E17` sentinel is serialized as an integer JSON token so full project inspection remains canonical and lossless. |
| Matched Template Click Classifier | The dedicated classifier editor preserves the exact 192 kHz Beaked Whale/Dolphin built-ins, waveform previews, normalisation, peak search, restricted length, click type, per-classifier thresholds, CSV import, project round-trip, runtime click annotation, and classification output. The secondary Java integration boundaries below remain explicit. |
| Level Meter | Canonical settings pin Java's `minLevel=-80`, full-scale reference, and peak measurement defaults, plus the exact full-scale/volts/micropascal and peak/RMS enum values. The dedicated editor preserves Java's `Double` parse, integer truncation, and `-abs(value)` storage behaviour. One Level Meter owns exactly one static display sourced from its own `levels` output. Runtime peak and RMS calculations match `LevelMeterSidePanel`; the browser applies Java's full-scale, volts, or per-channel micropascal conversion and shows an explicit unavailable state when the selected source lacks the required calibration metadata. |

These units are ordinary project graph nodes. Their source pickers and graph
lines read and write the same `rawAudio` binding. If more than one compatible
source exists, adding a required-input unit leaves it visibly
`needs-configuration`; the authority never chooses an arbitrary first source.
Selecting a source in the settings dialog makes the graph runnable.

## Scientific and browser verification

- Signal-routing Java fixture export covers five cases and compares 216 values
  with zero error, including signed gain, sparse absolute channels,
  calibration propagation, mixing, duplication, and an empty route.
- Existing Java/C++ IIR fixtures pin the filter coefficient and response
  implementation used by the standalone Filter.
- The Decimator fixture matches Java output exactly for the pinned cases,
  including rate conversion, anti-alias filtering, chunk boundaries, and
  channel/sample metadata.
- Focused controlled-unit, projection, JSON, fixture, and dedicated browser
  contracts pass for every Phase 4 unit.
- Noise Monitor, Noise Band Monitor, and LTSA pass an eight-test focused
  manifest/catalogue/project/runtime/scientific suite. The regenerated Java
  defaults fixture has SHA-256
  `B6EA73B029B7DD07F495B195503C3B9C1DEA340F2D51635C9B2E157F49105299`.
- Level Meter passes its Java-default fixture, exact C++ peak/RMS runtime
  math, manifest/catalogue/project projection, dedicated settings/display
  browser contracts, and the wider 14-test focused integration slice.
- The real-Chromium project-shell test adds and configures Signal Amplifier,
  Patch Panel, Filter, Decimator, Noise Monitor, Noise Band Monitor, LTSA,
  Whistle and Moan, all three Ishmael detectors, MHT Click Train, Matched
  Template, and Level Meter through their dedicated forms. It proves source
  bindings and standard Noise Monitor band creation, starts the resulting
  runtime, saves the project, restarts the service, and restores exact
  settings, sources, identities, and layout.
- The same real-Chromium workflow now also proves Level Meter's static
  owner/display identity, calibrated live two-channel stream, durable
  save/restart, and owner-cascade removal.
- The complete MSVC/CTest baseline passes all **149/149** configured tests,
  including scientific fixtures, controlled-unit projection, project
  authority, service/lifecycle tests, standalone browser contracts, real
  Chromium, and concurrency soak.
- The expanded real-Chromium Phase 4 path was rerun after that baseline and
  passed with more than **650** recorded browser requests and no
  `/sessions`, `/workspaces`, or low-level `/module-graph` request.

## Portable deviations

- The Amplifier does not reproduce Swing presentation or Java's immediate
  dialog-time mutation. Accepted project changes obey the controlled-unit
  stop-required policy.
- Java derives some measured-amplitude calibration through Acquisition and
  hydrophone mappings. The portable graph preserves calibration metadata but
  host-specific measured-amplitude UI remains outside this settings slice.
- Patch Panel's advanced arbitrary-gain matrix is a C++ extension and is
  labelled as such; Java checkbox routing remains the default and parity path.
- Java arbitrary-FIR file browsing is host-specific. Portable projects retain
  structured frequency/gain control points rather than a host file path.
- Java Viewer/offline-file tabs are not presented as live Normal-mode
  processing settings.
- Matched Template project click types `128..255` are deliberately preserved
  as stable unsigned integers. The editor's displayed `256` is represented by
  portable `0`. Java instead writes the `100..256` spinner value to a signed
  byte, so `128..255` reopen as negative values and `256` wraps to zero; the
  web project does not reproduce those reopen and downstream defects.
- Matched Template currently classifies individual click detections only.
  Java's selectable `CTDataUnit` source, its `AverageWaveform` classification,
  and its replacement/writing of `CTClassifierType.MATCHEDCLICK` click-train
  classifications are not yet implemented.
- Java's Matched Template `ClickTypeProvider` code/name integration for
  downstream consumers is not yet implemented.
- Browser template import currently supports CSV and the built-in templates.
  Java's MAT template importer is not yet available.

## Exit decision and remaining cross-phase boundaries

The Phase 4 exit gate is passed: every unit in the phase has an
authority-backed dedicated settings surface, one canonical project setting,
typed runtime wiring, source semantics, and fixture/round-trip evidence.

This does not relabel partial implementations as fully PAMGuard-equivalent.
Tracked click-train target-motion service wiring remains a Phase 3 scientific
boundary, and the Matched Template secondary integrations listed above remain
recorded deviations. Sound Recorder, Clip Generator, Alarm, Effort Monitor,
Aural Listening, User Input, graph-native storage/archive, compatibility
cleanup, long acquisition/display/audio soak, and operator acceptance belong
to Phases 5-7.
