# Pair Bearing Service Output

Date: 2026-07-03

## Purpose

`docs/157` ported PAMGuard's `PairBearingLocaliser` maths into the engine with fixture parity but did not expose it. This slice wires it into the click localisation pipeline and the HTTP/archive result contract so API consumers receive PAMGuard pair bearing angles alongside each geometry-constrained delay pair.

## Behaviour

For every geometry-constrained click localisation delay pair (hydrophone positions known for both channels, positive spacing, known sample rate), the session now runs the ported `PairBearingLocaliser` with:

- `spacing` = the pair's hydrophone distance,
- the measured pair delay converted to seconds,
- array-level error terms from new session config fields.

Each `clickLocalisations[].delays[]` item then carries:

- `pairBearingRadians` — angle from the pair axis (PAMGuard convention, `acos` of the clamped normalised delay).
- `pairBearingDegrees` — the same angle in degrees.
- `pairBearingErrorRadians` — PAMGuard's error estimate; omitted when the reference formula yields a non-finite value (for example `Infinity` at the endfire clamp).

The fields are omitted entirely for unconstrained delay pairs.

## Config

`array` session config gains four optional non-negative fields feeding the error estimate (all default `0.0`):

- `speedOfSoundErrorMps`
- `timingErrorSeconds`
- `spacingErrorM`
- `wobbleRadians`

The session config echo and OpenAPI document them; invalid (negative or non-finite) values are rejected at session creation.

## Schema

This bumps the engine result `schemaVersion` to `5`. Existing fields are unchanged; version 5 is purely additive.

## Validation

- `cpp-engine/scripts/service-smoke.ps1` asserts schema version 5 on live results, health, and archived records, and asserts archived `click-localisation` delay events include `pairBearingRadians`/`pairBearingDegrees`.
- Full CTest suite passes `50/50` including all three service smokes and the load smoke.

## Claim boundary

Pair bearing output is per delay pair; it is not yet aggregated into train-level summaries, and the ported LSQ bearing localiser (`docs/158`) is engine-only — bearing localiser selection for four-plus hydrophone sessions is a follow-up. Delay sign convention: the angle is measured from the pair axis oriented channel A to channel B, using the engine's correlation delay sign.
