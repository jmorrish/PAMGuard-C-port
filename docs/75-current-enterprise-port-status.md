# Current enterprise port status

Updated: 2026-07-25

Status: **the one-project/Data Model workflow in `docs/247` is current.
Session, Workspace, and low-level graph measurements retained later in this
document are compatibility/core-runtime evidence, not the normal operator
architecture.**

## Primary architecture

- One active, ETag-guarded project is the configuration authority. Ordered
  controlled units own canonical settings, typed source bindings,
  deterministic hidden runtime nodes, data blocks, display providers/tabs,
  graph positions, and saved layout.
- The normal browser is a PAMGuard-style Data Model shell. A blank project has
  Data Model and global lifecycle controls only; menus and displays are
  generated from loaded controlled units and compatible data-block
  capabilities.
- Project Acquisition, capture, supervised ingest, Sound Output, Sound
  Recorder transport, retained clicks/tracked events, and PCM ingress use
  stable project/controlled-unit identities. Low-level graph/Workspace writes
  cannot mutate the project.
- Fixed `AnalysisSession`, session archive/jobs, legacy capture identities,
  low-level Acquisition/operator-input writes, persisted Workspace state, and
  the old browser are available only in explicit
  `PAMGUARD_LEGACY_MODEL_COMPAT=1` oracle mode.
- Current phased evidence is `docs/249` through `docs/257`. Documents
  `docs/240` through `docs/246` describe historical typed-runtime and UI
  foundations superseded by `docs/247` for the normal workflow.

## Engine foundations and retained compatibility evidence

The scientific foundations and measurements below remain valid. References to
session dashboards, Workspace, archive, or `/jobs` describe the isolated
compatibility harness unless a stable `/v1/projects/**` route is stated.

- Typed C++ module/data-block graph, plus the legacy session-per-source
  compatibility model with optional owner/tenant metadata.
- PAMGuard-compatible FFT/window/spectrogram scaffold with parity fixtures,
  Java-exact FFT click removal, and reusable spectrogram noise-reduction graph
  nodes.
- Click detector foundation with trigger filters, waveform capture, feature extraction, basic classifier, train timing summaries, delay localisation with derived physical delay units and geometry constraint metadata, click localisation readiness status, per-click far-field bearing foundation, derived train-localisation summaries, and derived train-bearing summaries.
- Whistles & Moans connected-region tracker fed directly from positive bins in the noise-reduced FFT output, including Java frequency/default settings, audio-channel grouping, fragment handling, cross-PCM-chunk continuity, contour summaries, localisation, flush, and schema-v32 raw-FFT background spectra. The older peak detector remains an independent optional output (`docs/231`, `docs/233`, `docs/234`).
- Per-session configuration for FFT, click detector/features/classifier/trains, whistle peaks/regions, array geometry, and output selectors.
- Active-project supervisor for WAV/MP3/Icecast/BUTT-style sources, direct
  shell-free FFmpeg launch, restart/backoff, realtime pacing, API-key/env-key
  auth, stable project/Acquisition discovery, audio-shape and running-state
  checks, working-revision fencing, durable sample cursors, repeated FFmpeg
  input-option passthrough, and optional audio filters for channel mapping.
  The session-bootstrap bridge is retained only as explicitly named
  compatibility.
- HTTP service with API-key/env-file protection, optional session owner/tenant enforcement, optional JSONL audit logging, static web UI serving, Prometheus metrics including optional ingest supervisor gauges, health readiness fields, optional ingest supervisor status projection, per-session operational status, owner/tenant/source session and archive filters, persistence, result archiving, detector-event archive projection and summaries, indexed event sidecar persistence, cursor paging, interval-overlap detection filters, live and archived click-to-train links, archive query caps/range filters, PCM body caps, HTTP thread pool, and transactional session creation.
- Browser dashboard for session creation/listing, optional ingest status lookup, per-session operational cards with click localisation readiness, detector configuration, click classifier JSON overrides, spectrogram preview, whistle contour overlay and contour summary cards, result counters, API key use, flush/delete, click output selectors, array geometry overrides, owner/tenant-scoped archive event querying/export, click-track link display, and PCM continuity display.
- Multi-source ingest supervisor for one direct FFmpeg worker per stable Sound
  Acquisition instance, with restart supervision, project/unit target
  metadata, schema-v3 health summaries, schema-v2 manifests, validation
  preflight, revision-keyed cursors, and redacted command previews.
