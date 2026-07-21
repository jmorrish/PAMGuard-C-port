# MHT Correlation Chi2 Variable

Date: 2026-07-10

## Purpose

Completes the MHT chi2 variable set with `CorrelationChi2`, which favours click trains whose waveforms stay similar. This was the last unported variable.

## Reference semantics ported

Over the `SimpleChi2Var` base path, the per-pair term is:

```text
log(1/corr)^2 / max(minError, idi * error)^2
```

so a correlation of one contributes exactly zero and lower correlations grow logarithmically rather than linearly (the reference comments note the log scale "seems to work" where linear does not). Defaults: error 1, minError 0.01.

The correlation value between consecutive in-track clicks is **supplied by the caller**. PAMGuard derives it lazily through its `CorrelationManager`, optionally applying an FFT filter first; the engine has its own correlation estimator whose delay score is the same normalised peak quantity and which already has Java fixture parity (`docs/121`, correlation delay fixtures). The optional FFT pre-filter is not ported.

## Validation

`ct_idi_bearing_classifier_branches` gained correlation cases asserting the arithmetic directly: perfect correlation scores exactly zero, a correlation of 0.5 at 100 ms spacing matches the hand-computed `log(2)^2 / 0.1^2` divided by the bit count, and weaker correlation scores strictly worse than stronger. Full CTest suite passes `64/64`.

## Claim boundary

No Java fixture drives this variable: doing so needs `CorrelationManager` operating on real waveform-bearing data units, which reaches the same `PamController` coupling described in `docs/177`. The formula is short and is pinned arithmetically instead. Like the time-delay variable, it is not wired into the served MHT stack — that needs per-click-pair correlation values threaded into the MHT units. With this slice every MHT chi2 variable in `StandardMHTChi2.createChi2Vars()` is now ported.
