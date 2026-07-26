# PAMGuard-authoritative web workflow, module bundles, and configuration plan

Status: **Approved - implementation active (Phases 0-2 and 4-6 exit gates
passed; Phase 3 physical-audio acceptance and Phase 7 independent operator
acceptance remain; Phase 7 automated gates passed)**

Date: 2026-07-25

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Decision

The web application will have one operator workflow and one authoritative
configuration model.

The Data Model will be the configuration home. Modules, settings menus, data
blocks, displays, tabs, acquisition controls, and runtime status will all be
projections of the controlled units instantiated in that model.

A new or empty configuration will show the Data Model and global
start/stop/status controls. It will not show a Spectrogram, Click Detector,
Detections, Archive, Console, or audio-output surface unless the configuration
contains the unit or display instance that owns that surface.

The current fixed-session UI will no longer be part of the normal operator
application. Its service APIs may remain temporarily for headless regression,
Java/C++ comparison, import, and archive compatibility, but the production web
workflow will not call them.

If approved, this plan supersedes the operator-UI assumptions in `docs/240` and
`docs/246` that allowed the legacy pages to remain beside the Module Graph and
treated Workspace as a separate primary surface. It does not supersede their
typed data model, runtime graph, streaming, backpressure, or scientific-parity
work.

## Why the current UI is confusing

The browser currently combines three independent state models:

1. A fixed `AnalysisSession` workflow with a session sidebar, global detector
   dialogs, fixed Spectrogram/Click/Detections/Archive/Console tabs, and
   `/sessions/**` data.
2. A composable Module Graph with a separate draft, Validate, Apply, and
   runtime state.
3. A separate Workspace with its own display instances, audio monitor,
   persistence, and hard-coded provider catalogue.

This creates contradictions:

- detector and display tabs exist when their modules do not;
- the graph contains Spectrogram and Sound Output nodes that do not control the
  actual workspace spectrogram or audio monitor;
- detector settings exist both globally and on graph instances;
- the graph, workspace, graph layout, and legacy sessions persist separately;
- 32 low-level C++ runtime/display types are presented as though each were a PAMGuard
  operator module; and
- Validate -> Apply behaves like a second deployment system, whereas PAMGuard
  edits its one loaded configuration while stopped and uses Save for
  persistence.

The fix is not another navigation redesign. The conflicting state models must
be consolidated.

## Terminology

### Controlled-unit bundle

In this plan, a **module bundle** means the operator-visible equivalent of one
Java `PamControlledUnit`.

It can own several internal processes, data blocks, classifiers, localisers,
settings groups, actions, and displays. The Data Model shows one bundle node.
Its internal processes and blocks can be expanded for inspection, but they are
not separate palette modules unless Java PAMGuard registers them separately.

Examples:

- Java's FFT controlled unit owns the FFT process and spectral-noise process.
- Java's Click Detector owns detection, feature/classification work, its legacy
  train identification/localisation processes, legacy alert hooks, and
  displays. Java sound-alarm behaviour remains an agreed exclusion.
- Java's modern Click Train Detector is a separate controlled unit and owns its
  MHT algorithm, classification, and localisation.

### Configuration template

A **configuration template** is a convenience action that previews and adds
several independent controlled units and connections. A click-monitoring
template branches the raw stream correctly:

```text
Sound Acquisition -> FFT Engine -> Spectrogram
                  -> Click Detector -> Click display
                  -> Sound Output
```

A template must not merge those units into one node or one settings object.
Calling such a preset a module bundle would obscure PAMGuard's data model.

### Display instance

A display instance is presentation state contributed by a controlled unit or
created from an available display provider. It subscribes to a compatible data
block and is placed in a display tab. It is not scientific processing and must
not create an independent source-of-truth graph.

## Authoritative PAMGuard behaviour to reproduce

### Configuration lifecycle

- Open or create one configuration.
- Restore the ordered controlled-unit manifest first.
- Each controlled unit restores its own settings and registers its processes,
  data blocks, actions, and displays.
- Complete process and GUI setup only after all units exist.
- Restrict structural add/remove/connect/reorder changes to idle. Parameter
  settings follow an explicit per-setting policy: live-safe, process restart,
  or stop required. PAMGuard permits many settings dialogs while running, so a
  blanket settings lock would be a recorded web safety deviation rather than
  parity.
- Mark the configuration dirty after a successful change.
- Save or Save As persists the current model; Cancel in a settings dialog makes
  no change.
- Opening a different project performs a controlled stop and full model
  reinitialization; it is not merged into or hot-loaded over a running model.
- Start prepares all processes and aborts cleanly if any unit cannot prepare.
- Stop drains and flushes processing before returning to idle.

The server can retain transactional validation internally, but the operator
will not manage a separate draft deployment. Adding, removing, reconnecting,
or accepting a settings dialog will validate and update the active stopped
configuration as one atomic action.

### Module creation and dependencies

