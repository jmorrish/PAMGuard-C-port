# Phase 0 browser contracts and web extraction boundaries

Status: **Phase 0 extraction and guardrails complete; PAMGuard shell target remains the Phase 2 cutover gate**

Date: 2026-07-25

Authority: `docs/247-pamguard-authoritative-web-workflow-plan.md`

## Scope and result

This checkpoint deliberately does not change `web-ui/index.html`. It locks
three approved target behaviours in an executable browser contract and adds
the narrowly confined HTTP asset prerequisite needed for later
behaviour-preserving extraction:

1. an empty project exposes exactly one active operator tab, `Data Model`;
2. normal operator startup makes no request to `/sessions` or `/sessions/**`;
   and
3. display state is owned by a controlled unit, not by an independent global
   Workspace product.

The contract is
`cpp-engine/scripts/pamguard-operator-shell-contract.ps1`. It starts the real
service with isolated empty graph/session/workspace paths, opens a clean
Chromium profile, injects a network recorder before application JavaScript,
and then evaluates the rendered DOM and JavaScript state.

The current browser is intentionally non-compliant. Default execution is
therefore an explicit expected-failure characterization, not a skipped test:

```powershell
.\cpp-engine\scripts\pamguard-operator-shell-contract.ps1 `
  -BuildDir .\cpp-engine\build
```

It succeeds only after printing the observed target failures. If all target
assertions unexpectedly begin to pass in expected-failure mode, the script
fails and instructs the developer to promote the gate.

Only the two documented current mismatches are allowed in that mode. The
already-passing no-`/sessions` startup contract remains a hard guardrail, so a
new legacy-session startup request fails even before the full shell cutover.

The cutover gate uses the same code and evidence:

```powershell
.\cpp-engine\scripts\pamguard-operator-shell-contract.ps1 `
  -BuildDir .\cpp-engine\build `
  -EnforceTarget
```

There is no mocked API and no source-text-only proxy for browser behaviour.

## Exact contract

| Contract | Browser/API assertion |
|---|---|
| Blank project is Data Model only | The union of `[role=tab]`, `[data-pamguard-tab-kind]`, and `.tabbar button[data-tab]` contains exactly one active tab named `Data Model`. No fixed Workspace, Spectrogram, Click Detector, Detections, Archive, or Console tab or corresponding legacy panel exists. |
| No legacy session startup | Requests initiated through `fetch`, XHR, EventSource, WebSocket, or `sendBeacon` are recorded before application code runs. No recorded URL has a path matching `^/sessions(?:/|$)`. |
| Display ownership is not global | An empty project has no display instance or display tab. There is no global Workspace surface, `/workspaces` startup call, old `workspaceDisplays`/`workspaceBlocks`/`workspaceAudioMonitor` registry, or display element without `data-owner-controlled-unit-id` (the shorter `data-owner-unit-id` is accepted during migration). |

New dynamic display roots must expose
`data-pamguard-display-instance-id="<stable display instance id>"` and
`data-owner-controlled-unit-id="<owning controlled unit id>"`. A display tab
must additionally expose `data-pamguard-tab-kind="display"` and the same owner
attribute. The Data Model tab should use
`data-pamguard-tab-kind="data-model"`. These are semantic test and
accessibility hooks, not styling selectors.

The current characterization proves:

- fixed tabs and panels violate the blank-project contract;
- startup currently makes no `/sessions` request; and
- the independent Workspace DOM, `/workspaces` request, and global registries
  violate controlled-unit ownership.

## CTest promotion

`cpp-engine/CMakeLists.txt` defines the option
`PAMGUARD_ENABLE_OPERATOR_SHELL_TARGET_CONTRACT`, default `OFF`. Turning it on
registers `pamguard_operator_shell_browser_contract` with `-EnforceTarget`:

```powershell
cmake -S .\cpp-engine -B .\cpp-engine\build `
  -DPAMGUARD_ENABLE_OPERATOR_SHELL_TARGET_CONTRACT=ON

& 'C:\Program Files\CMake\bin\ctest.exe' `
  --test-dir .\cpp-engine\build `
  -R pamguard_operator_shell_browser_contract `
  --output-on-failure
