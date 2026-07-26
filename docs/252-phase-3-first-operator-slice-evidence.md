# Phase 3 first operator-ready slice evidence

Status: **automated exit evidence complete; physical audible-output operator
confirmation still required**

Date: 2026-07-25

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

Plan: `docs/247-pamguard-authoritative-web-workflow-plan.md`

## Scope

Phase 3 makes one complete project-native monitoring path usable without
falling back to the fixed `AnalysisSession` application:

```text
Sound Acquisition -> FFT (Spectrogram) Engine -> User Display / Spectrogram
                  -> Click Detector -> Click display
                  -> Sound Output
```

The branches remain independent controlled units and display instances. The
configuration template is only an atomic construction convenience.

## Implemented evidence

| Area | Evidence |
|---|---|
| Global Array Manager | A versioned global settings descriptor, canonical adapter, typed `ArrayConfiguration` projection, project mutation, persistence, and dedicated five-section browser editor exist. It remains available through Settings on a blank project. Click localisation consumes this single geometry projection. |
| Sound Acquisition | Dedicated portable source, sampling/channel mapping, calibration, and DC-removal settings are project-owned. A separate Settings -> Host input workflow enumerates host devices or accepts an allowed HTTP(S) source, owns capture Start/Stop, and explicitly reports `needsConfiguration` until bound. Host deployment state is never written into the portable project. |
| Acquisition DSP | DC removal reproduces Java's persistent per-channel recurrence and reset semantics. Default calibration is derived from voltage, Acquisition preamplifier, selected Array Manager hydrophone sensitivity, and hydrophone preamplifier gain. |
| Production ingest | Supervised ingest, container, Compose, and Kubernetes paths target the active project plus Acquisition instance. Schema-v1 session ingest is confined to an explicitly named compatibility example. |
| FFT | The controlled unit owns its FFT, click-removal, and spectral-noise settings and deterministic hidden expansion. The dedicated browser editor exposes operational source/channel, FFT, click-removal, and spectral-noise sections. |
| Sound Output | The controlled unit owns its playable source and portable playback settings. Device choice remains browser-local. Listen/Stop uses the selected project data block and tears down on lifecycle/project/source changes. Java's run-mode multiplicity is represented directly: Normal and Mixed are `0..unlimited`, while Viewer is exactly one. Project mutations, projection, catalogue JSON/OpenAPI, and the palette's effective maximum all use the same override. |
| User Display / Spectrogram | `DisplayInstance.source` is the sole FFT-source authority. Settings v2 explicitly own the Java panel/channel list, frequency and amplitude limits, colour model, fixed-seconds or pixels-per-FFT time scale, wrap/scroll mode, and axes. The dedicated four-tab browser editor and renderer support multiple independently bound, continuously streaming display instances. |
| Click completion order | The controlled recipe classifies first, selects default or positive click-type-specific delay settings, measures delays and bearing, applies Java's absolute/inclusive angle veto, and only then publishes the annotated public click. Features and the simple train tracker consume that post-localiser click. |
| Click settings workflow | The project shell now mounts the dedicated Source, Trigger, Click Length, Delays, Echoes, and Noise tabs plus the separate pre-filter, trigger-filter, angle-veto, Classification, Train ID, and Train Localisation actions. Canonical non-classifier leaves round-trip without the generic schema form. |
| Click classifier settings | Basic and Sweep classifier types now use complete structured subtype forms rather than open objects or JSON text. Every canonical field, enum, nested filter, conditional criterion, species code, name, enable/discard flag, and classifier-order option round-trips. Validation follows the pinned Swing workers: disabled Sweep criteria may retain stale values, reversed FFT edges remain valid, and Basic's inherited `enabled` flag is persisted but deliberately ignored by classification because Java `BasicClickIdentifier` does not consult it. Portable non-null names and unique species codes `1..255` are explicit web normalizations. |
| Click display | The static display is owned by Click Detector, consumes its click/localisation streams continuously, retains bounded history, and is removed with its owner. |
| Manual tracked-click events | The Click display now owns Java-style manual event membership independently of the automatic simple-train tracker. Single/shift selection and right-click actions create or extend an event, remove a click, reassign a whole event, delete an event, and request localisation. Event membership survives runtime Stop/Start for the same project/revision and resets on project, revision, detector, or service replacement. |
| Moving-platform tracked-event localisation | Project PCM ingest accepts an all-or-none finite heading/pitch/roll pose and an all-or-none east/north/height origin. The Acquisition unit's stable ID is the navigation reference. Click Detector snapshots the latest declared pose at trigger onset, retains ordered earth-bearing ambiguities, and the tracked-event service selects navigation samples in the event time window before running the ported Least Squares target-motion fit and range/height filters. The real HTTP smoke injects five platform poses around a known target and recovers an accepted fit within 50 m. |
| Click monitoring template | The blank canvas and Add Modules menu open a branch preview before `addConfigurationTemplate` atomically creates the independent branches. It reuses exactly one Acquisition, creates one when none exists, and rejects an ambiguous multi-Acquisition project. Repeating it creates independent FFT, User Display, Click Detector, and Sound Output branches. |
| Project/runtime authority | Template expansion produces ordinary project units, bindings, displays, and layouts. It does not add a second template settings object or bypass revision, validation, persistence, or runtime guards. |