- The palette uses PAMGuard's groups:
  Maps and Mapping, Sound Processing, Detectors, Classifiers, Localisers,
  Displays, Utilities, Visual Methods, Sensors, and Sound Measurements.
- Only supported and available types appear.
- A type at its maximum instance count is disabled.
- Adding asks for a unique instance name, using PAMGuard-style defaults such as
  `FFT (Spectrogram) Engine`, `FFT (Spectrogram) Engine 2`, and so on.
- Empty, over-length, and same-type duplicate names are rejected.
- Missing required data providers trigger a clear offer to add the authoritative
  default provider recursively.
- The new unit is added to the active model. As in PAMGuard, configuration is
  then opened explicitly through the node cog or Settings menu; a visible
  `needs configuration` state replaces an unexpected automatic dialog.

### Data Model

- A permanent, non-closable Data Model tab is the default configuration view.
- Nodes represent controlled units, not every internal process.
- A node can expand to show its processes and data blocks.
- Connections represent actual parent-data-block subscriptions.
- The graph's incoming line and a module dialog's source selector are two views
  of the same relationship.
- Only compatible typed data blocks appear as sources.
- Channel and sequence choices are restricted to those published by the
  selected block.
- Structural add/remove/connect/reorder operations are disabled while running.
- Node positions and display layout are saved with the configuration.

The primary right-click menu will stay close to PAMGuard:

- Configure, using the unit's real settings actions/sections;
- Inspect processes and data blocks;
- Rename;
- Remove; and
- Help where an authoritative help target exists.

Web-only actions such as Duplicate, Enable/Disable, raw JSON, and processing
reset will not crowd the primary parity menu. If retained, they belong under an
explicit Advanced/Developer surface.

### Menus and displays

- Settings is generated only from loaded controlled units.
- Display is generated only from display actions/providers contributed by
  loaded units. A provider that Java permits before a source exists may create
  an explicitly unbound display; its source selector still lists only
  compatible data blocks.
- A controlled unit that owns a static display adds that display when the unit
  is added and removes it when the unit is removed.
- Adding User Display creates its empty display tab. A `+` shortcut performs
  that same controlled-unit action or adds a provider instance inside a
  selected User Display; it never creates orphan tab state.
- A Spectrogram is created only as a display instance from an available
  provider and is bound to an FFT data block.
- Multiple display tabs and multiple display instances are normal.
- Sound Output owns actual audio source selection and playback. There is no
  unrelated global audio monitor with different settings.

## Target web shell

```text
+---------------------------------------------------------------+
| File | Add Modules | Settings | Display | Help   [Start/Stop] |
+---------------------------------------------------------------+
| Data Model | <module/display-contributed tabs only> | [+]      |
+---------------------------------------------------------------+
|                                                               |
|  grouped palette        controlled-unit graph / selected tab   |
|                                                               |
+---------------------------------------------------------------+
| configuration dirty state | runtime state | warnings | clock  |
+---------------------------------------------------------------+
```

For an untouched empty configuration, only `Data Model` is present. The shell
does not imply that a detector, display, archive, or capture path exists. The
`+` control is a shortcut to add a User Display controlled unit (or a provider
inside the selected User Display), not a separate workspace-state constructor.

### Current surface migration

| Current surface | Target |
|---|---|
| Fixed Session sidebar and session Create/Delete | Remove from operator shell |
| Global Sound acquisition card and capture target selector | Selected Sound Acquisition unit's settings and runtime actions |
| Global Detection menu and fixed detector dialogs | Settings/actions generated by instantiated controlled units |
| Permanent Spectrogram tab | Dynamic Spectrogram display instance sourced from an FFT block |
| Permanent Click Detector tab | Click display contributed by an instantiated Click Detector |
| Permanent Detections tab | Compatible block-backed display providers only |
| Permanent Archive tab | Graph-native storage/query display when storage modules exist; legacy archive remains developer-only meanwhile |
| Permanent Console tab | Help -> Diagnostics/System status |
| Separate Workspace tab | Display-tab placement manager underneath the same project |
| Independent workspace audio monitor | Sound Output controlled unit |
| File Create/Flush/Delete/List Session | File New/Open/Import/Save/Save As configuration |
| Session-centric footer | Project dirty state, graph runtime, warnings, and selected acquisition status |
| Array settings under Detection | Global Array Manager/system configuration |
| Synthetic test dialog | Test Source controlled unit or Developer tool |

## Controlled-unit architecture

The existing typed C++ runtime remains useful. A controlled-unit layer will
compose it into the same operator units as PAMGuard.

### Controlled-unit descriptor

Each supported palette type needs metadata equivalent to `PamModuleInfo`:

- stable controlled-unit type ID;
- authoritative PAMGuard palette name and aliases, group, tooltip, and authority
  class; any corrected/modernized web label is recorded explicitly;
