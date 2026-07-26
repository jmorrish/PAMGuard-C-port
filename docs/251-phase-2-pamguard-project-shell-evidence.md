# Phase 2 PAMGuard-authoritative project shell evidence

Status: **Exit gate passed**

Date: 2026-07-25

Authority: `docs/247-pamguard-authoritative-web-workflow-plan.md`

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Result

The Phase 2 operator-shell exit gate is complete. The normal browser now has
one project authority and one PAMGuard-style configuration workflow. A cold
blank project opens on the permanent Data Model tab and does not create or
imply a detector, Spectrogram, archive, Workspace, or legacy session.

The old fixed-session browser remains at `web-ui/legacy-compat.html` solely for
named compatibility and scientific-regression use. It is not imported,
initialized, or requested by the normal `web-ui/index.html` application.

This phase proves the shell cutover. It does not claim the live Acquisition,
Click Detector, Sound Output, or multi-Spectrogram operator slice; those are
the explicit Phase 3 target.

## One operator workflow

The normal shell provides:

- File, Add Modules, dynamic Settings, dynamic Display, and Help menus;
- global Start and Stop controls;
- a permanent, non-closable Data Model tab;
- only those additional tabs owned by controlled units in the active project;
- a grouped controlled-unit palette and connected Data Model canvas; and
- project dirty state, runtime/readiness state, actionable warnings, and the
  current time in the status bar.

There is no normal Session sidebar, Detection menu, fixed Spectrogram, Click
Detector, Detections, Archive, Console, or Workspace tab. Engine connection
and OpenAPI/diagnostic actions are under Help instead of being presented as
scientific project state.

Right-clicking a controlled-unit node exposes the parity-first actions
Configure, Inspect, Rename, Remove, and Help where applicable. Adding,
removing, renaming, reconnecting, configuring, moving nodes, changing viewport
layout, and managing displays all write through the active-project mutation
API. There is no browser-side Validate -> Apply deployment model.

## Project lifecycle and conflict handling

New, Open, Save, and Save As use the one active project and its strong ETag.
The browser serializes project edits, layout changes, runtime transitions, and
project switching so stale asynchronous reads cannot replace a newer client or
project snapshot.

Every authoritative edit supplies the current `If-Match`. A real stale-browser
HTTP `412` is displayed as a visible conflict; it is not silently retried or
overwritten. Failed derived inspection refreshes no longer turn an already
accepted project mutation into a false failure.

Start is disabled when the project is not prepared, and preparation errors are
shown as project readiness rather than legacy session capacity. Structural
editing is disabled while running. Stop returns the shell to an editable state
and persists any display-tab selection made locally while the runtime was
active.

Opening a project or reconnecting creates a new client generation. Poll,
runtime, dialog, and drag callbacks from an older project/client generation are
discarded. Disconnection clears stale nodes, displays, tabs, and active-project
state. Shell disposal cancels active interactions and is terminal; a disposed
shell cannot be remounted accidentally.

## Controlled-unit graph and settings

The browser renders server-owned controlled-unit instances, public data blocks,
source bindings, and deterministic runtime inspection. Connections and dialog
source selectors are projections of the same project bindings.

Node and viewport layout are persisted immediately through the project
authority while stopped. Server-generated default positions use sufficient
horizontal and vertical spacing for the current node geometry, preventing
newly added controlled units from overlapping.

Settings forms are generated from the controlled-unit descriptors and use
server-side validation. Cancel and dialog-close make no project mutation;
accepted changes are atomic and derive the dirty state from the saved project
baseline. Cancel controls bypass unrelated required-field validation.

## Display ownership

Display tabs, display instances, sources, settings, placement, and selected tab
are read from and written to the project hierarchy:

- adding User Display creates its owned tab;
- adding a Spectrogram provider creates an instance inside that User Display;
- the Spectrogram source picker lists compatible FFT data blocks;
- display selection remains usable while running and is reconciled after Stop;
- removing User Display removes its tab and owned Spectrogram; and
- Save As plus service restart restores the same controlled-unit and display
  identities.

The Spectrogram surface has a real, non-collapsed display region and responsive
grid placement, but live stream rendering and multiple independent operational
Spectrogram configurations remain Phase 3 work.

## Executable browser evidence

`pamguard_operator_shell_browser_contract` is an unconditional target gate. In
an installed Chromium browser it proves that a blank project exposes exactly
Data Model, that normal startup makes no `/sessions` or `/workspaces` request,
that there is no independent Workspace/display registry, and that a disposed
shell cannot be remounted.

`project_shell_browser_smoke` starts the real engine service with an isolated
project store and drives the visible browser application. It proves:

- the blank-shell geometry, usable grouped palette, and non-overlapping
  four-row application layout;
- visible controlled-unit creation and configuration;
- Sound Acquisition, FFT, User Display, and Spectrogram ownership;
- compatible source and settings round-trip;
- project layout and display persistence;
- Save As list labels and stable identity after a real service restart;
- a visible real-HTTP ETag conflict;
- Help/OpenAPI and connection-menu placement;
- non-collapsed display rendering; and
- owned-tab/display removal.

The client contracts separately prove strong-ETag parsing, derived-read race
suppression, accepted-mutation behavior, stale-client rejection, and secure
opaque browser identifier generation.

## Verification run

The final Windows Phase 2 verification on 2026-07-25 used a complete Debug
build and passed:

- JavaScript syntax checks for the project shell, project client, and
  identifier helper;
- parser checks for both changed PowerShell browser contracts;
- the four focused shell/client contracts: **4 of 4**, zero failures;
- the complete CTest baseline: **119 of 119**, zero failures, in 34.64 seconds
  of test wall time; and
- all existing scientific parity, runtime, project-authority, service,
  capture, archive, ingest, soak, and legacy-compatibility tests included in
  that baseline.

`git diff --check` reports no whitespace errors (only the repository's
line-ending conversion notices). `platform/openapi.yaml` parses as OpenAPI 3.1
with 51 paths and 103 component schemas.

## Remaining phase boundary

The first catalogue slice intentionally contains only Sound Acquisition, FFT,
User Display, and the Spectrogram provider. Their shell ownership and project
round-trip are real, but Phase 3 must now connect them to operator-ready host
Acquisition binding, streaming Spectrogram data, Sound Output playback, the
full Click Detector controlled-unit bundle and continuous Click display, global
Array Manager geometry, the click-monitoring template, and project-native
supervised ingest.

Legacy Swing colours/layout, RainbowClick file writing, sound alarms, and
Java-specific display aesthetics/preferences remain agreed non-goals.
