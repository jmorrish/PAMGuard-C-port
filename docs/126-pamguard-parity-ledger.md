# PAMGuard parity ledger

Date: 2026-07-03

## Purpose

This ledger tracks how close the C++/web engine is to PAMGuard behaviour for the modules in scope.

The target is not approximate similarity. The target is PAMGuard-compatible maths and configuration semantics for the selected modules.

## Status categories

- `Fixture parity`: covered by exported PAMGuard Java fixture(s) and C++ parity tests.
- `Foundation`: implemented in C++ with focused tests, but not yet proven as full PAMGuard equivalence.
- `Operational`: web/service/deployment feature that supports enterprise use but is not itself PAMGuard detector maths.
- `Gap`: required before claiming full module equivalence.

## Current ledger

| Area | Status | Evidence | Notes |
| --- | --- | --- | --- |
| Window functions | Fixture parity | CTest parity fixtures | Used by FFT/spectrogram path. |
| Real FFT | Fixture parity | CTest parity fixtures | Core spectral primitive. |
| Spectrogram chunking | Fixture parity | CTest parity fixtures | Browser preview and downstream whistle/click features use this foundation. |
| Click trigger foundation | Fixture parity | Seven Java fixtures: basic, min-sep split/merge, max-length truncation, min-trigger-channel gating/suppression, alternate threshold/filter constants (`docs/154-click-trigger-edge-fixture-sweep.md`) | Cross-chunk filter initialisation and the dual-alpha long-filter path remain outside fixture coverage. |
| Click feature extraction | Fixture parity | Feature fixtures and edge coverage | Basic feature set covered; broader PAMGuard feature/config combinations still need fixtures. |
| Basic click classifier | Fixture parity | Eleven-case Java decision fixture covering every criterion, ordering, discard, zero-max-length skip, and no-selection semantics (`docs/155-click-classifier-case-fixture-sweep.md`), plus preset constants and edge coverage | Exporter mirrors the PAMGuard decision loop with PAMGuard transform classes; real PAMGuard project/config import is still a gap. |
| Click train tracker | Foundation | Active/completed/flush/channel/gap/min-click tests; IDI mean/median/std statistics have bitwise Java fixture parity against `clickTrainDetector.IDIInfo` (`docs/156-click-train-idi-statistics-fixture.md`) | Train formation is a max-ICI/min-clicks rule, not the PAMGuard MHT click train detector. |
| Correlation delay estimator | Fixture parity | Java fixture parity plus edge coverage | Silent/no-peak behavior now guarded to avoid false max-delay outputs. |
| Delay group estimator | Fixture parity | 3-channel Java fixture plus edge coverage | Pairwise delay order and max-delay validation covered. |
| Far-field click bearing | Foundation | Geometry/config edge coverage and HTTP smoke | Needs deeper PAMGuard bearing localiser parity and array model semantics. |
| Click train localisation summaries | Foundation | Summary tests | Aggregation behavior implemented; needs reference behaviour pinned down for production science. |
| Click train bearing summaries | Foundation | Summary tests | Same caveat as train localisation summaries. |
| Whistle peak detector | Fixture parity | Better peak fixture and edge coverage | Needs broader fixture sweep across PAMGuard whistle settings. |
| Connected-region whistle/moan tracker | Fixture parity | Basic, flush, stub, discard, branch, rejoin, split/cross fixtures | Strong current fixture coverage for contour-building behaviour. |
| Whistle/moan localisation | Gap | None yet | PAMGuard whistle TOAD/bearing localisation is not ported. |
| PAMGuard project/config import | Gap | None yet | Current config is explicit JSON, not PAMGuard project import. |
| Archive detector events | Operational | HTTP smoke and indexed sidecar tests | Supports web/API workflows; not detector maths. |
| Multi-session service operation | Operational | 50-session smoke | Functional isolation smoke, not a detector throughput benchmark. |
| Icecast/BUTT/stream ingest | Operational | FFmpeg ingest bridge and supervisor dry-run checks | Needs long-running soak tests against real sources. |

## Current claim boundary

It is safe to say:

- The port has a working C++ service and web dashboard for the scoped detector foundations.
- Several mathematical primitives and detector subcomponents have PAMGuard fixture parity.
- Multi-channel click delay/bearing outputs pass through the HTTP/archive path.
- The architecture is moving toward enterprise operation with session isolation, indexed archives, ingest supervision, and Kubernetes starters.

It is not safe to say:

- The click detector module is a full PAMGuard clone.
- The click train/localisation modules are fully equivalent to PAMGuard.
- Whistle/moan localisation is implemented.
- PAMGuard project import/config equivalence is implemented.
- The system has production throughput proof for 50+ real detector-heavy live streams.

## Next parity priorities

- Export more Java fixtures for click train creation and train classification/localisation behavior.
- Pin down PAMGuard bearing localiser semantics for common array geometries.
- Add whistle/moan localisation fixtures before implementing that path in C++.
- Keep every parity claim tied to a fixture, source trace, or focused regression test.