- implementation/parity status;
- minimum and maximum instance count;
- dependencies and default provider types;
- supported run modes;
- cardinality-aware input source requirements;
- versioned runtime expansion recipe;
- hidden internal process/node descriptors;
- settings actions, section order, validation, defaults, labels, units, and
  conditional visibility, plus live-safe/restart/stop-required change policy;
- stable public output roles;
- contributed display/provider types; and
- authoritative help/source references.

Add a versioned operator API such as `/v1/controlled-unit-types` and
`/v1/projects/**`; do not silently change the existing low-level
`/module-types` contract. Low-level runtime node descriptors can remain on a
developer endpoint or in the expanded inspector.

Existing `/module-graph` and `/workspaces` mutation routes must become internal,
read-only projections, or compatibility endpoints that cannot mutate the active
operator project. All normal writes pass through one project transaction so a
low-level client cannot bypass bundle ownership and desynchronise the graph,
settings, displays, or sources. OpenAPI and client contract tests change in the
same phase as each endpoint.

### Controlled-unit instance

Each instance contains:

- a stable opaque ID;
- controlled-unit type and unique editable instance name;
- authoritative settings object;
- source bindings by stable producer-instance/output-role identity;
- ordered hidden child processes/nodes;
- owned data blocks;
- contributed display instances/actions; and
- runtime state, warnings, and parity status.

Internal child-node IDs must be deterministic from the parent instance and
child role. A reconfiguration must not make displays lose their source merely
because an incidental data-block ID changed.

Every controlled-unit adapter provides reversible server-side operations:

- normalize and validate canonical bundle settings;
- expand the bundle and its binding roles into hidden runtime nodes, settings,
  and connections as one transaction; and
- project the runtime expansion back into a read-only inspection model.

The project never accepts an independently edited hidden graph as authoritative.
Unknown, malformed, or unsupported unit settings are rejected or quarantined
before any runtime, capture, device, URL, file, or path action occurs.

### One project document

Replace fragmented graph/workspace/local-storage persistence with one versioned
project document containing:

- project identity, metadata, and schema version;
- controlled-unit descriptor and expansion-recipe versions;
- ordered controlled-unit instances;
- source bindings/connections;
- authoritative settings for each unit and subcomponent;
- global Array Manager and other system settings;
- display tabs, display instances, sources, and layout;
- Data Model node positions, pan, and zoom;
- portable acquisition intent/settings; and
- canonical saved revision/content hash.

API connection details, API keys, and deployment secrets are not scientific
project settings.

The service can store multiple projects but loads one active project/runtime per
engine process. It holds an in-memory working project and a separate durable
saved baseline:

- each accepted edit increments `workingRevision`;
- every mutation and Save supplies the expected revision/ETag;
- dirty state is derived from working content versus the saved content hash,
  not persisted as a mutable boolean;
- Save uses atomic file replacement and advances the saved baseline only after
  success;
- failed validation, projection, runtime preparation, save, or project switch
  leaves the previous project, runtime, and file intact; and
- a second browser editing an old revision receives a visible conflict rather
  than last-write-wins.

Opening another project prompts for unsaved work, quiesces the active runtime,
closes streams/audio/capture, validates and migrates the target, then activates
it atomically. The backend may keep low-level graph, runtime, and display-layout
objects internally, but they are projections of the working project rather
than independently writable or saved models.

Project migrations define canonical normalization, stable output-role aliases,
unknown/unsupported-unit handling, and expansion-recipe upgrades. Host paths,
recorder destinations, device IDs, URLs containing credentials, and similar
deployment bindings have explicit portable-versus-host policy.

### Acquisition runtime binding

Sound Acquisition separates portable scientific configuration from
host-specific runtime binding:

- the project stores source intent, sampling, channel mapping, and calibration;
- the host binding stores an enumerated exact-match device or an allowed
  HTTP(S) URL and non-secret deployment details;
- PIDs, stale process records, and `running` state are never project settings;
- opening a project starts stopped and never automatically opens a device or
  URL;
- capture state is keyed by active project revision and Acquisition instance,
  so multiple acquisition units are supported;
- status polling reaps dead children and rejects stale bindings; and
- no arbitrary FFmpeg argument, executable, device string, path, or URL scheme
  crosses the existing trust boundary.

### Display ownership

Every display has exactly one owner and one persistence path:

1. A static module-owned display, such as the Click display, has the same
   lifetime as its controlled-unit instance and is removed with that unit.
2. A provider instance, such as a Spectrogram, is owned by one User Display.
   If its source disappears it remains visibly unbound until rebound or deleted.
3. A display-producing controlled unit such as Level Meter owns both its
   measurement process and its contributed visual surface.

There is no orphan global Workspace display. Creating, deleting, moving,
binding, or unbinding a display is one project transaction and survives
save/restart with its owner ID.

### Source binding contract

There is one authoritative binding collection per controlled-unit input role,
with declared cardinality (`0..1`, `1`, `0..N`, or `1..N`):

