# Phase 5 operator-support and graph-native presentation/storage evidence

Status: **Phase 5 exit gate passed through implemented or explicit-unavailable
paths**

Date: 2026-07-25

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

Plan: `docs/247-pamguard-authoritative-web-workflow-plan.md`

## Exit decision

Operator-support surfaces are owned by controlled units and consume their
typed graph outputs. Partial Utilities, archive, and offline-job paths are not
presented as completed PAMGuard modules.

Phase 5 does not claim that every Java support module is ported. It uses the
plan's required safe boundary: implement a source-backed contract or keep the
type visibly experimental/unavailable and out of the normal palette.

## Sound Recorder

`pamguard.sound-recorder` owns one raw-audio binding, canonical Java-relative
portable settings, one recording-event output, and a dedicated browser editor.
Host output storage is injected only while preparing the active project:

```text
PAMGUARD_RECORDING_ROOT / projectId / soundRecorderUnitId
```

The portable project never stores that host path. Runtime starts Off and the
stable unit-ID service supports status plus Off/Continuous transport.
Cycle/scheduler restoration remains explicitly unsupported rather than being
silently approximated.

Runtime evidence covers:

- safe idle and explicit transport;
- selected-channel PCM conversion and valid WAV finalisation;
- Java file length, rounding, date-subfolder, initials, bit-depth, and trigger
  settings;
- exclusive collision-safe file creation without truncating an existing file;
- Stop/Off finalisation and completed-file counts;
- root-relative recording events, with no absolute host path or hidden runtime
  identity on the stable recorder surface; and
- settings read-only while running while Off/Continuous remain available.

The dedicated real-browser and service checks are
`project_sound_recorder_settings_browser_contract` and
`project_sound_recorder_http_smoke`.

## Clip Generator

`pamguard.clip-generator` owns raw-audio history plus zero or more explicitly
bound trigger policies. A fresh Java configuration has no registered trigger
policy and therefore remains safely idle.

The canonical editor and runtime cover:

- Java storage enum/default and dated-subfolder setting;
- source-specific enablement, pre/post windows, channel-selection modes,
  prefix, budget period, initial budget, and budget amount;
- strict routing by the exact bound runtime trigger block;
- sparse physical-channel bitmaps for detection channels, all raw channels,
  and first detection channel;
- Java sample truncation boundaries and independent Java-style trigger-budget
  state with deterministic random injection for tests;
- non-FIFO readiness when post-trigger audio arrives;
- complete-only clip publication; gaps, discarded audio, invalid channels,
  and incomplete Stop state do not create misleading clips; and
- trigger provenance, clip/trigger times, selected bitmap, prefix, and PCM
  content on the typed clip output.

Binary graph publication is implemented. Deployment-owned WAV and Both storage
are rejected at runtime with an explicit error until a host storage adapter is
implemented. Annotation storage is not exposed because the pinned Java dialog
path is dormant. The unit remains labelled experimental.

Focused checks are `clip_generator_portable_settings`,
`clip_generator_controlled_unit`, `clip_generator_runtime_parity`, and
`project_clip_generator_settings_browser_contract`.

## Utilities

The authoritative Utilities catalogue order is:

1. User input
2. Aural Listening Form
3. Alarm
4. Scroll Effort

All four currently have source-backed descriptors and dedicated data
contracts, but remain unavailable/experimental and hidden from the normal
palette. Project mutation rejects them with
`controlled_unit_type_unavailable`.

This prevents the low-level generic operator logger and click-count Alarm
foundation from masquerading as the substantially richer Java modules. The
catalogue, parity manifest, project projection, JSON order/status, data
contracts, and mutation rejection have focused CTest coverage.

## Displays, Archive, jobs, and health

- Display actions come from each loaded controlled-unit recipe's contributed
  provider IDs and the server's display-provider catalogue. Compatible
  sources come from typed public data-block capabilities. Browser renderer
  dispatch supplies implementation code but is not an independent provider
  catalogue or persistence authority.
- The normal shell has no fixed Archive tab. No graph-native storage/query
  controlled unit currently exists, so no Archive action or display is
  exposed. Session archive/query remains only in explicit compatibility mode.
  This satisfies the required safety boundary without falsely presenting the
  old session archive as project storage.
- `/jobs` still constructs the fixed compatibility `AnalysisSession` runtime.
  It is deprecated, compatibility-labelled, and unavailable in normal project
  mode until graph-project offline execution and an owning storage/query unit
  are implemented.
- Global health remains a small Help -> Diagnostics / Developer view over
  health, readiness, runtime, active-project, and inspection state. It does
  not create a phantom operator display. Module/data-map status can be added
  later only through owned providers.

## Permanent claim boundary

- Sound Recorder and Clip Generator are useful project-controlled
  experimental slices, not complete PAMGuard module-equivalence claims.
- Alarm, Scroll Effort, Aural Listening, and User Input are unavailable.
- There is no normal Archive surface.
- Compatibility session archive/jobs do not count as project-native storage
  or offline execution.
- Legacy Swing presentation, RainbowClick writing/import, Java sound alarms,
  and Java-specific display aesthetics remain agreed exclusions.