## Spectrogram vertical-slice contract

The current Java authorities are
`Spectrogram.SpectrogramParameters`,
`Spectrogram.SpectrogramParamsDialog`, and
`PamView.ColourArray.ColourArrayType`.

Portable Spectrogram settings are now:

```json
{
  "nPanels": 1,
  "channelList": [0],
  "frequencyLimits": [0, 0],
  "amplitudeLimits": [50, 120],
  "colourMap": "GREY",
  "wrapDisplay": true,
  "timeScaleFixed": false,
  "displayLength": 20,
  "pixelsPerSlics": 1,
  "showScale": true
}
```

`sourceName` is deliberately absent: the display's typed `source` reference
already identifies the FFT unit and public output role. No legacy-project
migration is carried because this is an active-development format change.
Java derives its initial panel count from the current array; the source-free
portable default materialises one channel-zero panel, while the editor's
**One panel per channel** action recreates the Java workflow once an FFT source
is selected. Duplicate panel channels remain valid, as required by Java.

The browser editor keeps Java's tab order: **Data Source**, **Scales**,
**Plug ins**, and **Mark Observers**. The first two are operational. The latter
two explicitly describe their future data-model slices rather than presenting
generic JSON or silently saving inert configuration.

The canvas renderer:

- subscribes once per display to exactly that display's projected FFT block;
- requests all unique configured channels and renders the ordered panel list,
  including repeated channels;
- applies frequency and amplitude limits plus all nine Java colour maps;
- implements fixed-duration and pixels-per-FFT axes, wrap and scroll
  positioning, panel/channel labels, and an amplitude legend; and
- keeps each display's frame history, binding, and settings independent.

Removing an FFT with `leave-unbound` clears only displays bound to that owner.
Those surfaces remain in the saved layout, show **Unbound** and
**Select an FFT source**, while displays bound to other FFT units continue.

## Current verification

The following checks passed at the Phase 3 boundary and in the later integrated
workflow validation:

- the original 132-test Phase 3 integration baseline, followed by the final
  163/163 Phase 7 integrated CTest pass recorded in
  `docs/256-phase-7-automated-validation-evidence.md`;
- `project_authority_check`;
- `project_authority_json_check`;
- `module_graph_check`;
- `controlled_unit_registry_check` against the parity manifest;
- project-service smoke, including atomic template creation, rollback, ETags,
  display ownership, persistence/restart, and low-level write isolation;
- project-Acquisition HTTP smoke, including independent host bindings,
  configuration state, project switching, and stale-binding cleanup;
- Sound Output focused settings/service/browser checks, including real
  Chromium playback lifecycle;
