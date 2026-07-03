# Click Localisation Geometry Metadata

Date: 2026-07-01

## Purpose

Multi-channel click localisation consumers need to know the physical constraint used when estimating each pair delay. A delay value without the array-derived maximum can be hard to audit, especially for streamed hydrophone arrays and click tracking.

## Added fields

When click channels have matching hydrophone geometry and positive sound speed, each `clickLocalisations[].delays[]` item now includes:

- `audioChannelA`
- `audioChannelB`
- `geometryConstrained`
- `maxDelaySamples`
- `maxDelaySeconds`
- `hydrophoneDistanceM`

Each `clickTrainLocalisations[].pairDelays[]` item carries the same pair-level audio-channel and geometry fields when at least one contributing localisation was geometry-constrained.

`channelA` and `channelB` remain the pair's waveform indices for compatibility with the internal localisation path. `audioChannelA` and `audioChannelB` expose the actual audio/hydrophone channels used by API consumers.

The maximum delay is calculated from:

```text
ceil((hydrophoneDistanceM / speedOfSoundMps) * sampleRateHz) + 1
```

This mirrors the existing C++ delay-search limit and makes it visible to web/API consumers.

## Schema

This bumps the engine result `schemaVersion` to `4`.

## Validation

`click_train_localisation_summary_check` now asserts geometry metadata propagation into train summaries. `cpp-engine/scripts/service-smoke.ps1` asserts archived `click-localisation` events include geometry constraint metadata.
