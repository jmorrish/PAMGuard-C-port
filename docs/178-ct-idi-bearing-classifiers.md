# Click Train IDI And Bearing Classifiers

Date: 2026-07-10

## Purpose

Continues the click train classifier chain (`docs/177`) with the IDI and bearing classifiers, the two remaining classifiers whose inputs the engine already produces.

## Reference semantics ported

`CtIdiClassifier` (`IDIClassifier`): each enabled criterion — median, mean, standard deviation of the IDI series — must place the statistic inside inclusive limits, otherwise `NOSPECIES`. Defaults enable only the median (0 to 2 s). The statistics come from the IDI summary layer, which has bitwise Java fixture parity (`docs/156`), so the classifier is threshold logic over already-pinned values.

`CtBearingClassifier` (`BearingClassifier`): computes per-click bearing derivatives in radians per second, then tests mean, median, and population standard deviation against limits, plus a range test. Reference behaviours preserved deliberately:

- The range test passes when **either** the minimum or maximum bearing falls inside the bearing limits (not both).
- Trains shorter than three clicks return no species immediately.
- Clicks without a localisation are skipped but **leave a zero** in the derivative array rather than shortening it, so missing localisations drag the statistics toward zero.
- The final bearing is filled in after the loop, because the loop stops one short.
- Trains where missing localisations leave fewer than about three localised clicks are rejected.
- Statistics are reported even when the train fails, matching the reference returning them alongside `NOSPECIES`.

## Why there is no Java fixture

Same wall as `docs/177`: both classifiers take a `CTDataUnit`, and populating one reaches `PamController.getInstance()`. The decision logic is threshold comparisons over statistics the engine already computes with fixture parity, so it is transcribed and covered by `ct_idi_bearing_classifier_branches` — eight IDI cases (including limit boundaries, disabled criteria, and no-criteria-always-passes) and six bearing cases (short train, steady beam-aspect pass, out-of-range fail, fast-sweep fail, too-many-missing-localisations, and derivative criteria disabled). This is branch coverage, weaker than fixture parity, and the ledger says so.

## Claim boundary

The template classifier and the standard composite classifier remain unported, as does `CTClassifierManager` chaining (running several classifiers in sequence and combining their verdicts). None of the classifiers are wired into the served MHT path yet; `mhtClickTrains` carry the chi2 and click times needed to apply them downstream.
