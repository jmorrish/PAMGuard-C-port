# Phase 1 controlled-unit project authority evidence

Status: **Backend exit gate passed; explicit checklist carryovers recorded**

Date: 2026-07-25

Authority: `docs/247-pamguard-authoritative-web-workflow-plan.md`

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Result

The Phase 1 backend exit gate is complete. The service now has one
authoritative active project in normal mode. Controlled units, settings,
source bindings, generated runtime children, graph layout, display tabs,
display ownership, and display settings are one versioned document with one
working revision and one strong ETag.

This phase establishes the authority and persistence boundary. It does not
claim that every broad Phase 1 checklist item is finished. The browser cutover
is Phase 2, the portable-project/host-Acquisition binding boundary is carried
into Phase 3, and injected durability/lifecycle failure coverage is carried
into Phase 6. It also does not expand the operator catalogue beyond the first
controlled-unit slice named below.

## Controlled-unit model

The versioned catalogue endpoint, `GET /v1/controlled-unit-types`, exposes
Java-pinned descriptors for:

- Sound Acquisition (`pamguard.acquisition`);
- FFT (Spectrogram) Engine (`pamguard.fft`);
- User Display (`pamguard.user-display`); and
- the Spectrogram display provider
  (`pamguard.spectrogram-display`).

Each descriptor records Java name, group, class and source references,
instance rules, typed public roles, default-provider relationships, ordered
settings sections, exact initial settings, closed settings schemas, change
policy, and a versioned runtime expansion recipe. Presentation providers are
owned by controlled units but are not emitted as scientific runtime nodes.

The project editor enforces PAMGuard-style Java item-name trimming and UTF-16
length, same-type uniqueness, multiplicity, allowed run modes, dependency
creation, provider compatibility, settings schemas, bindings and hierarchy
ownership. Adding an FFT with `add-defaults`, for example, atomically creates
and binds its default Sound Acquisition provider.

Generated runtime child IDs and data-block IDs are deterministic functions of
the persisted controlled-unit identity and recipe. A save/restart therefore
restores the same controlled units, hidden runtime graph, public blocks,
connections and display source mappings.

## One active project authority

The normal service exposes:

- `GET /v1/projects` and `GET /v1/projects/{id}`;
- `GET /v1/projects/active`;
- active-project inspection and compatible-source discovery;
- typed atomic project mutation batches;
- transactional New, Open, Save and Save As operations; and
- the controlled-unit catalogue.

Every state-changing request requires the current strong `If-Match` ETag.
Missing preconditions return `428`; a stale client returns `412` with the
current ETag. Mutation preparation retains the exact generated identities and
projected runtime candidate. The service preflights that candidate, performs a
stopped-runtime swap, and only then commits the authority after rechecking its
ETag. A failed authority commit swaps the previous stopped runtime back.

Opening or creating a project quiesces Acquisition captures, drains and stops
the old runtime, installs the prepared replacement, and remains idle. Cold
boot is an unsaved, stopped blank project unless an explicit saved project ID
is selected. Start rejects a project that is invalid, needs configuration, or
was not successfully prepared.

The former low-level graph and Workspace write endpoints return `405
project_authority_required` in normal mode. They cannot change the graph or
display hierarchy behind the active project.

## Persistence and conflict handling

Project files use strict UTF-8 and strict JSON parsing, canonical
serialization, bounded nesting/collections/settings, duplicate decoded-key
rejection, version checks, content hashes, and atomic publication within one
server-owned project root.

The persistence layer adds:

- a per-project cross-process lock;
- full normalized-envelope fingerprints, including metadata;
- pre-publication compare-and-swap revalidation and post-publication readback;
- no-follow/reparse rejection for project files;
- descriptor-anchored `openat`-family operations and directory `fsync` on
  POSIX;
- a retained and repeatedly revalidated project-root identity on Windows;
- flushed temporary files and atomic create/replace operations; and
- an explicit `ProjectDurabilityError` when publication may have occurred but
  durable directory synchronization cannot be confirmed.

The service maps a cooperative durable conflict to `409`. A pre-publication
save failure returns `500 project_save_failed`. A post-publication durability
uncertainty returns `500 project_save_uncertain` and instructs the caller to
reload/reconcile instead of blindly retrying.

