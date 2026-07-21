# Bearing Classifier Config And ICI Train Classification

Date: 2026-07-10

## Purpose

Closes the two follow-ups left by `docs/187`: the bearing classifier was ported but not reachable from config, and classification applied only to MHT trains.

## Bearing classifier config

`click.train.classifier.bearing` exposes the ported bearing classifier. Angles are configured in **degrees** and converted to the radians PAMGuard stores internally, since degrees are what an operator reasons about: `bearingLimMinDegrees`/`bearingLimMaxDegrees` (defaults 85/95) and the mean, median, and standard-deviation derivative limits in degrees per second, each behind its own `use*` flag.

Enabling it **requires** `click.localisation`, and session creation rejects the combination otherwise. Without localisation there are no per-click bearings, so the classifier would silently see empty bearings and reject every train — failing loudly is better than a config that quietly classifies nothing.

## ICI train classification

The chain now also runs over the max-ICI tracker's trains, producing a `clickTrainClassifications` array keyed by `trainId`, with the same fields as the MHT `classification` object.

Two notes on how the summary is built:

- The ICI tracker has **no MHT chi2**, so the summary reports zero. That passes the pre-classifier's chi2 test — which only rejects when chi2 *exceeds* the threshold — leaving the pre-classifier gating on click count and duration, which is the sensible reading of PAMGuard's intent for a non-MHT train.
- The tracker's own ICI statistics feed the IDI classifier directly, and those already have **bitwise Java fixture parity** (`docs/156`), so that classifier's inputs are as trustworthy here as anywhere in the port.

Member click waveforms and bearings are matched to trains by start sample to build the average spectrum and bearing clicks.

## Schema

Bumps the engine result `schemaVersion` to `14` (purely additive).

## Validation

`session_mht_train_wiring` gains an ICI-mode case asserting the steady 100 ms train is classified by its median ICI. Full CTest suite passes `65/65`.

## Claim boundary

Classifier verdicts are branch-covered rather than fixture-pinned (`docs/177`). Matching clicks to trains by start sample assumes start samples are unique within a chunk, which holds for the click detector's output. The whistle detection grouper and the project-import converter remain the open ledger items.