- dragging a connection updates that collection;
- choosing sources in the settings dialog updates the same collection and redraws
  the line;
- compatible choices come from typed data blocks, not hard-coded module names;
- source sample rate, channel map, frequency range, and capabilities drive
  downstream choices and read-only derived values; and
- removal of a source gives dependants a visible disconnected/invalid state.

### Runtime lifecycle and status

Lifecycle rules are enforced by the service, not only by disabled browser
controls:

- cold service/project activation opens in idle and never auto-starts a device;
- structural mutations are rejected while running;
- parameter mutations obey their descriptor's live/restart/stop policy;
- applying a stopped edit preserves the stopped state rather than implicitly
  starting the graph;
- Start normalizes and validates the working project, builds/prepares every
  process, starts only if all required units are ready, and rolls back on
  failure; and
- Stop first quiesces every Acquisition/capture, flushes buffered units through
  the graph in dependency order, drains scientific queues and finalizes trains,
  contours, clips, and recordings, then stops nodes in reverse dependency order.

A partial stop/flush failure is reported and cannot be presented as clean idle.
Project switch also closes data-block streams, browser audio, and capture
bindings before activation of the next project.

Health, readiness, and metrics will lead with active-project validity,
working/saved revision, runtime preparation/running state, acquisition/capture
health, queue pressure, and required-storage warnings. Legacy session counters
remain under explicitly named compatibility metrics and do not drive operator
readiness.

## Mapping the current 32 runtime/display types

### Operator-visible controlled units

| Current runtime type(s) | PAMGuard controlled unit shown to operator | PAMGuard group |
|---|---|---|
| `pamguard.acquisition` | Sound Acquisition | Sound Processing |
| `pamguard.amplifier` | Signal Amplifier | Sound Processing |
| `pamguard.patch-panel` | Patch Panel | Sound Processing |
| `pamguard.filter` | Filters (IIR and FIR) | Sound Processing |
| `pamguard.decimator` | Decimator | Sound Processing |
| `pamguard.fft` + internal `pamguard.spectrogram-noise` | FFT (Spectrogram) Engine | Sound Processing |
| `pamguard.click-detector` + internal click features/classifier/localiser/simple train roles | Click Detector | Detectors |
| `pamguard.mht-click-train` | Click Train Detector | Detectors |
| `pamguard.whistles-moans` + its internal noise chain | Whistle and Moan Detector | Detectors |
| `pamguard.ishmael-energy-sum` | Ishmael energy sum | Detectors |
| `pamguard.ishmael-sgram-corr` | Ishmael spectrogram correlation | Detectors |
| `pamguard.ishmael-match-filter` | Ishmael matched filtering | Detectors |
| `pamguard.matched-template-classifier` | Matched Template Click Classifier (Java alias: `Matched Template Click Classifer`) | Classifiers |
| `pamguard.fft-noise-monitor` | Noise Monitor | Sound Processing |
| `pamguard.noise-band-monitor` | Noise Band Monitor | Sound Processing |
| `pamguard.ltsa` | Long Term Spectral Average | Sound Processing |
| `pamguard.sound-recorder` | Sound recorder | Sound Processing |
| `pamguard.clip-generator` | Clip generator | Sound Processing |
| `pamguard.sound-output` | Sound Output | Sound Processing |
| `pamguard.level-meter` | Level Meter | Displays |
| `pamguard.alarm-event-counter` | Alarm, only after missing Alarm behaviour is implemented | Utilities |
| `pamguard.effort-monitor` | Scroll Effort | Utilities |
| `pamguard.aural-listening` | Aural Listening Form | Utilities |
| `pamguard.user-input` | User input | Utilities |

The separate Alarm controlled unit is not the owner of Click Detector's
internal legacy alert hook. The current web event counter remains a partial
separate Alarm implementation and cannot be labelled parity-complete.

### Hidden runtime implementation details

| Current runtime type | Operator treatment |
|---|---|
| `pamguard.spectrogram-noise` | Hidden child of FFT and Whistle/Moan ownership; standalone Java registration is disabled |
| `pamguard.click-features` | Hidden C++ engine adapter for derived click measurements; do not label it as a discrete Java `PamProcess` |
| `pamguard.click-localiser` | Hidden per-click delay/bearing engine adapter; array geometry moves to global Array Manager; it does not implement Java's tracked-click-train localiser |
| `pamguard.click-classifier` | Hidden basic/sweep classifier configured from Click Detector |
| `pamguard.click-train` | Hidden legacy simple train-identification process configured from Click Detector |

### Displays and extensions

| Current runtime type | Operator treatment |
|---|---|
| `pamguard.spectrogram-display` | Not a processing node; dynamic Spectrogram display-provider instance |
| `pamguard.click-display` | Not a processing node; the one static Click display owned by each Click Detector |
| `pamguard.storage-health` | Web extension under System/Diagnostics until it implements Java Backup Manager behaviour; do not claim Backup Manager parity |

