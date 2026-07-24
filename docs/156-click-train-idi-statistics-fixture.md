# Click Train IDI Statistics Fixture

Date: 2026-07-03

## Purpose

The click train tracker's ICI summary layer (mean, sorted median, population standard deviation) claimed PAMGuard-compatible statistics but had no Java fixture behind the claim. This slice generates a fixture by driving the real PAMGuard `clickTrainDetector.IDIInfo` class and proves the C++ summaries reproduce it bitwise.

## Exporter

`ClickTrainIdiFixtureExporter.java` subclasses `PamguardMVC.PamDataUnit` only to override `getTimeNanoseconds()` (the production accessor needs a parent data block's nano-time calculator); everything else — the nanosecond-time sort, IDI series construction, and `PamArrayUtils.mean/median/std` — is the real PAMGuard code path in `IDIInfo.calcTimeSeriesData`.

Because `PamArrayUtils` links against Apache commons-math at class-load time,
the generation script (`generate-click-train-idi-fixture.ps1`) asks the shared
oracle resolver to generate a local Maven dependency classpath under
`reference-tools/java/build/` and appends it to the run classpath.

## Scenarios

Scenario click times (milliseconds) are shared by name between the exporter and `click_train_idi_fixture_check.cpp`:

| Scenario | Pins |
| --- | --- |
| `regular-100ms` | 8 clicks at exactly 100 ms; mean is `0.09999999999999999` and std is `1.39e-17`, not the idealised 0.1/0 — the fixture captures Java's exact floating-point accumulation. |
| `jittered-even-idis` | 7 clicks, 6 IDIs; even-count median averages the two middle values. |
| `jittered-odd-idis` | 8 clicks, 7 IDIs; odd-count median takes the middle value. |
| `three-click-minimum` | Minimum train size accepted by `IDIInfo` (and the tracker's default `minClicks`). |
| `unsorted-jittered-even-idis` | Same clicks as `jittered-even-idis` supplied out of order; the row equals its sorted twin, pinning `IDIInfo`'s internal nanosecond-time sort. |

## Checker

The C++ check feeds each scenario through `ClickTrainTracker` with `start_sample = timeMs * 48` at 48 kHz, which makes the tracker's sample-based ICI values bitwise identical to PAMGuard's nanosecond-derived IDI values. It compares mean/median/std with **zero tolerance** — parity is exact, not approximate. The unsorted Java scenario is fed in time order on the C++ side: the streaming tracker's ingest contract is ordered clicks, and the fixture row itself proves order-independence of the Java statistics.

## Validation

- `click_train_idi_stats_parity` passes with `max_abs_error=0` across all five scenarios.
- Full CTest suite passes `48/48` on Windows Release.

## Claim boundary

This pins the IDI/ICI statistics layer, not train formation. PAMGuard's modern click train detector builds trains with MHT kernels, chi-square variables, pruning, and coasting; the C++ tracker still forms trains with a simple max-ICI/min-clicks rule. Ordering: PAMGuard sorts click times inside `IDIInfo`, while the C++ tracker relies on stream-ordered input by construction.
