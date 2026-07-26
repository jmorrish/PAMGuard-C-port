# Phase 0 execution evidence

Status: **Exit gate passed**

Date: 2026-07-25

Authority: `docs/247-pamguard-authoritative-web-workflow-plan.md`

## Result

Phase 0 is complete. The target PAMGuard shell is not yet implemented; its two
known legacy mismatches remain executable expected failures until the Phase 2
cutover. Phase 0 established the contracts, lifecycle behavior, capture
ownership, Java-authoritative manifest, safe browser-module boundaries, and
test evidence needed to introduce the project authority in Phase 1.

This result does not claim scientific parity beyond the fixture and behavior
evidence named below.

## Java-authoritative controlled-unit evidence

`platform/controlled-unit-parity-manifest.json` is pinned to PAMGuard Java
`2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

The schema-v2 manifest records 25 operator-visible controlled-unit contracts
and the web diagnostics extension owner. It locks exact Java names, groups,
classes, multiplicity, dependencies, settings classes and source paths. The
five first-slice configuration contracts cover Sound Acquisition, FFT,
Click Detector, User Display/Spectrogram, and Sound Output. All C++ parity
claims remain explicitly `not-claimed`.

`controlled_unit_manifest_check` resolves the pinned Java checkout directly
and checks every recorded authority tuple. It does not require the Java
checkout to have a clean working tree.

## Runtime lifecycle evidence

The runtime now:

- prepares a cold graph without starting it;
- rejects normal low-level graph mutation while running;
- permits an explicit temporary safe-stop mutation that leaves the runtime
  idle;
- quiesces sources, flushes and drains in dependency order, then stops in
  reverse dependency order; and
- does not report a clean idle state after a partial stop failure.

The integrated lifecycle fixture places pending click-train, clip, contour,
and recorder state in one runtime graph. `stop()` must publish every finalized
output and complete the exact 256-frame WAV before all nodes report stopped.
That proof passed both the focused CTest and five consecutive direct repeats.

A deterministic injected partial-stop-failure test remains a documented test
gap: the runtime has no injectable node factory or lifecycle failure seam.
Adding a production-only failure hook was not justified in Phase 0. This is a
testability gap, not a claim that partial-stop recovery is complete.

## Acquisition capture evidence

Capture is keyed by Acquisition instance. The service sends the raw module ID
to the ingest child, matches enumerated DirectShow devices exactly, accepts
only the supported HTTP/HTTPS URL forms, reaps dead children, and coordinates
capture with graph/runtime lifecycle serialization.

Module capture start and stop require the expected graph revision. Status
reports the current revision and capture kind, and stale module captures are
terminated if the graph revision, runtime, or Acquisition block no longer
matches.

The real-browser capture lifecycle test proves:

- two Acquisition instances can run distinct URL capture children;
- reload hydrates both states;
- each instance has its own target and stop action;
- stale revision writes return a conflict;
- child death is reaped and reflected in status;
- runtime Stop quiesces both children; and
- safe-stop graph reconfiguration quiesces the affected child.

The browser test child is isolated to the test target. The test verifies the
exact child executable and cleans its processes, browser profile, service, and
listeners.

Graph revision is the temporary Phase 0 concurrency boundary. Phase 1 replaces
it at the operator surface with the authoritative project revision and ETag.

## Browser extraction evidence

The browser assets are now separated into:

- `platform/dom.js`, `platform/http.js`, and `platform/lifecycle.js`;
- `legacy-compat.js`;
- `settings.js`;
- `data-model.js`;
- `displays.js`;
- `diagnostics.js`;
- `shell.js`; and
- the sole composition root, `main.js`.

The composition root injects the remaining cross-controller callbacks, mounts
each controller once, and disposes in reverse order. Disposal aborts HTTP
requests, stops streams/audio/timers, removes tracked listeners, and clears
graph and display state. Production behavior does not depend on the
`globalThis.__pamguardTest` browser-test adapter.

The service asset route confines requests to the canonical asset root,
rejects traversal and junction escapes, serves only regular allowlisted files
with explicit MIME types, and adds `nosniff`.

The behavior-preserving split intentionally retains the legacy shell until
Phase 2. The normal composition root must stop importing `legacy-compat.js`
when that cutover is complete.

## Verification run

The final Phase 0 verification on 2026-07-25 used a rebuilt Debug service and
passed:

- JavaScript syntax checks for every extracted asset;
- PowerShell parsing for all four browser contracts;
- OpenAPI YAML parsing;
- `git diff --check` with no whitespace errors;
- the visual graph browser smoke;
- the temporary workspace compatibility browser smoke;
- the capture lifecycle browser smoke; and
- the operator-shell characterization.

The operator-shell characterization observed exactly the two approved legacy
mismatches:

1. fixed tabs/panels still violate the blank-project Data Model-only target;
2. Workspace remains an independent global display authority.

Its permanent Phase 0 guardrails passed:

- startup made no `/sessions` request; and
- application disposal produced no new requests (`8 -> 8`), removed all
  tracked listeners (`148 -> 0`), deactivated the application, and cleared
  graph/display state.

The focused CTest gate passed 9 of 9:

1. `module_graph_and_typed_data_blocks`
2. `controlled_unit_parity_manifest`
3. `capture_service_command_trust_and_reaping`
4. `service_smoke_http`
5. `module_runtime_http_smoke`
6. `module_runtime_concurrency_soak`
7. `capture_service_http_smoke`
8. `capture_lifecycle_browser_smoke`
9. `web_assets_service_smoke`

After that focused gate, a clean rebuild of every configured target and the
complete CTest suite passed 106 of 106 in 8.94 seconds of test wall time. This
provides the Phase 1 starting baseline for the existing scientific fixtures,
runtime, service, archive, ingest, capture, and browser contracts.

## Phase boundary

Phase 0 enforces structural edits through the stopped-runtime boundary. The
temporary low-level `stopRuntime` operation is an explicit safe-stop bridge,
not the target operator editing model.

Typed per-setting change policies belong to the Phase 1 controlled-unit
descriptors. The initial safe policy is whole-settings-tree
`stop-required` until a module has a real transactional live setter and a
specific fixture-backed policy.

The Phase 2 shell contract remains deliberately unenforced by default until
the project authority is available and the fixed session/workspace surfaces
can be removed without introducing another state model.
