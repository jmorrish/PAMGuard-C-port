# Click Feature Foundation

Date: 2026-06-30

This checkpoint adds a PAMGuard-parity click feature layer underneath click classification.

## Implemented

- Added `ClickFeatureExtractor` in the C++ backend.
- Cloned PAMGuard click feature maths for:
  - click Hann windowing using the click-specific `length - 1` denominator;
  - PAMGuard/JTransforms packed real-FFT magnitude-square semantics;
  - `PamUtils.getMinFftLength` behaviour with minimum FFT length 4;
  - `ClickDetection.inBandEnergy(...)`, including the `10 * log10(energy) + 172` offset;
  - `ClickDetection.clickLength(...)` smoothing and peak-expansion width logic;
  - total power spectrum, peak frequency, peak width by energy fraction, and mean frequency.
- Added per-channel power spectra and length features.
- Added top-level click feature results to `AnalysisSession`.
- Exposed click features through `pamguard_engine_service` JSON via `clickFeatures`.
- Added configurable HTTP click feature options:
  - `click.featuresEnabled`
  - `click.features.fftLength`
  - `click.features.lengthEnergyFraction`
  - `click.features.widthEnergyFraction`
  - `click.features.energyBandsHz`
  - `click.features.peakFrequencySearchHz`
  - `click.features.meanFrequencyRangeHz`
- Added Java reference exporter:
  - `reference-tools/java/src/org/pamguard/port/reference/ClickFeatureFixtureExporter.java`
- Added fixture script:
  - `reference-tools/scripts/generate-click-feature-fixture.ps1`
- Added C++ parity checker:
  - `cpp-engine/tools/click_feature_fixture_check.cpp`

## Validation

The C++ backend builds successfully with MSVC/Ninja.

CTest status after this checkpoint:

```text
18/18 tests passed
```

New parity test:

```text
click_feature_basic_parity
```

## Remaining click-classifier work

- Implement PAMGuard `BasicClickIdentifier` rule evaluation using these extracted features.
- Add click type parameter presets and config import/export for beaked whale and porpoise defaults.
- Add Java parity fixtures for classifier pass/fail decisions.
- Add sweep-classifier feature parity after the basic classifier is stable.
- Add production controls for whether HTTP responses include full spectra or compact summaries.
