# Basic Click Classifier

Date: 2026-06-30

This checkpoint adds the first classifier layer above click detection and click feature extraction.

## Implemented

- Added C++ `BasicClickClassifier`.
- Cloned PAMGuard `BasicClickIdentifier` decision flow:
  - configured click types are checked in list order;
  - the first matching type wins;
  - unmatched clicks return click type `0`;
  - `discard` is carried with the matched click type;
  - rule groups are enabled by the same bitmask values as PAMGuard:
    - `0x1` energy bands;
    - `0x2` peak frequency width;
    - `0x4` peak frequency position;
    - `0x8` mean frequency;
    - `0x10` click length.
- Implemented checks for:
  - two in-band energy windows;
  - minimum band-energy difference;
  - peak-frequency search/range;
  - peak-width range;
  - mean-frequency range;
  - click-length range in milliseconds.
- Added session-level classification results.
- Exposed classifications through `pamguard_engine_service` JSON as `clickClassifications`.
- Added HTTP config support under:
  - `click.basicClassifier.enabled`
  - `click.basicClassifier.types`

## Validation

The C++ backend builds successfully with MSVC/Ninja.

CTest status after this checkpoint:

```text
19/19 tests passed
```

New parity test:

```text
click_basic_classifier_parity
```

HTTP smoke test:

```json
{"inputFrames":256,"clicks":1,"clickFeatures":1,"clickClassifications":1,"clickLocalisations":1}
```

## Remaining click-classifier work

- Add import/export helpers for PAMGuard's standard beaked whale and porpoise presets.
- Optimize classifier evaluation to reuse cached spectra/length calculations across many click types.
- Add sweep classifier parity.
- Add click train/event grouping parity.
- Add classifier UI controls and persistence in the web platform layer.