The project directory and its parent remain an operational trust boundary on
Windows because public Win32 file APIs do not provide a general handle-relative
rename operation. Non-cooperating external writers that intentionally ignore
the lock can still race the final fingerprint-to-replace interval on portable
filesystems. The engine detects changes immediately before and after
publication, but exclusive ownership of the configured project root remains a
deployment requirement.

## Explicit legacy compatibility boundary

Old graph/session/workspace regression tests run only when
`PAMGUARD_LEGACY_MODEL_COMPAT=1` is set. That mode is mutually exclusive with
`PAMGUARD_ACTIVE_PROJECT_ID`, disables the active-project routes, and is
reported as `legacyCompatibility` in status. It is a test/import bridge, not a
second normal operator authority.

In normal mode, browser and production clients must use the active-project
API. Existing `/sessions` endpoints remain available for the separately
identified legacy scientific/reference surface, but they are not consulted by
the project runtime.

## Service and OpenAPI evidence

`project_authority_service_smoke` proves:

- catalogue identity, Java pin, CORS and ETag headers;
- blank stopped project behavior;
- required and stale preconditions;
- validate-only isolation;
- recursive default-provider creation and source binding;
- running-mutation rejection;
- controlled-unit-owned Spectrogram hierarchy and saved Data Model layout;
- exact Save As, list, resource read, process restart and stable identity
  round-trip;
- prepared-but-idle restored runtime; and
- rejection of low-level graph and Workspace writes.

`platform/openapi.yaml` documents all project routes, `If-Match`/`ETag`
semantics, error status mappings, schemas and recovery behavior. It parses as
OpenAPI 3.1 with 51 paths and 103 component schemas.

Deployment examples set `PAMGUARD_PROJECT_DIR` to a persistent,
engine-exclusive directory. The container image declares `/data` as the
durable volume and defaults the project store to `/data/projects`.

## Verification run

The final Windows Phase 1 verification on 2026-07-25 used a complete Debug
rebuild and passed:

- `project_store_atomic_persistence`;
- `project_authority_transactions`;
- `project_authority_json_contract`;
- `project_authority_service_smoke`; and
- the complete CTest baseline: **115 of 115**, zero failures, in 25.80 seconds
  of test wall time.

That complete run includes all existing scientific parity fixtures, runtime
graph tests, service/session compatibility tests, capture/browser lifecycle
tests, concurrency soak, archive, job, ingest and static-asset tests.

All changed PowerShell service/browser scripts parse successfully.
`git diff --check` reports no whitespace errors (only the repository's
existing line-ending conversion notices), and the updated OpenAPI YAML parses
successfully.

The persistence implementation was additionally compiled with
`-Wall -Wextra -Wpedantic -Werror` under Linux/WSL. Its descriptor-anchored
POSIX and real cross-process locking paths passed there; Windows `/W4 /WX`
also passed.

## Remaining phase boundary

The normal browser still presents the old fixed tabs and separate Workspace
surface. Phase 2 now replaces that DOM and initialization path with the sole
Data Model shell and dynamic controlled-unit-owned displays.

Schema version 1 is the first project format, so there is no older project
schema to migrate. The current migration policy is therefore an explicit
no-op: v1 is accepted and unsupported future/foreign versions are rejected
safely. A tested migration registry is required before a v2 format can be
published.

The project currently persists portable Acquisition intent and scientific
settings, while concrete device/URL selection and capture process identity
remain transient service state. The revision-keyed host-binding API,
credential exclusion contract, and rebind-on-open validation belong to the
Phase 3 production-ingest cutover and are not claimed here.

Capture teardown precedes a project runtime switch and is intentionally
irreversible. All ordinary fallible candidate generation and runtime
preparation happens before that point, but a rare partial failure while
stopping the old runtime cannot recreate an already terminated external
capture process. The runtime reports such a partial stop rather than claiming
clean idle; deterministic failure injection remains a lifecycle testability
gap from Phase 0.

The successful Save As/restart service contract covers the narrow Phase 1
round-trip gate. HTTP-level New, Open and ordinary Save lifecycle cases, live
stream/audio EOF on switches, capture reaping during switches, readiness
transitions for a needs-configuration project, and fault-injected
post-publication reconciliation remain named acceptance work for Phases 3 and
6. Core authority tests already cover New/Open/Save state transactions, but
that is not represented here as equivalent to those end-to-end cases.