The facade also adds this authoritative UI-only controlled unit:

| Facade type | PAMGuard controlled unit | PAMGuard group | Runtime treatment |
|---|---|---|---|
| `pamguard.user-display` | User Display | Displays | Owns an empty display area and the provider instances placed within it; no DSP node |

It is valid for a controlled unit to contribute UI and settings without adding
a DSP process.

The current Whistle/Moan `peaks` fields originate from Java's older Whistle
Detector, not the current Whistle and Moan Detector. They will be removed from
the normal Whistle/Moan form. If retained for regression, they will be labelled
as legacy compatibility or later exposed through a separate authoritative
legacy Whistle Detector unit.

## Configuration parity contract

A schema-generated generic form is not sufficient for a unit marked
PAMGuard-compatible.

Each visible controlled unit must have a dedicated settings contract containing:

- Java section/tab/menu order;
- field label and unit;
- Java default and legal range;
- enum order and stored value;
- help text where useful;
- source and channel/group semantics;
- conditional visibility and enablement;
- read-only derived values;
- nested list/table editing behaviour;
- OK/Cancel semantics;
- server-side validation and normalization; and
- an authority reference to the Java class/member or exported fixture.

Raw JSON may remain in Developer tools, but it is not an operator configuration
surface or parity evidence.

### Priority configuration surfaces

| Controlled unit | Authoritative web layout |
|---|---|
| Sound Acquisition | `Data Source Type`; source-specific device/file/stream pane; `Sampling`, including channel mapping; `Calibration`. Mode-dependent outer tabs are `DAQ Settings`, `Offline Files`, and `GPS Timing`. Device selection and capture start/stop belong here. |
| FFT Engine | `FFT`, `Click Removal`, `Spectral Noise Removal`. Source/channels, length, hop/50%, window, and derived resolution are followed by exact click-removal and noise-method controls. |
| Click Detector | Primary tabs `Source`, `Trigger`, `Click Length`, `Delays`, `Echoes`, `Noise`; module menu/actions for Detection Parameters, click marking/annotation integration, Digital pre filter, Digital trigger filter, Angle vetoes, Click Classification, Click Train Identification, and Click Train Localisation. |
| Click Train Detector | `Detector`, `Pre Classifier`, `Species Classifiers`, including the MHT kernel/chi-squared and classification panes. |
| Whistle and Moan Detector | `Detection`, `Noise and Thresholding`, then only operational display controls needed to interpret data. FFT source properties are derived rather than duplicated; legacy colour/aesthetic preferences remain excluded. |
| Ishmael detectors | Compatible grouped source, detector-specific pane, then shared Peak Picking: FFT for energy sum and spectrogram correlation; raw audio for matched filtering. |
| Noise Monitor | FFT source/channels and read-only source resolution; measurement bands; interval and measures. FFT length/hop are not duplicate editable settings. |
| Noise Band Monitor | Raw source/channels; output interval; measurement bands; filters; separate display options. |
| LTSA | FFT source/channels and measurement interval. |
| Filters | Raw source/channels; filter type; response/band/frequencies; order/ripple; response preview. |
| Decimator | Raw source/channels; read-only input rate; output rate; full anti-alias filter parameters; interpolation. |
| Signal Amplifier | Raw source and visible per-channel Gain (dB) plus Invert. |
| Patch Panel | Raw source and PAMGuard input/output routing matrix; any gain-matrix extension is clearly Advanced. |
| Sound Output | Playable source/channels; output device type/number; default or custom output rate and resampling; playback rate/speed with live input fixed to 1x; gain; high-pass filter; mute and actual playback status. Sidebar-only presentation preferences are recorded separately and may be omitted. Instance limits retain Java's run-mode rules rather than the current global cap of one. |
| Level Meter | Source, minimum level, units, and peak/RMS behaviour. |
| User Display / Spectrogram | User Display owns the tab and provider instances. Spectrogram settings cover FFT source, channels, frequency and amplitude ranges, time/scroll behaviour, operational axes, and compatible overlays, following `SpectrogramParameters`/`SpectrogramParamsDialog`. |
| Click display | Owned by Click Detector and subscribes to its click/localisation blocks. Preserve operational time, amplitude/bearing/ICI axes, channel groups, scrolling, selection, and continuous-history behaviour; legacy colours/layout are not parity targets. |

Sound Recorder, Clip Generator, Alarm, Effort Monitor, Aural Listening, and User
Input currently implement only subsets of their Java counterparts. They remain
labelled experimental until their operator forms and runtime behaviour satisfy
the same contract.

### Click Detector gaps that must close before a parity label

The existing child nodes are implementation foundations, not proof that the
Java Click Detector bundle is complete. Phase 3 must cover and test:

- grouped raw-data source and channel-group behaviour;
- Simple Echo enable/discard/interval settings and matching runtime behaviour;
- click marking/annotation integration, or an explicit unsupported state until
  the supporting display tool exists;
