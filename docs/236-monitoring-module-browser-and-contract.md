# Monitoring module browser and contract

Date: 2026-07-24

This checkpoint makes the existing non-click monitoring modules discoverable
and round-trippable without requiring operators to compose one large
top-level JSON object.

## Browser controls

The Noise & monitoring dialog now has eight focused panes:

- FFT noise
- filter-bank noise
- LTSA
- Ishmael energy sum
- Ishmael spectrogram correlation
- Ishmael matched filter
- PAMGuard matched-template classifier
- advanced top-level settings

FFT noise, filter-bank noise, and LTSA have structured controls for their
common settings. The larger scientific parameter sets use module-specific
JSON object editors with examples, an explicit enable switch, and object/array
type checks before session creation. The advanced pane remains available for
acquisition calibration and future top-level settings; structured panes take
precedence for their own module keys.

## HTTP/OpenAPI contract

The OpenAPI create-session schema now documents all seven module settings
objects plus acquisition calibration. The process-result schema documents
filter-bank levels, LTSA spectra, the three Ishmael detection arrays, and
matched-template classification results.

Session status now echoes each module using the same object shape accepted at
creation, including kernels, correlation segments, match/reject waveforms, and
calibration. `fftNoise.channelBitmap` is reconstructed from its resolved
channel list; the channel list remains as a diagnostic field.

## Validation

- Browser JavaScript parses successfully; all 246 IDs are unique and all 269
  literal element lookups resolve.
- The running service served all seven new module controls.
- OpenAPI YAML parses, all 51 schema references resolve, and no referenced
  schema is missing.
- The registered HTTP service smoke imports the real-Java `.psfx` fixture and
  asserts the acquisition, noise-band, LTSA, Ishmael, spectrogram-correlation,
  matched-filter, and matched-template settings in the session status.

## Claim boundary

This work changes UI and contract coverage only. The module maths and result
schema are unchanged. Large waveform/kernel settings remain JSON editors;
reproducing PAMGuard's Swing file choosers and display preferences is not a
web-port goal.
