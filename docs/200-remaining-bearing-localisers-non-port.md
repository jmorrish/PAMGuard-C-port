# Remaining Bearing Localisers: A Deliberate Non-Port

Date: 2026-07-21

## Purpose

With `PairBearingLocaliser` (`docs/157`), `LSQBearingLocaliser` (`docs/158`), `MLGridBearingLocaliser2` (`docs/195`), `MLLineBearingLocaliser2` (`docs/199`), and the selector itself (`docs/194`) ported, three classes remain in `Localiser.algorithms.timeDelayLocalisers.bearingLoc`. This records why none of them is being ported, with the evidence, so the decision is auditable rather than an omission someone re-discovers later.

## The evidence

A search of the entire PAMGuard source tree for live instantiations of each:

| Class | Live `new` sites | Notes |
| --- | --- | --- |
| `SimplexBearingLocaliser` | **0** | Class-level `@Deprecated`. Its only constructions are inside `CombinedBearingLocaliser`, which is itself never constructed. |
| `CombinedBearingLocaliser` | **0** | Both call sites are commented out, in `BearingLocaliserSelector`. |
| `MLGridBearingLocaliser` (v1) | **0** | Both references are commented out — one in `BearingLocaliserSelector`, one in `ArrayDialog`. Superseded by `MLGridBearingLocaliser2`. |

Every reference outside the classes' own files is a commented-out line:

```java
//			return new CombinedBearingLocaliser(new MLGridBearingLocaliser(hydrophoneMap, -1, timingError),
//			return new SimplexBearingLocaliser(hydrophoneMap, -1, timingError);
//			return new CombinedBearingLocaliser(hydrophoneMap, -1, timingError);
```

There are no class-name string references either, so nothing reaches them reflectively or through settings. The cluster is unreachable in the shipped application.

## Why that settles it

Porting them would produce code the engine can never select, validated against a reference path PAMGuard itself never executes. It would grow the parity surface without moving parity: no output would change, no fixture would cover a served path, and every future refactor would carry three more files.

The distinction from `MLLineBearingLocaliser2` (`docs/199`) is worth being precise about, since that one *was* ported despite also not being selected. That class **is** reachable in PAMGuard — under an SMRU licence flag — so a real deployment can produce its numbers and someone may need to reproduce them. These three are reachable under no configuration at all.

`SimplexBearingLocaliser` carries an additional signal: a class-level `@Deprecated`. That is PAMGuard's own statement about it, matching the `DetectionGroupLocaliser` situation recorded in `docs/192`.

## What would change the decision

Any of these would make it worth revisiting:

- A PAMGuard release that uncomments one of those selector branches.
- A user needing to read archived results produced by an older PAMGuard build that did select one.
- A requirement for simplex refinement of grid bearings specifically — `CombinedBearingLocaliser` runs a first localiser, seeds the simplex from its answer, and refines. That is a real technique, and if the engine ever wants it, this document is where to start rather than a blank page.

## Coverage statement

The bearing localiser package is now fully accounted for:

| File | Status |
| --- | --- |
| `BearingLocaliser.java` | Interface — expressed as the C++ localiser types |
| `BearingLocaliserSelector.java` | Ported (`docs/194`) |
| `PairBearingLocaliser.java` | Ported (`docs/157`) |
| `LSQBearingLocaliser.java` | Ported (`docs/158`) |
| `MLGridBearingLocaliser2.java` | Ported (`docs/195`, served `docs/196`) |
| `MLLineBearingLocaliser2.java` | Ported as an unselected variant (`docs/199`) |
| `SimplexBearingLocaliser.java` | **Non-port** — this document |
| `CombinedBearingLocaliser.java` | **Non-port** — this document |
| `MLGridBearingLocaliser.java` (v1) | **Non-port** — this document |
| `AbstractLocaliser.java` | Base plumbing, no maths to port |
| `OldAngleConverter.java` | Not ported: converts angles in **archived binary click files** written by pre-2017 PAMGuard (`ClickBinaryDataSource` calls it on read). The engine has no PAMGuard binary-file reader, so there is nothing for it to convert. |
| `DelayOptionsDialog/PaneFX/Panel.java` | Swing/JavaFX GUI |

## Claim boundary

This document is a decision record, not a port. It adds no code and changes no output.

The `OldAngleConverter` line above is a genuine gap rather than a dismissal: if reading PAMGuard's binary click files is ever in scope, that converter is part of doing it correctly for older data, and it is not ported.
