# Streamer Geometry

Date: 2026-07-10

## Purpose

Completes the multi-streamer work begun in `docs/181`, which ported streamer-scoped *shape* semantics but left streamer identity caller-supplied and streamer *positions* unmodelled. Sessions can now describe arrays the way PAMGuard does: hydrophones positioned relative to a streamer, with the streamer carrying its own origin.

## Reference semantics ported

`PamArray.getAbsHydrophoneVector` computes a hydrophone's absolute position as its own coordinates **plus its streamer's coordinate vector** — a translation only. Notably it does **not** rotate by streamer heading, so the port does not either; `headingDegrees` is accepted and echoed as metadata rather than applied. PAMGuard's own source carries a `TODO` on that method about using times to select the correct hydrophone and streamer data units, so time-varying locators are not really applied there either, and the port matches that state rather than inventing behaviour.

## Implementation

Streamer offsets are resolved **once at session construction**, folding each streamer's origin into its hydrophones' coordinates. Every downstream consumer — delay-limit geometry, pair and LSQ bearings, array shape and directions — then sees absolute positions without needing streamer awareness, so the feature adds no branching to the localisation paths.

`array.streamers` declares them; `array.hydrophones[].streamerId` assigns hydrophones, and a `streamerId` that matches no declared streamer is rejected at session creation. The same field feeds the streamer-scoped duplicate-position detection from `docs/181`.

## Validation

`session_lsq_bearing_wiring` gains a case that expresses the same four-hydrophone array two ways — absolute coordinates, and streamer-relative coordinates with a streamer at the matching offset — and asserts both localise **identically** (azimuth and elevation within `1e-12`). That is the property that matters: a streamer-relative description must not change the answer. Full CTest suite passes `66/66`.

## Claim boundary

Translation only. Streamer heading, pitch, and roll are not applied, matching `getAbsHydrophoneVector`; PAMGuard applies orientation elsewhere through its hydrophone locators, which are unported. Streamer positions are static per session — PAMGuard's time-varying streamer data units (GPS-tracked towed arrays whose geometry changes through a tow) are not modelled, so a session describes one snapshot of the array.
