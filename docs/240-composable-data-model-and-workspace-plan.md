# Composable data model and operator workspace

Status: **Complete**

Date: 2026-07-24

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Completion result

All work packages A-J and all 13 complete-program requirements are implemented
and validated. The final exact-candidate gate ran the seven-node vertical slice
for two hours with four simultaneous fast/slow FFT, click-event, and audio
clients: 154,042 chunks / 315,478,016 frames, 112.6 ms worst ingest, and
29.3 MiB peak working-set growth, with no queue, history, observer, memory, or
ingest invariant failure.

The full configured suite passes `101/101` CTest checks. Detailed implementation
evidence is recorded in `docs/241`-`docs/244`; the requirement-by-requirement
close-out and retained boundaries are in `docs/245`.

## Objective

Turn the current parity-tested C++ detector engine and fixed browser console into
a PAMGuard-style composable monitoring system.

Operators must be able to create multiple named processing-module instances,
connect compatible module outputs to downstream inputs, create and arrange any
number of display instances, select the source and channels for each display,
and listen to any compatible live audio stream.

The target is the flexibility of PAMGuard's data model and module graph, not a
pixel-for-pixel port of the Swing interface.

## Why this is the next architectural step

The current C++ `AnalysisSession` owns one FFT configuration and constructs its
detectors directly. The browser similarly contains one fixed spectrogram and a
fixed set of display tabs. Adding a second spectrogram as another special case
would leave this limitation in place.

PAMGuard instead uses:

- typed `PamDataUnit` values;
- discoverable `PamDataBlock` streams;
- `PamProcess` subscriptions and output blocks;
- named `PamControlledUnit` module instances;
- a `PamModuleInfo`/`PamDependency` registry;
- source selection by compatible data type;
- display providers capable of creating multiple independent displays; and
- settings that restore the module graph and display layout.

Those semantics are the authority for this plan. C++ and browser-native
implementations may improve type safety, stable identity and transport design,
but must preserve the operator-visible composition model.

## Target vertical slice

```text
Acquisition
│
├── Raw audio ───────────────────────→ Sound Output
│
├── FFT: full bandwidth ─────────────→ Spectrogram A
│
├── Decimator
│   └── FFT: low-frequency ──────────→ Spectrogram B
│
├── Filter / Patch Panel
│   └── Click Detector ──────────────→ Click Display
│                                    ├→ Alarm
│                                    └→ Clip Generator
│
└── Other detectors ────────────────→ compatible displays and overlays
```

Cropping a display to a frequency range and creating a genuinely decimated
audio branch are separate operations. The finished system must support both.

## Design principles

1. The newer Java PAMGuard checkout remains authoritative for module behaviour,
   data semantics and defaults.
2. Existing parity-tested maths is wrapped as graph nodes, not rewritten.
3. Module instances and data blocks use stable opaque IDs. Names are editable
   labels, not identities.
4. Processing connections are typed and validated before a graph starts.
5. The processing graph is independent from the UI layout.
6. Displays consume data blocks; they do not own detector or FFT processing.
7. Display and listening subscribers may drop stale presentation data under
   pressure. Scientific processing paths may not silently drop data.
8. Reconfiguration is transactional: validate first, then apply or reject the
   complete change.
9. Configuration, graph topology and workspace layout are versioned and
   migratable.
10. The first plugin boundary is a compiled-in module factory registry. A public
    DLL/plugin ABI is deferred until the internal contracts are stable.
11. Java Swing components and Java serialisation are not port targets.
12. Every migrated scientific module retains or strengthens its Java-fixture
    validation.

## Work package A — typed runtime data model

Port the semantics of `PamDataUnit`, `PamDataBlock`, `PamObservable`,
`PamObserver` and `PamProcess`.

### A1. Data-unit envelope

Every unit carries the common facts required to route and align data:

- stable data-unit type ID and schema version;
- source data-block ID;
- timestamp and monotonic sample position;
- duration in samples/time where applicable;
- channel or sequence bitmap;
- UID/sequence number;
- continuity and clock-domain metadata; and
- typed payload.

