# Multi-Streamer Arrays

Date: 2026-07-10

## Purpose

Closes the multi-streamer array item. `docs/163` ported `ArrayManager`'s shape and direction semantics but assumed a single streamer, so two hydrophones at the same position always collapsed into one. PAMGuard scopes that uniqueness test by streamer.

## Reference semantics ported

`ArrayManager.getSpatiallyUniquePhones` treats two phones as duplicates only when their positions match **and** they belong to the same streamer:

```java
if (phoneVec.equals(phoneVectors[j]) & jStreamer == iStreamer) { ... }
```

`getArrayShape` applies the same streamer-scoped test again in its own inner dedup loop, so co-located phones on different streamers must survive **both** filters. The port threads an optional `streamer_ids` vector through `array_shape` and `array_directions` (an empty vector keeps the previous single-streamer behaviour) and applies the streamer check in both places.

## Fixture

Three cases appended to the array shape fixture (existing rows unchanged — a three-line insertion):

| Case | Shape | Pins |
| --- | --- | --- |
| `streamers-colocated-pair` | Plane | Four phones at two positions across two streamers stay four distinct phones. |
| `streamers-colocated-pair-single-streamer` | Line | The identical positions on **one** streamer collapse to two phones. |
| `streamers-two-towed-lines` | Plane | Two parallel towed streamers form a plane. |

The first two share identical coordinates and differ only in streamer assignment, so they isolate the semantics precisely. `array_shape_basic_parity` reproduces all of them.

## Claim boundary

Streamer identity is supplied by the caller; PAMGuard derives it from the array configuration's streamer list, and streamer-level geometry (independent streamer positions, headings, and their time-varying locators) is not ported. The session's array config does not yet expose a streamer field, so served sessions still behave as single-streamer; this slice makes the underlying shape/direction maths correct for when it does.
