# Click Train Template Classifier

Date: 2026-07-10

## Purpose

Ports `CTTemplateClassifier`, the last click train classifier. It matches a train's average click spectrum against a species spectrum template, completing the classifier chain begun in `docs/177`.

## Reference semantics ported

`calcSpectrumCorrelation`:

1. The template spectrum, defined over `0 .. templateSampleRate`, is linearly interpolated onto the average spectrum's frequency grid (`0 .. sampleRate`), extrapolating zero outside the template's range. PAMGuard notes that the sample rate is used directly rather than halved, because only the *shape* comparison matters.
2. Both the interpolated template and the average spectrum are L2-normalised (`PamArrayUtils.normalise` divides by the root-sum-of-squares, so it distributes energy rather than peaking at one).
3. The correlation is their dot product — a cosine similarity reaching exactly one for identical shapes and unchanged by scaling either spectrum.

**Faithful detail worth knowing:** a failing correlation, a NaN, or a missing average spectrum returns `PRECLASSIFIERFLAG` (`-1`), *not* `NOSPECIES` (`0`). Both are at or below `NOSPECIES`, so either still vetoes the standard composite classifier (`docs/184`), but the distinct value is preserved because PAMGuard reports it. Default correlation threshold is 0.5.

## Validation

`ct_idi_bearing_classifier_branches` gained four template cases: an identical spectrum correlating to exactly 1.0 and classifying; a scaled spectrum giving the same correlation (proving normalisation invariance); a mismatched shape falling below threshold and returning `PRECLASSIFIERFLAG`; and a missing average spectrum returning the same flag. Full CTest suite passes `64/64`.

## Claim boundary

Branch coverage, not fixture parity — same `PamController` coupling as the rest of the chain (`docs/177`). PAMGuard's built-in `DefualtSpectrumTemplates` (beaked whale, porpoise, and similar preset spectra) are **not** transcribed; the template is supplied by the caller. The average click spectrum itself must come from the caller too: the engine's click feature extractor produces per-click power spectra with fixture parity, but averaging them across a train the way PAMGuard's `AverageWaveform` does is not yet wired. With this slice every click train classifier in PAMGuard's chain is ported.