Initial payload types:

- raw audio;
- FFT frame;
- click;
- click classification;
- click train;
- whistle/moan contour;
- noise measurement;
- LTSA period;
- localisation/bearing result; and
- module status/warning.

### A2. Data blocks

Each data block declares:

- stable ID, editable name and producer module;
- data-unit type and schema;
- sample rate and clock domain;
- channel/sequence map;
- frequency range where meaningful;
- capabilities such as playable audio, 2D display or overlay;
- bounded history/retention policy;
- subscriber count and health counters; and
- optional persistence/export providers.

Blocks publish data to multiple subscribers and provide bounded recent history
for newly attached displays.

### A3. Processes and ports

Processes expose named, typed input and output ports. Although Java commonly
uses one parent block, the C++ contract supports multiple named inputs for
modules such as overlays, triggered recording and localisation.

Connections validate:

- data-unit compatibility;
- sample/clock-domain compatibility;
- channel availability;
- required metadata/capabilities; and
- graph cycles.

Lifecycle operations include configure, prepare, start, stop, flush, reset,
disconnect and destroy.

### A4. Scheduling and pressure

- Preserve data ordering per block.
- Separate scientific processing subscriptions from presentation subscriptions.
- Allow explicit synchronous or queued delivery.
- Bound every queue and expose lag/drop counters.
- Never block acquisition indefinitely on a browser subscriber.
- Make discontinuities and dropped presentation frames visible.

### A acceptance

- One producer can feed multiple downstream processes and displays.
- A process can be safely reconnected to another compatible block.
- Invalid connections fail before processing starts.
- Ordering, channel maps, history and continuity have focused tests.
- Slow display subscribers cannot stall or corrupt detector processing.

## Work package B — module registry and graph configuration

Port the semantics of `PamControlledUnit`, `PamModuleInfo`, `PamDependency`,
`PamConfiguration` and standard source selection.

### B1. Module type registry

Each registered module type declares:

- type ID, display name, category and description;
- factory;
- input/output port descriptors;
- settings JSON schema and defaults;
- minimum/maximum instance count;
- dependencies;
- supported run modes;
- provided display types; and
- current implementation/parity status.

### B2. Module instances

Operators can add, name, duplicate, configure and remove instances. Multiple
FFTs, decimators, filters, detectors and displays are normal rather than special
cases.

### B3. Graph document

Create a versioned graph configuration containing:

- module instances;
- settings per instance;
- connections between output and input ports;
- acquisition and clock configuration;
- persistence/output policies; and
- graph revision.

Graph changes use optimistic revision checks and transactional validation.

### B4. Source catalogue

The service exposes compatible data blocks using type and capability metadata.
Every module and display source picker uses this catalogue rather than a
hard-coded list.

### B acceptance

- Add two FFT instances and bind them to different raw-audio blocks.
- Dependency errors explain the missing or incompatible source.
- Save, reload and reproduce stable module/block identities and connections.
- Removing a source gives downstream modules a clear disconnected status.

## Work package C — foundational signal-routing modules

### C1. Acquisition node

Wrap sound-card, URL/stream and file ingest as producers of raw-audio blocks.
Retain sample rate, channels, calibration, source identity, timing and
continuity.

### C2. Decimator node

Port PAMGuard decimator behaviour and defaults:

- selectable raw-audio source;
- selected channels;
- output sample rate;
- anti-alias filter;
- interpolation/resampling semantics; and
- output raw-audio block.

Add Java fixtures for maths and chunk-boundary state.

### C3. Filter node

Expose the already ported PAMGuard IIR/filter maths as a reusable processing
module with selectable raw source and output raw block. Add FIR behaviour only
when its Java path is explicitly ported and validated.

### C4. Amplifier node

Per-channel gain/attenuation with calibrated metadata propagation.

### C5. Patch Panel node

Port PAMGuard's channel reorder, duplication and mixing matrix semantics. Its
output is another discoverable raw-audio block.