- Dry-run-first grouped archive retention utility, detector-event index rebuild utility, and Kubernetes CronJob example for archive cleanup.
- Container runtime with FFmpeg, Python supervisor support, web UI, data volume, healthcheck, and deployment guardrails.
- Starter Kubernetes manifests for the engine service, API-key secret mounting,
  a one-replica active-project ingest supervisor with persistent cursor/status
  state, archive retention CronJob, and autoscaling/disruption examples.
- Windows GitHub Actions workflow for C++ build, CTest, service smoke, archive index rebuild smoke, and Python ops syntax checks.

## Current validation signal

- Full C++ build is green.
- The configured Windows CTest corpus now contains 163 scientific,
  controlled-unit, project-authority, browser, service, compatibility, and
  soak checks. The final integrated pass count is recorded in the Phase 7
  evidence rather than maintained as a second mutable number here.
- The real-Chromium project workflow covers blank startup, raw and true
  Decimator-to-FFT Spectrogram branches, continuous Click display,
  selected-source AudioWorklet delivery, Sound Recorder transport/WAV
  finalisation, remove/re-add with fresh identities, source loss, Save As,
  service restart, and restored project/display ownership.
- The project-authoritative pressure soak repeats an identical PCM prefix with
  and without six fast/throttled FFT-click-audio subscribers and requires exact
  count plus SHA-256 scientific payload equality. Its 60-second candidate
  passed 1,255 chunks / 5,140,480 frames, 20,204 FFT units, 2,526 clicks,
  22.7 ms maximum ingest, 48.5 MiB growth, queue depth at most four, and clean
  subscriber/runtime drain.
- The exact two-hour composable-graph soak passes with 154,042 chunks /
  315,478,016 frames through seven live nodes and four simultaneous fast/slow
  FFT, click-event, and audio clients; maximum ingest was 112.6 ms, peak
  working-set growth was 29.3 MiB, and no queue, history, observer, memory, or
  ingest invariant failed.
- `workspace-browser-smoke.ps1` still passes against a live service as
  explicitly labelled compatibility evidence. It is not the normal project
  display workflow.
- HTTP service smoke coverage is available through `cpp-engine/scripts/service-smoke.ps1` and passed against the current build, including optional ingest status projection and metrics, optional audit logging, session metadata enforcement, session listing, owner/tenant metadata propagation, per-session operational status, multi-channel click localisation/bearing outputs, physical delay units, geometry constraint metadata, schema-v5 PAMGuard pair bearing outputs on geometry-constrained delay pairs, schema-v6 PAMGuard LSQ bearing outputs for four-plus hydrophone sessions, and schema-v7 train-level pair bearing aggregation, archive sample-range, interval-overlap, and metadata filtering, detector-event summaries, indexed detector-event queries, cursor paging, metadata-aware CSV export, click-track/localisation/bearing events, and live/archived click-to-train event links.
- Multi-session service load smoke is available through `cpp-engine/scripts/service-load-smoke.ps1` and passed locally with `50` sessions and `2` chunks per session in both unauthenticated and API-key modes.
- The noise band monitor is ported with exact band-table parity across all six ANSI band types and served as `noiseBands` at schema v23, calibrated to dB re 1 uPa via ported `rawAmplitude2dB` — hydrophone `sensitivityDb` is now actually used (`docs/214-noise-band-monitor.md`).
- The separate FFT statistics noise monitor is ported against the real `noiseMonitor.NoiseProcess` with zero fixture error and served as `fftNoise` at schema v31; settings/API/browser/`.psfx` wiring is complete (`docs/230-fft-noise-monitor.md`).
- The Whistles & Moans raw-FFT background smoother matches all 32 values from
  the real `Spectrogram.SpectrumBackground` fixture with zero observed error;
  its snapshots are served as `whistleBackgrounds` at schema v32 and the
  interval setting is wired through HTTP/OpenAPI/browser/`.psfx` (`docs/233`).