```

The shell-cutover change is not complete until it:

1. passes direct `-EnforceTarget` execution;
2. passes the opt-in CTest above;
3. changes the CMake option default to `ON`, or removes the option and registers
   the test unconditionally on supported Windows/Chromium workers; and
4. updates the older visual-graph/workspace smoke tests so they no longer
   assert the superseded dual-product UI.

That promotion rule prevents the expected-failure wrapper from becoming a
permanent skip.

## Why extraction needs an explicit boundary

`web-ui/index.html` is currently 9,140 lines containing CSS, operator shell,
legacy fixed-session configuration, graph editing, display classes,
persistence, audio, diagnostics, and bootstrap side effects. Moving arbitrary
line ranges would change behaviour because otherwise separate areas share
lexical globals and directly call one another.

The service previously served only `/` and `/index.html` from the path in
`PAMGUARD_WEB_UI_FILE`. It now has the narrowly rooted `/assets/...` route
required before external CSS or ES modules are introduced.

The asset route must:

- derive its root from an explicitly configured web asset directory or the
  validated parent of `PAMGUARD_WEB_UI_FILE`;
- canonicalize each requested path and reject traversal outside that root;
- serve only an allowlisted asset subtree and correct MIME types;
- never treat a URL as an arbitrary filesystem path; and
- retain `/` and `/index.html` behaviour while the extraction is verified.

The implemented root is `PAMGUARD_WEB_ASSET_DIR` when explicitly set;
otherwise it is an existing `assets` directory beside the canonical,
regular-file `PAMGUARD_WEB_UI_FILE`. Invalid explicit configuration fails
startup. Each request is URL-decoded by the HTTP library, structurally
validated, canonicalized, checked against the root again after symlink
resolution, required to be a regular file, and mapped through an explicit MIME
allowlist. The focused
`cpp-engine/scripts/web-assets-service-smoke.ps1` test covers CSS and nested
JavaScript loads, MIME and `nosniff`, both root modes, preserved HTML routes,
plain and encoded traversal, missing and unsupported files, and a junction
escape.

This route is infrastructure for static files, not permission to add a
framework or another UI state model.

## Current code anchors

Line numbers below identify the reviewed version. Symbol/comment anchors are
the stable extraction boundary if later edits move them.

| Current anchor | Responsibility | Important cross-boundary dependency |
|---|---|---|
| Lines 7-1337, `<style>` | All visual styles | Class names are shared by every current surface. Extract unchanged before renaming or deleting selectors. |
| Lines 1340-1832, `.app` | Menus, fixed sidebar, graph/workspace/fixed tabs, status bar | Contains both the future shell and legacy DOM. Do not move it wholesale into the target shell. |
| Lines 1834-2760, settings dialogs | Graph settings/inspector plus fixed global Detection/Test dialogs | `graphSettingsDialog` and `graphInspectorDialog` belong to Data Model/settings; `dlg*` fixed detector forms belong to legacy compatibility. |
| `const $` through the comment `composable processing graph` (lines 2764-4784) | Shared helpers plus the entire fixed-session config, archive, capture, waterfall, and Click display flow | `api()`/`requestJson()` are shared; legacy capture calls `refreshWorkspaceSources()`. Health/ingest actions are mixed beside `/sessions` actions. |
| Comment `composable processing graph` through `loadGraphEditor()` (lines 4785-6624) | Module catalogue, graph draft/layout/rendering, guided settings, runtime controls, graph operator events | `updateCaptureTargets()` writes the legacy capture selector; Apply calls `refreshWorkspaceSources()`; workspace status displays call `graphFocusModule()`. |
| Comment `composable operator workspace` through `workspaceAudioMonitor.populateDevices()` (lines 6626-9022) | Independent Workspace store, display classes, streams, audio monitor, persistence, and initialization | Reads shared API/format/canvas helpers and calls `graphFocusModule()`. This whole block is legacy global ownership until adapted to the project. |
| Comment `UI chrome` through initial `activateTab()` (lines 9024-9136) | Menu/tab wiring, health timer, legacy status timer, default route | Reads graph, workspace, fixed Click display, capture, session, and metric globals. It currently defaults to Spectrogram. |

## Safe target files and interfaces

The first split is behaviour-preserving. Each module has an explicit
constructor or `mount()` function and a `dispose()` function; importing a file
must not start network requests, timers, streams, audio, or DOM mutation.

| Module | Extracted ownership | Allowed interface |
|---|---|---|
| `assets/platform/http.js` | API base/auth resolution, JSON/text requests, typed HTTP errors | `createHttpClient(connectionSettings)` returning `get/post/put/delete/stream`; it owns no module, display, or tab state. |
| `assets/platform/dom.js` | Element lookup, formatting, common canvas sizing/colour helpers | Pure helpers only. Missing required elements fail at `mount()`, not during module import. |
| `assets/data-model.js` | Graph catalogue/draft/layout, palette, nodes, typed wires, selection, undo/redo, runtime controls | `mountDataModel({http, elements, settingsController, onApplied})`; exports `load`, `focusControlledUnit`, `dispose`, and read-only test state. No direct Workspace or fixed capture DOM access. |
| `assets/settings.js` | `GRAPH_SETTING_SECTIONS`, labels/help, schema controls, guided form read/render, OK/Cancel shell | `createSettingsController({http, dialogHost})`; receives the selected unit descriptor/settings and returns a validated draft. It does not own graph connections or mutate runtime globals. |
| `assets/displays.js` | Reusable display rendering and stream subscriptions derived from the current `Workspace*` classes | `createDisplayController({http, projectStore, tabHost})`; every `add` requires display ID and controlled-unit owner ID. No local-storage/workspace authority and no `/workspaces` call. |
| `assets/diagnostics.js` | Health/system/ingest inspection and diagnostic log presentation | `mountDiagnostics({http, host})`; no `/sessions` calls during normal startup. Legacy session tools are not imported here. |
| `assets/legacy-compat.js` | Fixed `/sessions` config/archive/capture/test flow and, during extraction only, the old global Workspace orchestration | `mountLegacyCompatibility(...)` is called only by a deliberate developer/compatibility entry point. Merely loading the normal operator application must not import or mount it after cutover. |
| `assets/shell.js` | PAMGuard menu composition, Data Model tab host, dynamic contributed tabs, Start/Stop/status, routing | `mountShell({projectController, dataModel, settings, displays, diagnostics})`; it renders projections but is not an independent settings/display store. |
| `assets/main.js` | Composition root and initialization order | Creates dependencies, mounts exactly once, and registers teardown. It contains no detector maths, display implementation, or legacy session actions. |

`index.html` should remain the static semantic skeleton during the JavaScript
split. Moving HTML into fetched fragments would add another initialization and
failure path and is not needed to establish module boundaries.

## Required adapters before moving blocks

The following direct calls are the unsafe seams. Replace them with injected
callbacks in the same behaviour-preserving extraction change:

1. Replace Data Model's `updateCaptureTargets()` DOM write with an
   `onAcquisitionListChanged` callback owned by the temporary legacy adapter.
2. Replace `applyGraphEditor()`'s direct `refreshWorkspaceSources(false)` call
   with `onApplied(graphSnapshot)`.
3. Inject `focusControlledUnit(moduleId)` into the temporary Workspace adapter
   instead of reading `graphFocusModule` lexically.
4. Move the health and ingest-status click handlers away from the neighbouring
   `/sessions` handlers into Diagnostics.
5. Move API key/base lookup out of `#dlgConnect` DOM reads and into the shared
   HTTP client's connection-settings provider.