- project shell real-Chromium contract, including two FFTs/two Spectrograms,
  independent channels/ranges/time modes, continuous rendering, stable
  save/restart identities, selective FFT-source loss/unbinding, Array Manager
  OK/Cancel, Click monitoring template preview/creation, and dedicated Click
  settings round-trip. The template path additionally proves Host input
  OK/Cancel, explicit Sound Output channel selection, global Start/Stop,
  injected multichannel PCM, a live Spectrogram, continuously retained Click
  detections, browser AudioWorklet delivery with a non-zero received-frame
  count, durable Save As, service restart, and restoration of the same unit,
  binding, display, and graph-layout identities. It also proves that the
  host-only Acquisition binding is not leaked into the saved project;
- focused Spectrogram Node contract for multi-panel channel filtering,
  independent frame histories, colour rendering, and operational time axes;
- controlled-unit manifest/catalogue and project-projection checks for
  Spectrogram settings v2 and duplicate-source rejection; and
- exact Basic/Sweep classifier schema, Java/C++ classifier fixtures, project
  round-trip, and structured-browser contracts with no raw JSON subtype
  editor;
- Java-semantics fixtures, service API checks, browser contracts, and the real
  Chromium workflow for manual tracked-click event creation, extension,
  click removal, whole-event reassignment, deletion, and localisation status;
- the project-native tracked-click localisation HTTP smoke, which injects five
  posed two-channel PCM chunks, verifies trigger-onset navigation/orientation
  and ordered earth-bearing ambiguity retention, assigns the retained clicks
  to one event, and recovers a known target through the Least Squares run with
  navigation-beam, range, and height-filter evidence;
- the capture-lifecycle real-Chromium smoke, including two independent
  captures, reload hydration, per-Acquisition stop, stale-revision conflict,
  dead-child reaping, runtime quiescence, and graph-reconfiguration
  quiescence;
- the repeatable project-owned physical Acquisition smoke against
  `Microphone (Razer Kiyo X)`: exact server enumeration and host binding,
  project runtime Start, DirectShow/FFmpeg ingest, nine retained raw-audio
  chunks in the projected Acquisition block, capture Stop, and verified child
  reaping. A direct probe additionally confirms the device's native 44.1 kHz
  stereo format; and
- a 60-second project-authoritative concurrency soak with Acquisition, FFT,
  two Spectrogram instances, Click Detector, and Sound Output ownership:
  1,255 chunks / 5,140,480 frames, 20,204 FFT units, 2,526 clicks, six
  fast/throttled FFT-click-audio streams, 22.7 ms maximum ingest, 48.5 MiB
  working-set growth, maximum observed queue depth four, and clean drain/stop.
  Its repeated eight-chunk prefix produced identical counts and SHA-256
  scientific payload signatures with and without presentation pressure
  (126 FFT units and 16 clicks);
- acquisition, lifecycle, capture, ingest, scientific fixture, and concurrency
  tests included by the full CTest baseline.

These results complete the automated part of the Phase 3 exit gate. Phase 4
through Phase 6 have since passed their own integration boundaries. Physical
audibility remains intentionally human-confirmed rather than inferred from
browser PCM delivery.

## Exit-gate work still open

- Complete the audible physical-output operator check. Browser AudioWorklet
  delivery, source ownership, teardown, and non-zero PCM frame receipt are
  automated, but a human must confirm the selected physical device is heard.

## Deviations and exclusions

Live timestamped GPS/attitude ingestion and sub-chunk pose interpolation remain
future integrations, not a Phase 3 exit dependency. The implemented project
contract accepts explicit chunk-cadence pose/origin samples; it does not invent
a sensor feed or interpolate data it was not given.

- Java's legacy Swing colours/layout and Java-specific display preferences are
  not parity targets.
- RainbowClick file writing/import and Java sound alarms are excluded.
- Browser device identifiers are host-local and are not portable project
  settings.
- Java `TrackedClickLocaliser` groups manually marked/tracked events. That
  membership/reassignment workflow remains separate from automatic simple
  click trains. Numerical Least Squares target-motion localisation is
  available only when every observation has compatible navigation-reference,
  origin, heading, and bearing ambiguity inputs. Missing prerequisites still
  return an explicit unavailable result; no synthetic position is substituted.
- The stable PCM API currently supplies pose/navigation at chunk cadence.
  Live GPS device integration, navigation interpolation, and the alternative
  simplex/MCMC target-motion algorithms are not claimed.
