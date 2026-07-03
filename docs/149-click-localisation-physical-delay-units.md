# Click Localisation Physical Delay Units

Date: 2026-07-01

## Purpose

Multi-channel consumers need localisation delays in engineering units, not only samples. This checkpoint keeps the existing PAMGuard-style delay-sample fields and adds derived physical fields in service JSON.

This introduced engine result `schemaVersion` `3`; later schema versions retain these fields.

## Added fields

Each `clickLocalisations[].delays[]` item now includes, when sample rate is known:

- `delaySeconds`
- `pathDifferenceM`, when sound speed is configured and positive

Each `clickTrainLocalisations[].pairDelays[]` item now includes:

- `meanDelaySeconds`
- `meanPathDifferenceM`, when sound speed is configured and positive

The calculation is:

```text
delaySeconds = delaySamples / sampleRateHz
pathDifferenceM = delaySeconds * speedOfSoundMps
```

## Archive projection

Archived detector events for `click-localisation` and `click-track-localisation` carry the same fields because the archive writer uses the same result serialization path.

## Validation

`cpp-engine/scripts/service-smoke.ps1` now asserts that archived click-localisation delay events include both `delaySeconds` and `pathDifferenceM`.
