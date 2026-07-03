# Current enterprise port status

Date: 2026-07-03

## Implemented engine foundations

- C++ session-per-source processing model with optional owner/tenant metadata for web-tier orchestration.
- PAMGuard-compatible FFT/window/spectrogram scaffold with parity fixtures.
- Click detector foundation with trigger filters, waveform capture, feature extraction, basic classifier, train timing summaries, delay localisation with derived physical delay units and geometry constraint metadata, click localisation readiness status, per-click far-field bearing foundation, derived train-localisation summaries, and derived train-bearing summaries.
- Whistle/moan peak detector and connected-region tracker, including fragment discard, branch, rejoin, stub handling, contour output, timing/frequency contour summaries, and flush.
- Per-session configuration for FFT, click detector/features/classifier/trains, whistle peaks/regions, array geometry, and output selectors.
- FFmpeg bridge for WAV/MP3/Icecast/BUTT-style sources, restart/backoff, realtime pacing, API key/env-key auth, session bootstrap with source/owner/tenant overlays, continuity logging, resume controls, session shape checks, repeated FFmpeg input-option passthrough, and optional FFmpeg audio filters for channel mapping.
- HTTP service with API-key/env-file protection, optional session owner/tenant enforcement, optional JSONL audit logging, static web UI serving, Prometheus metrics including optional ingest supervisor gauges, health readiness fields, optional ingest supervisor status projection, per-session operational status, owner/tenant/source session and archive filters, persistence, result archiving, detector-event archive projection and summaries, indexed event sidecar persistence, cursor paging, interval-overlap detection filters, live and archived click-to-train links, archive query caps/range filters, PCM body caps, HTTP thread pool, and transactional session creation.
- Browser dashboard for session creation/listing, optional ingest status lookup, per-session operational cards with click localisation readiness, detector configuration, click classifier JSON overrides, spectrogram preview, whistle contour overlay and contour summary cards, result counters, API key use, flush/delete, click output selectors, array geometry overrides, owner/tenant-scoped archive event querying/export, click-track link display, and PCM continuity display.
- Multi-source ingest supervisor for one bridge process per source/session, with restart supervision, source/session metadata, top-level health summaries in optional status-file output, source manifests, validation preflight, and redacted command previews.
- Dry-run-first grouped archive retention utility, detector-event index rebuild utility, and Kubernetes CronJob example for archive cleanup.
- Container runtime with FFmpeg, Python supervisor support, web UI, data volume, healthcheck, and deployment guardrails.
- Starter Kubernetes manifests for the engine service, API-key secret mounting, example ingest workers, archive retention CronJob, and autoscaling/disruption examples.
- Windows GitHub Actions workflow for C++ build, CTest, service smoke, archive index rebuild smoke, and Python ops syntax checks.

## Current validation signal

- Full C++ build is green.
- `ctest` passes `49/49` tests on this Windows build, including registered service smoke tests, API-key-file smoke coverage, FFmpeg ingest help coverage, ingest supervisor status/command smoke coverage, archive event index rebuild smoke coverage, and grouped archive retention smoke coverage.
- HTTP service smoke coverage is available through `cpp-engine/scripts/service-smoke.ps1` and passed against the current build, including optional ingest status projection and metrics, optional audit logging, session metadata enforcement, session listing, owner/tenant metadata propagation, per-session operational status, multi-channel click localisation/bearing outputs, physical delay units, and geometry constraint metadata, archive sample-range, interval-overlap, and metadata filtering, detector-event summaries, indexed detector-event queries, cursor paging, metadata-aware CSV export, click-track/localisation/bearing events, and live/archived click-to-train event links.
- Multi-session service load smoke is available through `cpp-engine/scripts/service-load-smoke.ps1` and passed locally with `50` sessions and `2` chunks per session in both unauthenticated and API-key modes.
- Python ops compile checks pass for ingest supervision, grouped archive retention, and detector-event index rebuild tooling; rebuild dry-run/apply smoke passed on a sample archive and CTest smokes are registered for ingest supervisor status summaries, manifest command expansion, rebuild offset, metadata preservation, and retention grouping checks.
- Kubernetes starter manifests pass static presence/kind checks.
- Existing parity/fixture coverage includes window functions, FFTs, spectrogram chunking, click trigger/features/classifier/train foundation, delay/bearing foundation, whistle peaks, connected regions, connected-region summary metrics, and rejoin/stub/flush scenarios.
- Correlation delay focused coverage now includes invalid config rejection, silent input behavior, zero search-window clamping, identical-signal zero-delay behavior, and PAMGuard fixture parity.
- Click train focused coverage now includes sub-minimum rejection, large-gap reset, channel-bitmap isolation, active summaries, completion, flush behavior, duration, ICI spread, ICI coefficient of variation, and interval click-rate metrics, plus bitwise Java fixture parity for IDI mean/median/std statistics against `clickTrainDetector.IDIInfo` (`docs/156-click-train-idi-statistics-fixture.md`).
- Basic click classifier focused coverage now includes bad config rejection, no-type defaults, non-match defaults, ordered type matching, and PAMGuard preset constants, plus an eleven-case Java decision fixture covering every criterion, discard propagation, zero-max-length skip, and no-selection semantics (`docs/155-click-classifier-case-fixture-sweep.md`).
- Click feature focused coverage now includes bad config rejection, empty waveform rejection, minimum FFT behavior, and channel metadata fallback.
- Click trigger focused coverage now includes bad config rejection, invalid chunk rejection, missing-channel rejection, trigger gating, waveform capture, and reset reproducibility, plus a Java fixture sweep covering min-separation split/merge, max-length truncation, min-trigger-channel gating/suppression, and alternate threshold/filter constants (`docs/154-click-trigger-edge-fixture-sweep.md`).
- Whistle peak focused coverage now includes bad config rejection, search-bin defaulting, bad slice rejection, reset reproducibility, broad-over-threshold suppression, and peak-width rejection.
- Localisation focused coverage now includes single-channel/no-pair handling, max-delay validation, partial/missing hydrophone geometry, one-pair bearing behavior, and invalid config rejection, plus a ported PAMGuard `PairBearingLocaliser` with a seven-case Java fixture covering endfire clamping, negative spacing, and the three-delay reduction (`docs/157-pair-bearing-localiser-port.md`).

## Still not safe to claim as complete PAMGuard equivalence

- `docs/126-pamguard-parity-ledger.md` tracks the exact fixture/foundation/gap boundary for PAMGuard equivalence claims.
- `docs/136-multichannel-localisation-operation.md` describes the current multi-channel localisation path and its claim boundary.
- Click train tracking is still a foundation and not a full PAMGuard click train/localisation module clone.
- Bearing/localisation output is a far-field foundation and needs more PAMGuard array model parity before being treated as final scientific output.
- The web UI exposes the major module controls implemented here, not every PAMGuard desktop parameter.
- Result storage is append-only NDJSON plus indexed detector-event sidecars, not yet a query-indexed database with migrations and richer ad-hoc query planning.
- Service validation is strong for this engine, but not a substitute for full PAMGuard project/config import parity.

## Next correctness priorities

- Continue Java fixture extraction for click train/localisation edge cases.
- Continue refining derived train-level localisation/bearing aggregation as PAMGuard reference behaviour is pinned down.
- Add deeper API/integration tests around persistence/archive/continuity/error paths.
- Add indexed result storage, retention policies, and migration tooling.
- Keep expanding OpenAPI and browser config surfaces as detector parity grows.
