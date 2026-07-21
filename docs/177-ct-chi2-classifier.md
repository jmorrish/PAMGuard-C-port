# Click Train Chi2 Threshold Classifier

Date: 2026-07-10

## Purpose

Starts the click train classification chain with `Chi2ThresholdClassifier`, the simplest and most widely used of PAMGuard's click train classifiers: it accepts a train as a species when its MHT chi2 and size/duration clear configured thresholds.

## Reference semantics ported

`Chi2ThresholdClassifier.classifyClickTrain` rejects to `NOSPECIES` in this order, and the port matches each branch:

1. `durationInMilliseconds < minTime * 1000`
2. `subDetectionsCount < minClicks`
3. `chi2 > chi2Threshold` **and** `chi2Threshold != 0` — so a zero threshold disables the chi2 test entirely
4. the `minPercentage` data-selector criterion (not ported, see below)

Otherwise the configured species flag is returned. Defaults follow `Chi2ThresholdParams`: threshold 1500, minClicks 5, minTime 0 s. Note the comparisons are strict, so a train exactly at a threshold passes.

## Why there is no Java fixture

Unlike every other slice in this arc, this classifier could not be driven headlessly. `classifyClickTrain` needs a real `CTDataUnit`, and populating one reaches `CTDataUnit.addSubDetection` → `addToAverageWaveform` → `PamController.getInstance().getRunMode()`. The parent-data-block dependency could be stubbed, but the `PamController` singleton is the same GUI-coupled wall that blocked driving `ArrayManager` and `PairBearingLocaliser.prepare()` directly.

The decision logic is four threshold comparisons, so it is transcribed from the source and covered by `ct_chi2_classifier_branches`, an eight-case branch catalogue that pins each rejection path plus the boundary cases (chi2 exactly at threshold, clicks exactly at minimum, duration exactly at minimum, zero threshold). This is a weaker guarantee than fixture parity and is recorded as such in the ledger.

## Claim boundary

Only the chi2 threshold classifier is ported, and only its threshold logic: the `minPercentage` data-selector criterion is unported (PAMGuard short-circuits it at its default of zero, which is what the port assumes). The other classifiers in the chain — bearing, IDI, template, and the standard composite classifier — are untouched, as is `CTClassifierManager` chaining. The classifier is not yet wired into the served MHT path; `mhtClickTrains` carry chi2, so classification can be applied downstream.