### C6. FFT node

Move FFT ownership out of the monolithic session:

- selectable raw source;
- independent length, hop, window, channels and click removal;
- independent noise-reduction chain where selected;
- FFT output block with full metadata; and
- any number of FFT instances.

### C acceptance

The target vertical-slice graph runs continuously with one acquisition, one
decimator and two independent FFTs without duplicating acquisition.

## Work package D — detector migration

Wrap each existing algorithm as one or more registered graph modules. Migrate in
dependency order:

1. click detector and click classification outputs;
2. click-train detector/classifier;
3. Whistles & Moans consuming a selectable FFT block;
4. noise monitor and noise-band monitor;
5. LTSA;
6. Ishmael detectors;
7. matched-template classifier; and
8. localisation/bearing processes where separation is meaningful.

Each module must:

- select inputs from the source catalogue;
- publish typed output blocks;
- report status and counters;
- preserve existing Java parity fixtures;
- round-trip its complete configuration; and
- remain usable by multiple independent displays/subscribers.

The legacy monolithic session path is removed only after equivalent graph
integration and regression coverage exist. No old C++ session compatibility is
required during this active-development transition.

## Work package E — service graph and streaming API

Add service contracts for:

- module-type catalogue;
- module-instance and graph inspection;
- graph validation and transactional apply;
- data-block catalogue and metadata;
- per-block recent-history queries;
- per-block live subscriptions;
- graph/module/block health;
- workspace persistence; and
- playable raw-audio subscriptions.

Use the current HTTP stack where practical. Prefer binary chunked streams for
high-rate FFT/audio payloads and NDJSON for lower-rate event data. Do not add a
network dependency without a separate reviewed decision.

Subscription requests can select:

- data block;
- channels/sequences;
- frequency-bin range;
- presentation cadence/decimation;
- recent-history window; and
- delivery format.

Scientific module-to-module routing remains in-process and does not travel
through HTTP.

## Work package F — browser display-provider workspace

Port the semantics of `UserDisplayProvider`, `UserDisplayComponent`,
`DisplayNameManager`, saved display parameters and compatible source selection.

### F1. Display registry

Each display type declares:

- type ID and name;
- compatible data-block types/capabilities;
- settings schema/defaults;
- maximum instances, normally unlimited;
- renderer factory; and
- optional overlay support.

Initial display types:

- spectrogram;
- raw waveform;
- click detector;
- generic time plot;
- level meter;
- detector/event list;
- module status/console; and
- map/data-map later.

### F2. Workspace manager

- Add, remove, duplicate and rename displays.
- Dock, split, tab, resize and full-screen panels.
- Support multiple workspaces.
- Support browser pop-out windows for multi-monitor operation.
- Persist layout, source binding and display settings.
- Provide layout presets without constraining custom layouts.

### F3. Source picker

Every display chooses a compatible block from the live catalogue, then chooses
channels/sequences. Source loss is shown explicitly and can be rebound.

### F acceptance

- Create at least two spectrogram instances and one click display.
- Bind each to an independently selected block.
- Arrange, save, reload and reproduce the workspace.
- A new registered display type appears without editing the workspace shell.

## Work package G — spectrogram and overlay system

Each spectrogram instance has independent:

- FFT source block;
- channel/panel list;
- frequency limits;
- amplitude limits;
- colour map;
- time span and scroll/wrap mode;
- display cadence;
- waveform option;
- freeze/live state;
- overlay selections and styles; and
- optional synchronized-time group.

Port PAMGuard's overlay compatibility idea:

- blocks advertise axes/capabilities they can draw;
- the display lists only compatible overlays;
- overlays have per-display selectors and styling;
- click, whistle, annotation and other detections can coexist; and
- display selection/mark events can be consumed by other modules.

Port the useful semantics of the scroll system:

- live-follow and paused history;
- coupled or independent time axes;
- synchronized cursor/selection;
- bounded backfill from data-block history; and
- clear gap/discontinuity display.

