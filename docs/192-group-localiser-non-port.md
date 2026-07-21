# Group Localiser: Deliberate Non-Port

Date: 2026-07-10

## Decision

`DetectionGroupLocaliser` and `GroupDetection` will **not** be ported in their current form. This is a decision, not an omission, and this note records the evidence so it is not repeatedly re-litigated.

## Evidence

**PAMGuard marks it deprecated.** `DetectionGroupLocaliser` carries a class-level `@Deprecated` annotation (line 18), `GroupDetection` has a deprecated member, and the implementation contains its own verdict inline:

> "No simple rule that I can see - in any case this is deprecated. Need to switch whistle and moan paired localiser over to a new system in any case."

Porting it would mean reproducing an implementation the reference itself has disowned, and pinning parity against behaviour PAMGuard intends to replace.

**It needs a model the engine does not have.** The algorithm triangulates bearings taken from *different platform positions* as a vessel moves along a track: it references `LatLong` origins per sub-detection in eighteen places, converting each to metres relative to a reference detection before the least-squares fit. The engine has no GPS, vessel-track, or geo-referencing model at all — sessions describe a static array in local coordinates. Adding one purely to serve a deprecated localiser would be a large, load-bearing change justified by nothing else in the programme.

**It solves a different problem than whistle groups pose here.** The engine's whistle groups (`docs/191`) associate contours seen *simultaneously* on different channel groups of one array. `DetectionGroupLocaliser` assumes sub-detections spread along a track over time. Triangulating between spatially separated sub-arrays at one instant is a related but distinct problem, and the ported LSQ and pair bearing localisers (`docs/157`, `docs/158`) already cover the instantaneous multi-element case with fixture parity.

## What this means for the ledger

Whistle group **localisation** stays a Gap, with the reason recorded rather than a task queued. If group localisation is wanted later, the sound route is to target PAMGuard's current localiser chain (the "new system" the source comment points at) against a pinned PAMGuard version, together with whatever geo-referencing model the platform decides to adopt — not to port the deprecated path.

Whistle group **association** is ported, served, and cross-chunk capable (`docs/189`, `docs/191`); only localisation of a formed group is out of scope.