- the complete basic and sweep classifier editors and discard/check-all rules;
- full Click Train Identification settings, including run state, minimum and
  maximum ICI, maximum ICI change, angle and distance constraints, update
  ratio/gap, and Java's minimum-click default;
- per-click delays/TDOA and bearing from the hidden C++ localiser adapter;
- the separate tracked-click-train localisation settings and runtime that
  Java's `TrackedClickLocaliser` owns; the current C++ click-localiser does not
  implement this; and
- faithful output ownership where Java annotates a click rather than publishing
  a separate C++-style enrichment block.

### Known default corrections

Defaults will be generated or checked against the pinned Java classes, not
copied from the current browser schema:

- Sound Acquisition starts from Java's 48 kHz / 2-channel default, then requires
  an explicit usable device/file/stream selection.
- FFT starts with Java's channel-map semantics rather than silently selecting
  only channel 0.
- Click Detector starts with Java's grouped two-channel semantics where the
  selected source supports them; the implemented threshold/filter/window
  defaults already close to Java remain fixture-checked.
- input-derived FFT length, hop, rate, resolution, and channel availability are
  not repeated as divergent downstream settings.

The previously agreed exclusions remain exclusions: legacy Swing colours and
layout, RainbowClick legacy writing/import, sound alarms, and Java-specific
display aesthetics/preferences. Operational display settings such as source,
axes, ranges, time window, scrolling, channel group, and selection remain in
scope.

## Delivery plan

Work is split into reviewable gates. A phase is not complete merely because its
UI renders; its acceptance tests and Java mapping evidence must pass.

### Phase 0 - lock behaviour and remove immediate runtime hazards

1. Record the controlled-unit mapping above as a machine-readable parity
   manifest.
2. Add failing browser contracts for an empty project:
   Data Model only, no detector/display tabs, and no `/sessions` requests.
3. Add fixture checks for names, groups, multiplicity, dependencies, section
   order, and priority Java defaults.
4. After those characterization tests, perform a behaviour-preserving split of
   the monolithic `web-ui/index.html` into shell, legacy compatibility, Data
   Model, settings, displays, and diagnostics modules without adding a
   framework dependency.
5. Fix module-capture routing so the ingest child receives the raw Acquisition
   module ID rather than a second `module:` prefix.
6. Replace the browser-global capture target with per-Acquisition status,
   hydrate/reap it from the service, and coordinate stop/rebind with
   acquisition removal, project revision, or graph reconfiguration.
7. Make cold boot idle, prevent graph mutation from auto-starting the runtime,
   enforce server-side idle/change-policy guards, and implement quiesce,
   flush/drain, finalize, then stop.
8. Add the lifecycle, capture, and existing API contracts to OpenAPI and service
   tests before changing clients.

Exit gate: the target behaviour is executable as tests, and the acquisition
path needed by the first vertical slice publishes data reliably. Cold boot is
stopped, and Stop emits/finalizes buffered detector and recorder state before
clean idle.

### Phase 1 - introduce the controlled-unit and unified project model

1. Add versioned controlled-unit descriptors above the low-level runtime
   registry and expose them through a new versioned operator endpoint.
2. Implement canonical validation plus reversible, versioned expansion recipes
   and deterministic hidden child identities.
3. Implement unique naming, min/max rules, dependencies, and default-provider
   creation.
4. Make source bindings the common state behind graph lines and dialogs.
5. Define the one-active-project model, in-memory working revision, durable
   saved baseline, ETag conflict handling, migrations, and transactional
   New/Open/Save/Save As operations.
6. Make low-level graph/workspace writes internal or read-only so they cannot
   bypass the project authority.
7. Move graph layout and the explicitly owned display hierarchy out of
   browser-only local storage into that project.
8. Add atomic file replacement, failed-switch rollback, stream/audio/capture
   teardown, and portable-versus-host binding validation.
9. Update OpenAPI and all affected clients/tests as the project endpoints land.

Exit gate: one saved project round-trips controlled units, hidden runtime graph,
settings, connections, display ownership, and layout with stable identities.
Two clients editing the same old revision cannot overwrite one another, and no
low-level write can desynchronise the active project.

### Phase 2 - cut over to one PAMGuard-style shell

1. Make Data Model the only initial tab and default route.
2. Replace static menus with File, Add Modules, dynamic Settings, dynamic
   Display, Help, and global Start/Stop.
3. Remove the fixed session sidebar, footer, Detection menu, fixed tabs, and
   global detector dialogs from the operator DOM and initialization path.
4. Integrate display placement into dynamic tabs; remove Workspace as a
   separate product mode.
5. Move diagnostics, connection settings, and temporary compatibility tools to
   Help -> Diagnostics/Developer.
6. Make accepted edits immediate in the working project with OK/Cancel and
   derived dirty state; remove the operator-facing Validate -> Apply ceremony.
