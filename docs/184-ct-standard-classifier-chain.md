# Click Train Standard Classifier And Chaining

Date: 2026-07-10

## Purpose

Completes the click train classifier chain apart from the template classifier: the composite `StandardClassifier` and `CTClassifierManager`'s chaining behaviour.

## Reference semantics ported

`CtStandardClassifier` (`StandardClassifier`) is a composite **AND gate**: every sub-classifier runs, and if any *enabled* one returns `NOSPECIES` or below, the composite returns `NOSPECIES`; otherwise it returns its own species flag. Sub-classifier verdicts are retained for reporting (`classify_detailed`), as the reference keeps them in its `StandardClassification`. A disabled sub-classifier cannot veto but is still reported.

`CtClassifierChain` (`CTClassifierManager.classify`) reproduces the two-stage flow:

1. The chi2 **pre-classifier** gates first. A rejection flags the train as junk, clears classifications, and short-circuits — no further classifier runs.
2. Otherwise, when classifiers are enabled, **every** classifier runs and all verdicts are retained, with the **first** one returning a species setting the classification index. Later matches do not overwrite it.

A shared `CtTrainSummary` carries what the classifiers read (chi2, click count, duration, IDI statistics, bearing clicks), with thin adapters exposing the three ported classifiers through a common `CtClassifier` interface so they compose.

## Validation

`ct_idi_bearing_classifier_branches` gained standard-classifier cases (all enabled and passing; one enabled failure vetoing; the same failure ignored when disabled but still reported) and chain cases (pre-classifier rejection flagging junk with no classifications; all verdicts retained with the first match setting the index; classifiers disabled running only the pre-classifier). Full CTest suite passes `64/64`.

## Claim boundary

Branch coverage, not fixture parity — same `PamController` coupling as `docs/177`/`docs/178`. The **template classifier** (`CTTemplateClassifier`, spectrum-template matched filtering) remains unported: it needs spectrum template data and a correlation path over click spectra, which is a genuinely separate piece of work rather than threshold logic. The chain is not yet wired into the served MHT path; `mhtClickTrains` carry the chi2, click count, and click times a caller needs to drive it.