- Whistles & Moans audio-channel grouping matches all eight group bitmaps from
  the real Java `GroupedSourceParameters` path; contour creation,
  backgrounds, and delay/bearing pairs now honor each group's first channel
  and membership (`docs/234`).
- The LTSA is ported with exact fixture parity against the real `LtsaProcess.ChannelProcess` (maxError 0 across 16 averaging periods, gap and alignment quirks pinned) and served as `ltsa` at schema v24 (`docs/215-ltsa.md`).
- The Ishmael energy-sum detector is ported with bit-exact fixture parity against the real `EnergySumProcess` + `IshPeakProcess` chain (640 values, 11 detections, maxRelError 0) and served as `ishmaelDetections` at schema v25 (`docs/216-ishmael-energy-sum.md`).
- The matched-template click classifier's maths is pinned to 5.1e-15 against the real `MTClassifier`/`ClickLength` chain, with Java-default settings/templates and `WavInterpolator` decimation pinned separately. It remains a partial module: click-train average-waveform input/`MATCHEDCLICK` flags, the click code/name provider, and MAT import are not yet implemented (`docs/217-matched-template-classifier.md`).
- The Ishmael spectrogram correlation detector is ported with 2.2e-16 fixture parity against the real `SgramCorrProcess` + `IshPeakProcess` chain (kernel exported row by row) and served as `sgramCorrDetections` at schema v27 (`docs/218-sgram-corr-detector.md`).
- The Ishmael matched filter is ported with 7.5e-14 fixture parity against the real `MatchFiltProcess2` + `IshPeakProcess` chain (kernel round-tripped through a real WAV) and served as `matchFiltDetections` at schema v28 — all three Ishmael detectors are now ported (`docs/219-match-filt-detector.md`).
- Archive-enabled throughput is measured: 50 detector-loaded sessions sustain 12.0× realtime with full result + audio archiving, 11.6× with the monitoring modules added on top (`docs/221-archive-throughput-benchmark.md`).
- PAMGuard's Filters package (Butterworth/Chebyshev design + the FastIIRFilter runtime) is ported at 2.4e-15 fixture parity and wired as the click detector's preFilter/triggerFilter with PAMGuard's exact signal flow — closing a parity gap in the click path that an end-to-end rumble-rejection test now demonstrates (`docs/213-click-iir-filters.md`).
- A structured security review of the HTTP surface is on record with two fixes applied (global payload ceiling, constant-time key comparison), full route-by-route auth verification, and the accepted risks stated (`docs/212-security-review.md`); the WP3 "direct Ethernet connector" is a recorded decision — covered by the PCM endpoint and FFmpeg's network protocols until a named device protocol exists (`docs/211-ethernet-connector-decision.md`).
- Shared-session subscribers (WP7): any number of viewers poll `GET /sessions/{id}/results?sinceSeq=K` against a bounded per-session ring with monotone sequence numbers; the engine session stays shared, and the smoke proves a non-posting viewer reads the poster's results incrementally (`docs/210-shared-session-subscribers.md`).
- The audio archive/replay store (WP3) records every accepted PCM chunk as raw f32le plus a self-describing index, exposes gaps via `GET /sessions/{id}/audio/index`, and replays archived audio through the job queue with the ORIGINAL chunk boundaries preserved — the smoke reproduces a live session's click count exactly (`docs/209-audio-archive-replay.md`).
- Offline WAV jobs can be queued and replayed through `/jobs` (WP7): same session machinery and archive writers as live audio, path-confined to `PAMGUARD_JOB_AUDIO_DIR`, with a CTest smoke proving detection counts, archive queryability, replay determinism, and traversal rejection (`docs/208-offline-job-queue.md`).
- Throughput is now **measured**, not just smoked: `cpp-engine/scripts/service-throughput-bench.ps1` sustains 50 detector-loaded live sessions (click + localisation + trains + whistle with noise reduction) at **26.7x realtime** with p95 chunk latency 48 ms on the development workstation, scaling linearly from 25 sessions (`docs/207-throughput-benchmark.md`) — the docs/01 load-test acceptance criterion is met for this machine and mix.
- Python ops compile checks pass for ingest supervision, grouped archive retention, and detector-event index rebuild tooling; rebuild dry-run/apply smoke passed on a sample archive and CTest smokes are registered for ingest supervisor status summaries, manifest command expansion, rebuild offset, metadata preservation, and retention grouping checks.
- Kubernetes starter manifests pass static presence/kind checks.
- PAMGuard project import: `PamguardProjectConverter` converts a real `.psfx` (read via the real `PSFXReadWriter`) into engine session JSON, and the HTTP smoke round-trips a sample written by the pinned version's own writer through the live engine (`docs/203-project-import-converter.md`).
- Existing parity/fixture coverage includes window functions, FFTs, spectrogram chunking, click trigger/features/classifier/train foundation, delay/bearing foundation, whistle peaks, connected regions, connected-region summary metrics, and rejoin/stub/flush scenarios.
- Correlation delay focused coverage now includes invalid config rejection, silent input behavior, zero search-window clamping, identical-signal zero-delay behavior, and PAMGuard fixture parity.
- Click train focused coverage now includes sub-minimum rejection, large-gap reset, channel-bitmap isolation, active summaries, completion, flush behavior, duration, ICI spread, ICI coefficient of variation, and interval click-rate metrics, plus bitwise Java fixture parity for IDI mean/median/std statistics against `clickTrainDetector.IDIInfo` (`docs/156-click-train-idi-statistics-fixture.md`) ported MHT IDI, length, and amplitude chi2 variables with fixture parity against the real Java classes including junk-track penalties, track-exclusion semantics, and the delta-path zero-ramp property (`docs/166-mht-idi-chi2-foundation.md`, `docs/167-mht-length-amplitude-chi2.md`), a ported MHT kernel with step-exact fixture parity against the real `MHTKernel` covering branch growth, pruning, coast confirmation, and the all-coasts backstop (`docs/168-mht-kernel-port.md`), the full StandardMHTChi2 stack with end-to-end fixture parity including perfect interleaved-train separation (`docs/169-standard-mht-chi2-stack.md`), and the MHT stack selectable as the served click train former at schema v10 via `click.train.algorithm`, with garbage reclamation for long streams, per-session kernel/chi2 parameters covering all five ported chi2 variables, and PAMGuard's click train classifier chain served over MHT trains at schema v13 (`docs/170`-`docs/187`).
- Basic click classifier focused coverage now includes bad config rejection, no-type defaults, non-match defaults, ordered type matching, and PAMGuard preset constants, plus an eleven-case Java decision fixture covering every criterion, discard propagation, zero-max-length skip, and no-selection semantics (`docs/155-click-classifier-case-fixture-sweep.md`).
- Sweep click classification now runs online with every pinned `SweepClassifierSet` criterion and channel mode, validated by nineteen cases through the real Java `SweepClassifierWorker`; settings round-trip through `.psfx`, HTTP/OpenAPI, and the browser (`docs/228-sweep-click-classifier.md`).
- Click detector angle vetoes preserve Java's list and inclusive absolute-angle semantics, run before every downstream consumer, and round-trip through `.psfx`, HTTP/OpenAPI, and the browser (`docs/229-click-angle-veto.md`).
- Click feature focused coverage now includes bad config rejection, empty waveform rejection, minimum FFT behavior, and channel metadata fallback.
- Click trigger focused coverage now includes bad config rejection, invalid chunk rejection, missing-channel rejection, trigger gating, waveform capture, and reset reproducibility, plus a Java fixture sweep covering min-separation split/merge, max-length truncation, min-trigger-channel gating/suppression, and alternate threshold/filter constants (`docs/154-click-trigger-edge-fixture-sweep.md`).
- Whistle peak focused coverage now includes bad config rejection, search-bin defaulting, bad slice rejection, reset reproducibility, broad-over-threshold suppression, and peak-width rejection, plus a ported whistle contour delay core with five-case Java fixture parity including narrowband delay ambiguity (`docs/164-whistle-delay-foundation.md`) schema-v9 cross-channel whistle region delays with geometry and pair bearing metadata served over HTTP (`docs/165-whistle-delay-service-output.md`), schema-v11 region-level whistle bearings with PAMGuard ambiguity semantics (`docs/175-whistle-region-bearing.md`), and schema-v12 full channel-pair whistle delays with LSQ bearings for four-plus hydrophone groups (`docs/180-whistle-lsq-bearing.md`).
- Localisation focused coverage now includes single-channel/no-pair handling, max-delay validation, partial/missing hydrophone geometry, one-pair bearing behavior, and invalid config rejection, plus ported PAMGuard `PairBearingLocaliser` and `LSQBearingLocaliser` implementations with Java fixtures covering endfire clamping, negative spacing, the three-delay reduction, Jama QR least-squares weighting, curvature error semantics, and rank-deficiency behaviour (`docs/157-pair-bearing-localiser-port.md`, `docs/158-lsq-bearing-localiser-port.md`), a controlled end-to-end check pinning the engine delay-sign/bearing convention (`docs/162-pair-bearing-physical-convention.md`), and ported `ArrayManager` array shape/direction semantics with the PAMGuard pair spacing sign flip applied to served pair bearings at schema v8 (`docs/163-array-shape-semantics-port.md`), plus streamer geometry with positions and heading/pitch/roll attitude resolved into hydrophone coordinates as `HydrophoneLocator.getPhoneLatLong` does, with eight-case fixture parity against the real `PamQuaternion`/`PamVector.rotateVector` (`docs/190-streamer-geometry.md`, `docs/193-streamer-orientation.md`), and PAMGuard's `BearingLocaliserSelector` switch choosing the bearing localiser from sub-array shape rather than channel count, reported as `arrayShape`/`bearingLocaliser` at schema v16 with the MLGrid-for-LSQ substitution documented (`docs/194-bearing-localiser-selection.md`), and `MLGridBearingLocaliser2` itself ported with eleven-case fixture parity to 7e-17 including Jama's LU inverse and PeakSearch's 2D interpolated peak (`docs/195-ml-grid-bearing-localiser.md`), served as `gridBearing` at schema v17 with per-hydrophone coordinate errors in session config (`docs/196-grid-bearing-service-output.md`), and PAMGuard's `AbstractLocalisation.getWorldVectors` ported with exact twelve-case fixture parity and served as `gridBearing.worldVectors` at schema v18, giving directions in the array's own xyz frame with the mirror/left-right ambiguity each array shape cannot resolve carried explicitly (`docs/197-world-vectors.md`), extended at schema v19 to pair bearings (`pairBearingWorldVectors`) and LSQ bearings, the latter deliberately skipping the array-axis rotation because `LSQBearingLocaliser` fits raw inter-hydrophone vectors (`docs/198-pair-and-lsq-world-vectors.md`), and to the earth frame at schema v21 via ported `getRealWorldVectors` when a static `array.orientation` is declared (`docs/202-earth-frame-vectors.md`).

