# Whistles & Moans background spectra

Date: 2026-07-24

## Java path

`WhistleToneConnectProcess` observes the raw FFT block before spectrogram
noise reduction. One `Spectrogram.SpectrumBackground` per connector channel
maintains a decaying mean of magnitude-squared FFT bins.

The background smoother always uses a 10-second time constant. The separate
`WhistleToneParameters.backgroundInterval` controls how often the current
background is copied into a `SpecBackgroundDataUnit`.

## Port

The C++ implementation preserves the Java update rules:

- `dT = fftHop / sampleRate`;
- run-in length is Java integer truncation of `10 / dT`;
- before the run-in completes, the coefficient is `1 / nDone`;
- afterwards it is `dT / 10`;
- non-finite input bins are skipped without changing their stored value;
- raw, pre-noise-reduction FFT power feeds the smoother;
- exported bins are `sqrt(backgroundPower * 2 / fftLength)`.

`whistle.backgroundIntervalSeconds` is available in session JSON, the
OpenAPI contract, the browser contour dialog, session readback, and the
`.psfx` converter. The default is 10 seconds.

Result schema v32 adds `whistleBackgrounds`. Each item carries channel,
channel bitmap, snapshot time, covered start sample/duration, bin bounds, and
the full spectrum.

## Validation

The Java fixture drives the real pinned
`Spectrogram.SpectrumBackground` class for eight slices and four bins,
including NaN and positive-infinity inputs. The C++ checker matches all 32
stored values within `1e-14` (observed maximum error zero).

The session-level Whistles & Moans check also proves that:

- snapshots are emitted on the configured interval;
- both selected channels are emitted at the same snapshot time;
- each spectrum has the raw FFT data width;
- every exported value is finite and non-negative.

The HTTP service smoke proves the schema-v32 result member is always present,
and the OpenAPI, browser JavaScript, and example session documents pass their
syntax/contract checks.

## Timestamp boundary

Java initialises its emission clock from wall time in `prepareProcess`.
That is unsuitable for deterministic replay when an audio file carries
historical timestamps. The engine anchors the first interval to the first FFT
frame time instead. All subsequent interval comparisons and snapshot
timestamps follow Java's single-clock behavior.

This deliberate replay-stability difference affects the first snapshot
alignment only; it does not change the background spectrum maths.

## Remaining boundary

Ordinary audio-channel grouping is now implemented, so only the first channel
of each group owns a connector/background (`docs/234`). PAMGuard beamformer
sequence maps remain outside the engine's source model.