6. Give every controller its own abort controllers, timers, animation-frame
   handles, event-listener removers, stream readers, pop-out windows, and audio
   contexts, all closed by `dispose()`.
7. Replace direct cross-module globals used by browser tests with a single
   read-only `globalThis.__pamguardTest` adapter. Production behaviour must not
   depend on that adapter.

## Extraction order and invariant checks

Use small changes in this order:

1. add the safe asset route and a MIME/traversal/static-load service test;
2. move CSS byte-for-byte and confirm the two existing browser smoke tests;
3. extract pure DOM/format helpers and the HTTP client;
4. extract Data Model and settings using the callbacks above;
5. quarantine the complete current Workspace block in legacy compatibility,
   then carve reusable display renderers behind owner-required interfaces;
6. extract Diagnostics;
7. extract Shell and make `main.js` the sole composition/startup location; and
8. only then perform the Phase 2 shell cutover and enable the hard target
   contract.

For each behaviour-preserving extraction commit:

- the default guardrail must report the same two known target mismatches and
  continue to show no `/sessions` startup request;
- `visual-graph-browser-smoke.ps1` must retain graph behaviour;
- `workspace-browser-smoke.ps1` must retain the temporary compatibility
  behaviour;
- every module mounts once and disposes cleanly on reload/project switch; and
- no endpoint, persistence key, default value, source selection, stream
  subscription, or runtime action changes in the extraction commit.

The later cutover intentionally changes those legacy expectations. At that
point the old workspace smoke becomes a compatibility-only test, the new
operator contract becomes mandatory, and the normal composition root must not
import `legacy-compat.js`.
