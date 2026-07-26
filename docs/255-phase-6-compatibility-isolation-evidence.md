# Phase 6 compatibility isolation and cleanup evidence

Status: **Phase 6 exit gate passed**

Date: 2026-07-25

Plan: `docs/247-pamguard-authoritative-web-workflow-plan.md`

## Normal operator boundary

The normal `/` application contains only the project-native Data Model shell,
controlled-unit settings, owned displays, and project clients. Its permanent
browser contracts reject:

- fixed Session DOM or `AnalysisSession` code;
- `/sessions`, `/jobs`, or legacy archive requests;
- `/workspaces` requests or browser-local Workspace persistence;
- low-level `/module-graph`, runtime-node Acquisition ingress, or
  runtime-node operator-event publication;
- legacy capture status/start/stop identities; and
- fixed Spectrogram, Click, Detections, Archive, Console, or Workspace tabs.

`project_shell_browser_smoke` exercises the real service and Chromium workflow
without any of those requests. Project PCM injection in that test now uses the
stable Acquisition controlled-unit route.

## Service isolation

Normal project-authority mode returns `404` with
`legacy_compatibility_required` for:

- `/sessions` and every `/sessions/**` result/archive/audio/PCM route;
- `/jobs` and `/jobs/**`;
- `/workspaces` reads;
- `/capture/status`, `/capture/start`, and `/capture/stop`; and
- `/module-runtime/acquisitions/{runtimeId}/pcm-f32le`; and
- `/module-runtime/operator-inputs/{runtimeId}/events`.

Those routes work only when the engine is deliberately started with
`PAMGUARD_LEGACY_MODEL_COMPAT=1`. Successful compatibility responses carry a
deprecation header. Persisted fixed-session configurations, persisted
Workspace state, and offline-job worker threads also load/start only in that
mode; setting an active project ID and compatibility mode together is rejected.

The global capture-device enumerator remains available because the normal
Sound Acquisition Host input dialog uses it. Project runtime control and typed
data-block read/audio streams also remain available because they are
projections/operations of the active project, not alternate configuration
authorities.

Normal `GET /module-graph` remains a read-only generated inspection projection.
Direct graph PUT and Workspace PUT/DELETE return project-authority errors and
the service smoke proves that the active project snapshot, revision, and
runtime state remain unchanged after each attempted write.

Health/readiness capability flags report the effective mode rather than
advertising disabled session/archive/job features in project mode.

## Compatibility harness and production artifact

`web-ui/legacy-compat.html` is visibly and structurally labelled as an isolated,
deprecated oracle/regression harness. It is not linked or loaded by the normal
application.

The production Dockerfile copies an explicit project-shell asset allowlist
instead of the complete `web-ui` directory. It therefore excludes:

- `legacy-compat.html`;
- the old `main.js`, `legacy-compat.js`, `data-model.js`, `displays.js`,
  `settings.js`, and `shell.js` application;
- legacy Workspace/session UI state; and
- session-oriented example configuration assets.

`production_compatibility_isolation_browser_contract` checks the production
HTML/script graph, service guard coverage, Docker allowlist, absence of legacy
references, and every local Dockerfile `COPY` source. The contract verifies
that each copied source exists and is not excluded by `.dockerignore`. Docker
CLI was not installed on the validation host, so the image itself was not
built; packaging isolation was validated statically from the
Dockerfile/context contract.

## API and test classification

OpenAPI marks 30 retained session/archive/job/capture/low-level graph and
ingress operations as deprecated Compatibility operations. Stateful
session/job/capture, Workspace-read, and generated-ID ingress operations
document their explicit compatibility-mode requirement; the remaining
low-level graph projections or disabled mutation routes are likewise not
presented as normal operator APIs. Project and controlled-unit routes remain
the normal operator contract.

CTest applies compatibility/legacy-session labels to the retained
session-manager, service/load/job/archive, legacy capture/browser, and
low-level runtime tests. Fifteen compatibility-labelled tests passed together.
Core module-runtime/scientific tests remain core rather than being mislabeled
as legacy simply because the same maths also supports oracle work.

## Dialog lifecycle correction

The expanded browser workflow exposed a real native `<dialog>` reuse race.
`dialog.close()` removes `open` synchronously but queues the old `close` event.
A fast reopen could mount a new editor before that event arrived; the stale
handler then disposed the new Sound Recorder editor and made its Off transport
click disappear.

The project shell now keeps a close barrier. A new form waits for the prior
dialog's close lifecycle to settle before it mounts listeners/content and then
rechecks the dialog state. No arbitrary delay or weakened recorder assertion
was added. The strict real-browser path now proves Continuous, stable project
PCM ingest, WAV filename/status, Off PUT, and completed finalisation.

## Verification

- full MSVC build;
- normal-mode project authority route-denial and mutation-invariance smoke;
- compatibility-mode service, load, job, archive, module-runtime, capture, and
  legacy browser smokes;
- all 15 compatibility-labelled CTests;
- production compatibility-isolation Node contract;
- dedicated dialog failure/recovery browser contract plus the rapid-reopen
  path in the real-Chromium project shell smoke;
- real-Chromium project shell, Workspace oracle, and capture lifecycle smokes;
- OpenAPI YAML parse and compatibility-operation audit; and
- source whitespace/diff checks.

The remaining legacy code is a deliberate headless/oracle compatibility path,
not a hidden second production operator application.