## Work package H — selectable audio output

Port PAMGuard Sound Output semantics as a graph consumer, adapted for the web.

Settings:

- any playable raw-audio block as source;
- channel selection and mono/stereo mixing;
- gain and mute;
- high-pass filter;
- output rate/resampling;
- live latency target;
- output device where browser support allows it; and
- playback speed for later offline/viewer use.

Initial transport should use a binary streaming response feeding a browser
`AudioWorklet` ring buffer, avoiding a new server dependency. Expose buffer
level, underruns, dropped presentation samples and end-to-end latency.

The audio path must subscribe directly to the selected raw block, never to a
spectrogram preview or detector response.

### H acceptance

- Switch among acquisition, filtered and decimated sources without recreating
  the graph.
- Select channels and hear the expected mix.
- Detector processing remains unaffected by mute, browser disconnect or slow
  playback.
- Dropouts and latency are visible.

## Work package I — operator-support modules

After the composable core is proven, port in this order:

1. Level Meter.
2. Sound Recorder.
3. Clip Generator and detection-triggered recording.
4. Alarm and Event Counter.
5. Module status, warnings and remedial actions.
6. Data Map and richer synchronized navigation.
7. Effort Monitor.
8. Aural Listening and User Input.
9. Backup/storage-health integration.

These use the same graph and display contracts rather than adding fixed UI
pages.

## Work package J — validation and hardening

### Correctness

- Keep all existing CTest and Java-fixture checks green.
- Add Java fixtures for newly ported routing/signal modules.
- Prove chunk-boundary equivalence.
- Compare graph-wrapped detectors with the previous session path until the
  latter is removed.

### Graph and lifecycle

- Add/remove/reconnect while stopped.
- Transactional invalid-change rejection.
- Start/stop/flush/reset reproducibility.
- Source disappearance and recovery.
- Persist/reload round trips.
- Stable identities across reload.

### Concurrency and performance

- Multiple simultaneous displays and viewers.
- Slow/disconnected clients.
- Bounded memory under prolonged operation.
- Multiple FFT branches and audio playback under detector load.
- Multi-hour soak with visible queue/drop/latency counters.

### UI

- Layout persistence and migration.
- Multi-monitor pop-outs.
- Independent and coupled time axes.
- Keyboard-accessible workspace controls.
- Clear disconnected, delayed and dropped-data states.

## Complete-program definition of done

This plan is complete only when:

1. The runtime is a typed, inspectable module/data-block graph rather than a
   hard-wired detector session.
2. Operators can add multiple named instances of compatible modules.
3. Modules and displays select inputs from a shared live source catalogue.
4. One acquisition can branch through independent decimator/filter/FFT paths.
5. At least two independently configured spectrograms and a click display run
   together.
6. Each spectrogram independently selects source, channels, frequency range,
   scale, overlays and time behaviour.
7. Operators can arrange and persist custom multi-display workspaces.
8. Sound Output can listen to any compatible raw-audio block with channel,
   mix, gain, filter and resampling controls.
9. Existing detector maths is migrated without losing Java parity.
10. Graph changes, source loss, subscriber pressure and audio dropouts are
    explicit and tested.
11. The full graph and workspace survive save/reload with stable identities.
12. The complete vertical slice passes functional, concurrency, soak and
    browser validation.
13. Documentation and OpenAPI describe the finished module, graph, display and
    audio contracts honestly.

## Execution order

1. A — typed runtime data model.
2. B — registry, graph and source catalogue.
3. C1/C2/C6 — acquisition, decimator and multiple FFT vertical slice.
4. E — graph/block APIs and high-rate subscriptions.
5. F/G — display registry, workspace and multiple spectrograms.
6. H — selectable audio output.
7. D — migrate every existing detector into graph nodes.
8. C3/C4/C5 — reusable filter, amplifier and patch panel.
9. I — operator-support modules.
10. J — full hardening and completion audit.

Each stage receives focused tests and a numbered evidence document before the
next stage becomes authoritative.
