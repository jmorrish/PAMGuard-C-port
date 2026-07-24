# Sweep click classifier

Date: 2026-07-24

Authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

## What is ported

`SweepClassifierWorker` and `SweepClassifierSet` are represented across the
whole runnable path:

- ordered enabled classifier sets, first-match behavior, discard, and
  `checkAllClassifiers`;
- require-all, require-one, and use-means channel policies;
- analytic-envelope length, calibrated amplitude, energy-band ratios, peak
  frequency, peak width, mean frequency, zero-crossing count/sweep,
  cross-correlation, and bearing-limit tests;
- restricted click-center/click-start spectra and the optional four-band FFT
  filter;
- Porpoise and Beaked Whale presets with defaults exported from Java;
- session runtime, JSON config/readback, result output, OpenAPI, browser
  controls, and real `.psfx` conversion.

The port intentionally preserves observable Java quirks, including the
use-means spectrum channel-0 double-add, integer peak-width bounds, strict
bearing endpoints, pass-on-missing-localisation behavior, and the
cross-correlation pair-array indexing.

## Java validation

`SweepClassifierFixtureExporter` allocates controller-bound test doubles
without running PAMGuard's GUI lifecycle, then executes the real
`SweepClassifierWorker`. Nineteen cases cover every decision branch,
filtering, channel aggregation, disabled-set handling, first match, and
multi-match output. A second fixture exports both real Java preset objects.

CTest target: `click_sweep_classifier_parity`.

## API

Select the runtime with:

```json
{
  "click": {
    "classifier": {
      "type": "sweep",
      "runOnline": true,
      "discardUnclassifiedClicks": false,
      "sweep": {
        "enabled": true,
        "checkAllClassifiers": false,
        "standardTypes": ["beakedWhale"],
        "types": []
      }
    }
  }
}
```

Every custom field is documented by `SweepClickType` in
`platform/openapi.yaml`. Schema version 30 adds `classifiersPassed` only when
multi-match output is non-empty.

## Related bearing gates

The separate `Detector Vetoes` settings unit is documented in `docs/229`.
During normal online Java classification a click has no localisation yet, so
an enabled Sweep bearing limit passes; the C++ runtime preserves that behavior.
The separate angle-veto gate then runs after delay measurement, in the same
order as Java.