## Still not safe to claim as complete PAMGuard equivalence

- `docs/126-pamguard-parity-ledger.md` tracks the exact fixture/foundation/gap boundary for PAMGuard equivalence claims.
- `docs/136-multichannel-localisation-operation.md` describes the current multi-channel localisation path and its claim boundary.
- Click train tracking is still a foundation and not a full PAMGuard click train/localisation module clone.
- Bearing/localisation output is a far-field foundation and needs more PAMGuard array model parity before being treated as final scientific output.
- The graph editor exposes every registered module's complete settings JSON,
  typed inputs, defaults, validation, and lifecycle. The older focused
  detector dialogs remain useful for operator-friendly editing, but
  Java Swing layout/preferences, legacy colours, and other desktop-only
  presentation state are not port targets.
- Result storage is append-only NDJSON plus indexed detector-event sidecars, not yet a query-indexed database with migrations and richer ad-hoc query planning.
- Service validation is strong for this engine, but not a substitute for full PAMGuard project/config import parity.

## Subsequent correctness priorities

- Continue Java fixture extraction for click train/localisation edge cases.
- Continue refining derived train-level localisation/bearing aggregation as PAMGuard reference behaviour is pinned down.
- Add indexed result storage, retention policies, and migration tooling.
- Consider a public module/display plugin ABI only after the compiled-in
  contracts have remained stable.
- Add policy-driven copy/move/remote backup actions only with explicit
  destination, credential, retention, and destructive-action requirements;
  periodic storage-health monitoring is already present.
