# PAMGuard parity ledger

Date: 2026-07-10

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
| Click train tracker | Foundation | Active/completed/flush/channel/gap/min-click tests; IDI mean/median/std statistics have bitwise Java fixture parity against `clickTrainDetector.IDIInfo` (`docs/156-click-train-idi-statistics-fixture.md`); the MHT IDI, length, and amplitude chi2 variables are ported with real-class fixture parity including junk penalties and the delta-path zero-ramp property (`docs/166`, `docs/167`); the MHT kernel is ported with step-exact fixture parity (`docs/168`), and the full StandardMHTChi2 stack (kernel + combiner + IDI/amplitude/length variables) has end-to-end fixture parity against the real Java stack including perfect interleaved-train separation (`docs/169-standard-mht-chi2-stack.md`) | Served train formation is selectable at schema v10: the max-ICI rule or the ported MHT stack (`docs/170-mht-train-service-output.md`) with MHTGarbageBot reclamation for long streams (`docs/171-mht-garbage-reclamation.md`); the bearing and peak-frequency variables are ported (`docs/172`) and selectable per session alongside tunable kernel/chi2 parameters (`docs/173-mht-session-parameters.md`); the electrical noise filter is ported with appended stack-fixture cases pinning its exact trigger point (`docs/174`), and the time-delay variable is ported with its drop-the-worst-pair rule pinned (`docs/176-mht-time-delay-chi2.md`, not yet wired into the served stack); and the chi2-threshold, IDI, and bearing click train classifiers are ported with branch coverage but **no Java fixture** (all PamController-coupled, `docs/177`, `docs/178-ct-idi-bearing-classifiers.md`); the correlation variable is ported too (`docs/179`), completing every MHT chi2 variable, and all five are selectable in the served stack (`docs/183`); the standard composite classifier, CTClassifierManager chaining, and the spectrum-template classifier are ported with branch coverage (`docs/184`, `docs/185-ct-template-classifier.md`), completing the classifier chain, and PAMGuard's five default spectrum templates have exact fixture parity (`docs/186-ct-spectrum-templates.md`); and the chain is served over both MHT and ICI-tracker trains at schema v14 with ported train average-spectrum construction and full classifier config including bearings (`docs/187`, `docs/188-ct-bearing-classifier-and-ici-trains.md`). |
| Correlation delay estimator | Fixture parity | Java fixture parity plus edge coverage | Silent/no-peak behavior now guarded to avoid false max-delay outputs. |
| Delay group estimator | Fixture parity | 3-channel Java fixture plus edge coverage | Pairwise delay order and max-delay validation covered. |
| Far-field click bearing | Foundation | Geometry/config edge coverage and HTTP smoke | Needs deeper PAMGuard bearing localiser parity and array model semantics. |
| Pair bearing localiser | Fixture parity | Seven-case Java fixture for angle/error including endfire clamp, negative spacing, and three-delay reduction (`docs/157-pair-bearing-localiser-port.md`); exposed per geometry-constrained delay pair at result schema v5 (`docs/159-pair-bearing-service-output.md`) | `prepare()`-side array derivation is config-supplied; train-level aggregation is a follow-up. |
| LSQ bearing localiser | Fixture parity | Four-case fixture generated by real Jama QR/PamVector; rank-deficiency semantics pinned (`docs/158-lsq-bearing-localiser-port.md`); exposed per click at result schema v6 for four-plus hydrophone sessions (`docs/160-lsq-bearing-service-output.md`) | Needs at least four non-coplanar hydrophones by construction. Localiser selection now follows PAMGuard's `BearingLocaliserSelector` switch on sub-array shape rather than channel count, reported as `arrayShape`/`bearingLocaliser` at schema v16 (`docs/194-bearing-localiser-selection.md`); MLGrid, MLLine, simplex, and combined localisers remain unported, so a plane or volume sub-array runs LSQ where PAMGuard runs its grid search — a documented substitution, not parity. |
| ML grid bearing localiser | Fixture parity | Eleven-case Java fixture over plane, volume, and line sub-arrays with speed-of-sound and asymmetric position errors, matching to 7e-17 against the real `PamVector`/`Jama.Matrix.inverse()`/`PeakSearch` (`docs/195-ml-grid-bearing-localiser.md`) | Ports Jama's LU inverse and PeakSearch's 2D interpolated peak alongside. Served as `gridBearing` on click localisations and whistle delays at schema v17, with per-hydrophone coordinate errors in session config (`docs/196-grid-bearing-service-output.md`); angles are the reference's theta/phi in the array's principal axis frame, not compass azimuth/elevation. Streamer-level separation errors and the reference's dead crawl/simplex/bisection search paths are unported. |
| Array shape/directions | Fixture parity | Twelve-case Java fixture driving real PamVector maths, including streamer-scoped uniqueness (`docs/163-array-shape-semantics-port.md`, `docs/181-multi-streamer-arrays.md`); pair spacing sign flip wired into pair bearings at schema v8 | Streamers are declarable in session config with positions folded into hydrophone coordinates as `getAbsHydrophoneVector` does (`docs/190-streamer-geometry.md`), and streamer heading/pitch/roll now rotate those coordinates before the offset as `HydrophoneLocator.getPhoneLatLong` does, with eight-case fixture parity against the real `PamQuaternion`/`PamVector.rotateVector` pinning the clockwise-heading and pitch-roll-heading Euler conventions (`docs/193-streamer-orientation.md`); time-varying locators remain unported. |
| Click train localisation summaries | Foundation | Summary tests | Aggregation behavior implemented; needs reference behaviour pinned down for production science. |
| Click train bearing summaries | Foundation | Summary tests | Same caveat as train localisation summaries. |
| Whistle peak detector | Fixture parity | Better peak fixture and edge coverage | Needs broader fixture sweep across PAMGuard whistle settings. |
| Connected-region whistle/moan tracker | Fixture parity | Basic, flush, stub, discard, branch, rejoin, split/cross fixtures | Strong current fixture coverage for contour-building behaviour. |
| Whistle/moan localisation | Foundation | Contour delay core ported with five-case Java fixture parity (`docs/164-whistle-delay-foundation.md`); cross-channel region delays with geometry/pair-bearing metadata served at schema v9 (`docs/165-whistle-delay-service-output.md`), region-level bearings with PAMGuard ambiguity semantics at schema v11 (`docs/175`), and the full channel-pair set plus LSQ bearings for four-plus hydrophone groups at schema v12 (`docs/180-whistle-lsq-bearing.md`) | The detection grouper is ported with branch coverage including its non-intersection frequency-overlap quirk and served as `whistleGroups` at schema v15 (`docs/189-whistle-detection-grouper.md`, `docs/191-whistle-group-service-output.md`); array-axis reference fields remain unported, localiser selection is channel-count based, and group *localisation* is a documented non-port (`docs/192-group-localiser-non-port.md`). |
| PAMGuard project/config import | Gap | Feasibility established with a working settings inspector (`docs/182-project-import-feasibility.md`) | Settings payloads are Java-serialised object graphs, so import must run on the JVM and emit engine JSON — it cannot be ported to C++. The format is also version-brittle: the repo's sample `.psf` fails to load against PAMGuard's own current build. Requires a pinned target version. |
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
- Whistle/moan localisation is implemented beyond the contour delay core.
- PAMGuard project import/config equivalence is implemented.
- The system has production throughput proof for 50+ real detector-heavy live streams.

## Next parity priorities

- Whistle group *localisation* is a deliberate non-port: PAMGuard's `DetectionGroupLocaliser` is `@Deprecated`, needs a GPS/track model the engine lacks, and solves a different problem than instantaneous multi-array groups (`docs/192-group-localiser-non-port.md`).
- Model time-varying streamer positions and attitude for towed arrays, which needs a sensor/GPS feed path the engine does not have; static geometry and static attitude are both ported (`docs/190-streamer-geometry.md`, `docs/193-streamer-orientation.md`).
- Establish an array orientation reference so grid bearings can be reported as compass azimuth and elevation rather than principal-axis theta/phi (`docs/196-grid-bearing-service-output.md`).
- Build PAMGuard project import as a JVM-side converter against a pinned PAMGuard version, extending `PamguardSettingsInspector` (`docs/182-project-import-feasibility.md`).
- Keep every parity claim tied to a fixture, source trace, or focused regression test.