7. Apply server-provided live/restart/stop change policies and show actionable
   preparation errors on Start.
8. Drive the status bar and readiness from the active project/runtime rather
   than legacy session counts.

Exit gate: the normal browser has one state model and makes no `/sessions`
request. An empty project cannot show a phantom detector, display, or archive.

Phase 2 exit evidence: `docs/251-phase-2-pamguard-project-shell-evidence.md`.

### Phase 3 - complete the first operator-ready vertical slice

Implement exact controlled-unit ownership and dedicated settings for:

1. global Array Manager, so per-click delay/bearing configuration has one
   authoritative geometry source;
2. Sound Acquisition and its portable/host runtime binding;
3. FFT (Spectrogram) Engine, including hidden spectral-noise processing;
4. Click Detector, including the field-level gaps listed above, its hidden
   engine adapters, and tracked-click-train localisation;
5. Sound Output;
6. User Display / Spectrogram provider instances; and
7. Click Detector's contributed display.

Add a clearly labelled `Click monitoring configuration template` that previews
and creates the independent branches
`Acquisition -> FFT -> Spectrogram`,
`Acquisition -> Click Detector -> Click display`, and
`Acquisition -> Sound Output`. Acquisition remains visibly incomplete until
the operator chooses a real device/file/stream.

Migrate `ops/ingest_supervisor.py`, ingest source configuration, containers,
and Kubernetes examples from session IDs to active project and Acquisition
instance IDs. Browser capture and supervised ingest use the same instance-owned
contract and trust checks.

Exit gate: starting from blank, an operator can build the path, configure it,
create multiple spectrogram displays with independent sources/ranges, listen to
the chosen raw-audio block, view a continuous click display, save, restart, and
resume with the same identities and layout. No production ingest path creates a
parallel fixed `AnalysisSession` runtime.

Phase 3 working evidence (exit gate not yet claimed):
`docs/252-phase-3-first-operator-slice-evidence.md`.

### Phase 4 - port the remaining processing/detector configuration surfaces

Implement and validate in dependency order:

1. Signal Amplifier, Filters, Decimator, and Patch Panel;
2. Noise Monitor, Noise Band Monitor, and LTSA;
3. Whistle and Moan Detector;
4. Ishmael energy sum, spectrogram correlation, and matched filtering;
5. Click Train Detector and Matched Template Click Classifier; and
6. Level Meter.

Each unit is promoted from experimental only after its dialog layout, defaults,
source semantics, settings round-trip, runtime wiring, and scientific fixtures
pass.

For Matched Template Click Classifier, promotion also requires an explicit
boundary around Java's secondary integrations. The current click-level slice
does not yet accept `CTDataUnit` average-waveform input, write
`CTClassifierType.MATCHEDCLICK` train classifications, or provide Java's
click-code/name provider to downstream consumers. Browser template import is
CSV-only; Java's MAT importer is not yet available. These are remaining
features, not implicit parity.

One deliberate portable deviation is retained: project `clickType` values
`128..255` remain stable unsigned integers, while the dialog value `256` is
stored as portable `0`. Java writes the `100..256` spinner into a signed byte,
which makes `128..255` reopen as negative values and wraps `256` to zero.
Reproducing those reopen and downstream defects is not a portability goal.

Exit gate: every non-experimental palette item in this group has an
authority-backed dedicated settings surface and no independent duplicate
setting.

Phase 4 exit evidence: `docs/253-phase-4-processing-configuration-evidence.md`
(149/149 complete CTest baseline plus expanded real-Chromium project
save/restart coverage).

### Phase 5 - operator-support units and graph-native presentation/storage

1. Port Sound Recorder and Clip Generator configuration/trigger ownership.
2. Complete Alarm, Effort Monitor, Aural Listening, and User Input or keep them
   clearly experimental.
3. Make all display providers register through controlled units/data-block
   capabilities rather than a hard-coded browser catalogue.
4. Replace legacy Archive with storage-module-backed graph-native query/export
   displays before exposing Archive to operators.
5. Define graph-project offline execution for `/jobs`; until then, label the
   session/archive job path compatibility-only.
6. Keep system health as a small global drawer; keep module/data-map status as
   optional displays.

Exit gate: operator-support and archive surfaces appear only when the owning
unit exists and consume graph-native data blocks.

Phase 5 exit evidence:
`docs/254-phase-5-operator-support-and-storage-evidence.md`.

### Phase 6 - compatibility isolation and cleanup

1. Delete production-browser calls and dead DOM/JavaScript for the fixed
   `AnalysisSession` workflow.
2. Retain `/sessions/**` only in a named compatibility/oracle test harness until
   graph-native import/archive replaces its remaining uses.
3. Remove duplicate graph/workspace/local-storage persistence paths and
   compatibility writes to the active runtime.
4. Update README, OpenAPI examples, deployment examples, and operator
   documentation around the one-project workflow.

Exit gate: the production bundle contains no hidden second operator
application, while scientific comparison and compatibility tests still have a
deliberate headless path.

