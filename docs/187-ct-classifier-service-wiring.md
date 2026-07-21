# Click Train Classifier Service Wiring

Date: 2026-07-10

## Purpose

Closes the last open click train item. `docs/177`-`docs/186` ported every classifier and PAMGuard's default templates; the blocker to serving them was train average-spectrum construction. This slice ports that and wires the chain into the served MHT path.

## Train average spectrum

`ct_train_average_spectrum` ports the spectrum half of PAMGuard's `AverageWaveform`: each member click's waveform is zero-padded to the FFT length, transformed, and its magnitude-squared spectrum summed bin-wise across the train, using the engine's existing PAMGuard-compatible FFT and packing (the path that already has click feature fixture parity).

Two reference behaviours preserved:

- Spectra are **summed, not averaged**. The template classifier L2-normalises before correlating, so this does not change classification, but the values are sums.
- Spectra are summed **without** the time-delay alignment used for the average *waveform*. PAMGuard does this deliberately so interference during waveform averaging cannot distort the spectrum.

## Service wiring

`click.train.classifier` enables the chain over MHT trains, with a `preClassifier` block plus optional `idi` and `template` classifiers. The template classifier accepts either a PAMGuard `preset` name (validated against the ported defaults from `docs/186`) or an explicit `spectrum` with its `sampleRateHz`; a template that is neither is rejected at session creation.

Each `mhtClickTrains` entry then carries a `classification` object: `junkTrain` (the pre-classifier verdict), `speciesId` (the first classifier to return one), `classifierSpeciesIds` (every verdict, retained as PAMGuard does), and `templateCorrelation` when the template classifier ran. The session builds each train's IDI statistics from its member click intervals and its bearing clicks from the per-click bearings already threaded in for the bearing chi2 variable.

## Schema

Bumps the engine result `schemaVersion` to `13` (purely additive).

## Validation

`session_mht_train_wiring` gains a classifier case: with a permissive pre-classifier and an IDI classifier bracketing the synthetic 100 ms ICI, the steady train is classified with the configured species flag; narrowing the IDI window past the train's ICI rejects it. Full CTest suite passes `65/65`.

## Claim boundary

The bearing classifier is not exposed in the config surface yet (it is ported and covered, but needs per-click bearings to be meaningful, so enabling it blindly would mislead). Classification is applied to MHT trains only, not to the ICI tracker's `clickTrains`. The classifiers themselves remain branch-covered rather than fixture-pinned (`docs/177`), and the train average spectrum has focused coverage through the classifier path rather than a dedicated Java fixture, since `AverageWaveform` is reachable only through `PamController`-coupled data units.
