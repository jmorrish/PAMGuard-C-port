# Per-Chunk Attitude

Date: 2026-07-22

## Purpose

Narrows the last localisation ledger item to its true remainder. "Time-varying orientation needs a live sensor feed" was half right: sub-chunk interpolation and geographic position do. But attitude **at chunk cadence** needs nothing the ingest path doesn't already have — a chunk is the engine's ingest granularity, and whoever posts the chunk can post the heading they measured with it.

This lets a session's array attitude change over time, chunk by chunk, with geometry itself staying static.

## Why this is cheap and correct

Array orientation touches exactly one computation: the earth-frame rotation of `docs/202`. It does not change relative hydrophone geometry, so the ML grid's delay table, the LSQ fit, the pair bearings, the delays, and every array-frame output are all orientation-independent. A per-chunk attitude therefore means re-rotating a handful of vectors per result — no table rebuilds, no session state beyond three angles.

That is also why the genuinely blocked remainder is what it is: time-varying **geometry** (a towed array deforming) would rebuild the grid table and reload every localiser, and PAMGuard handles that through `resetArray` and its GPS-driven locators. That still needs a feed and an architecture.

## Semantics

`POST /sessions/{id}/pcm-f32le` accepts optional `headingDegrees`, `pitchDegrees`, and `rollDegrees` query parameters:

- **All three travel together.** A partial declaration is rejected, so a new heading can never silently combine with a stale pitch or roll.
- **A declaration holds until replaced.** A source that measures attitude every tenth chunk declares it every tenth chunk; the chunks between keep the last attitude, which is what "the most recent measurement" means. It does not decay back to the session's static orientation.
- **With no declarations ever, nothing changes** — the session's static `array.orientation` (or its absence) governs, exactly as at `docs/202`.

Angles use the same conventions as everywhere else in the port: PAMGuard's `PamQuaternion` construction, heading clockwise (`docs/193`).

No result fields change and existing requests behave identically, so the result schema stays at v21 — this is a request-side addition.

## Validation

`session_lsq_bearing_wiring` gains a case posting a chunk that declares 42°/-7°/11° into a session with **no** static orientation: earth vectors appear, and they match the config-declared path's output to `1e-12` — the same rotation reached through the other door. A follow-up chunk with no declaration still produces earth vectors, pinning the holds-until-replaced rule.

The HTTP smoke asserts the plain chunks produce no earth-frame vectors (the smoke session declares no static orientation), then posts a chunk with the three parameters and asserts the pair bearing gains its two earth-frame vectors. The archive record count assertion moved from three to four to account for the extra chunk.

Full CTest suite passes `72/72`.

## Claim boundary

Attitude is piecewise-constant at chunk boundaries. PAMGuard interpolates GPS attitude to each data unit's timestamp; a detection near a chunk boundary during a fast turn will carry the chunk's attitude, not an interpolated one. For typical chunk lengths (seconds) and vessel dynamics that difference is small, but it is a difference, and a deployment that needs better should send shorter chunks or wait for a real feed.

Still unmodelled, and now the *complete* list of what actually needs external input:

- **Time-varying geometry** — array deformation through a tow; rebuilds localiser state, needs a position feed.
- **Geographic position** — `LatLong` and everything built on it; needs GPS.
- **Sub-chunk attitude interpolation** — needs timestamped attitude data, i.e. the same feed.
- **`.psfx` compatibility with other PAMGuard builds** — needs a file written by such a build (`docs/203`).