Phase 6 exit evidence:
`docs/255-phase-6-compatibility-isolation-evidence.md`.

### Phase 7 - parity, resilience, and operator acceptance

1. Run all C++ unit/fixture/service tests and Java-vs-C++ scientific comparisons.
2. Run browser workflow tests across empty, core click, multi-spectrogram,
   decimated branch, source loss, save/restore, and remove/re-add cases.
3. Soak acquisition, multiple display subscribers, Sound Output, and detectors;
   prove presentation backpressure cannot affect scientific processing.
4. Have an experienced PAMGuard operator perform task-based acceptance from a
   blank project without implementation guidance.
5. Record deviations explicitly in the parity ledger; do not silently label
   partial modules as PAMGuard-equivalent.

Automated validation evidence:
`docs/256-phase-7-automated-validation-evidence.md`.

Human acceptance record:
`docs/257-phase-7-operator-acceptance.md`.

## Acceptance tests that must remain permanently

1. A blank project shows only Data Model and global controls.
2. Normal browser startup makes no `/sessions` call.
3. Adding Click Detector without a compatible source offers to add Sound
   Acquisition and assigns unique names; its cog/Settings action opens Click
   configuration without an unsolicited automatic dialog.
4. One Click Detector node expands to its internal processes/data blocks;
   Click Features, Click Classifier, Click Localiser, and simple Click Train do
   not appear in the normal palette, and C++ adapters are not falsely labelled
   as discrete Java processes.
5. Settings contains exactly the actions contributed by current units.
6. Display contains only providers/actions contributed by current units; a
   Java-permitted provider without a source creates a clearly unbound instance
   whose picker lists only compatible blocks.
7. No Spectrogram exists until the operator adds User Display and creates one;
   two spectrograms can
   use different FFT sources, channels, ranges, and layouts.
8. A dialog source change redraws the graph connection, and a graph reconnect
   updates the dialog source.
9. Every display has exactly one owner. Removing Click Detector removes its
   static Click display; removing an FFT source leaves its User-Display-owned
   Spectrogram explicitly unbound; both behaviours survive save/restart.
10. Sound Output settings control the audio actually heard.
11. Cold restart is idle and never opens a device automatically. Structural
    graph editing is rejected by the server while running; parameter changes
    obey their declared live/restart/stop policy.
12. Cancel leaves settings untouched; OK validates/applies atomically and marks
    the working project dirty; Save atomically advances the durable baseline
    and clears derived dirty state.
13. Save/restart restores module order, names, settings, sources, graph layout,
    display tabs/instances/layout, and selected audio source.
14. Acquisition start -> browser reload -> status -> stop works, and removal or
    reconfiguration cannot leave an orphan capture process; dead status entries
    are reaped.
15. Stop quiesces acquisition, drains/flushes the graph, and finalizes pending
    click trains, whistle contours, clips, and recordings before clean idle.
16. A direct low-level graph/workspace write cannot desynchronise the active
    project.
17. Two browsers editing the same revision produce a visible conflict, never
    last-write-wins.
18. Failed save, invalid projection, failed runtime preparation, or failed
    project switch preserves the prior working project, runtime, and saved file.
19. Project switch closes block streams, browser audio, and capture bindings.
20. Supervised ingest reaches the selected project's Acquisition block and does
    not create a fixed session.
21. Readiness becomes false and actionable for an invalid project, preparation
    failure, dead required capture, or required-storage failure.
22. Java default/enum/order fixtures and scientific output comparisons pass.
23. Existing module-runtime tests remain core; session/archive tests are
    explicitly labelled compatibility tests, not primary UI evidence.

## Definition of done

This programme is complete when:

- there is one normal operator configuration and runtime model;
- Data Model is its sole configuration workflow;
- every visible palette node maps to an authoritative PAMGuard controlled unit
  or is clearly labelled as a web extension;
- internal processes are inspectable but not falsely exposed as modules;
- configuration dialogs reproduce the supported Java sections, defaults,
  units, source/channel behaviour, validation, and OK/Cancel semantics;
- menus, tabs, displays, playback, and storage surfaces are derived only from
  instantiated units and compatible data blocks;
- graph lines and source controls cannot diverge;
- the project saves and restores processing plus UI layout together;
- an empty project contains no phantom analysis UI;
- the production browser does not initialize or call the legacy session
  workflow; and
- all acceptance, scientific-parity, service, browser, and soak tests pass.

## Explicit non-goals

- A pixel-for-pixel port of Swing or JavaFX styling.
- Preserving old C++ browser sessions or their UI configuration.
- RainbowClick legacy file writing/import.
- Java sound alarms.
- Java-specific display colour/preferences parity.
- Claiming unsupported PAMGuard modules are implemented.
- Rewriting parity-tested DSP merely to fit the new controlled-unit layer.
- Introducing a third-party web framework during the workflow correction.
