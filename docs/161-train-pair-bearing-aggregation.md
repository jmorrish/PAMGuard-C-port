# Train Pair Bearing Aggregation

Date: 2026-07-03

## Purpose

Schema v5 added per-click PAMGuard pair bearing outputs on geometry-constrained delay pairs. Train-level consumers (track review, exports) previously had to walk every click. This slice aggregates pair bearings into the existing click train localisation pair-delay summaries.

## Behaviour

Each `clickTrainLocalisations[].pairDelays[]` item now carries, when at least one contributing click had a valid finite pair bearing for that pair:

- `pairBearingCount` — number of contributing clicks with a valid pair bearing.
- `meanPairBearingRadians` / `meanPairBearingDegrees` — arithmetic mean of the per-click PAMGuard pair bearing angles. Pair bearing angles are `acos` cone angles in `[0, pi]`, so an arithmetic mean has no wraparound concerns.

Non-finite per-click bearings (for example the `Infinity`-error endfire clamp still produces a finite angle, but a hypothetical non-finite angle) are excluded from the aggregation.

## Schema

Bumps the engine result `schemaVersion` to `7` (purely additive).

## Validation

- `click_train_localisation_summary_check` now asserts the mean and count across contributing clicks and asserts pairs without bearings report no aggregation.
- `service-smoke.ps1` asserts schema version 7 on health, live results, and archived records.
- Full CTest suite passes `51/51`.

## Claim boundary

This is a derived engine-side aggregation for web/API consumers, not a PAMGuard reference behaviour; PAMGuard's own train localisation aggregation (through its click train localiser chain) remains a gap tracked in the parity ledger.
