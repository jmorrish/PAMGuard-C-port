# Bearing Localiser Selection by Array Shape

Date: 2026-07-21

## Purpose

Replaces the engine's channel-count-based localiser choice with PAMGuard's actual rule, which is based on the **shape** of the sub-array taking part in a localisation. `docs/158` and `docs/180` both flagged this as a known divergence: the engine used the pair localiser for two channels and LSQ for four or more, which is not a rule PAMGuard has anywhere.

## Reference semantics ported

`BearingLocaliserSelector.createBearingLocaliser` is a single switch on `ArrayManager.getArrayType(phoneMap)`:

| Sub-array shape | PAMGuard localiser |
| --- | --- |
| None, Point | `null` — no bearing at all |
| Line | `PairBearingLocaliser` |
| Plane | `MLGridBearingLocaliser2` |
| Volume | `MLGridBearingLocaliser2` |

Three things about that switch are worth stating because they are easy to get wrong from the outside:

- **It is called on a sub-array, not the array.** The argument is a channel map of the hydrophones in one localisation, so a session whose full array is volumetric can still take the line branch for a click detected on two channels. The port therefore computes the shape from the participating channels each time, not once at session construction.
- **Channel count does not appear.** Four hydrophones in a tow line are a `Line`, and PAMGuard gives them pair bearings. The engine used to attempt an LSQ solve there. Those solves were rank deficient — every baseline is a multiple of the same vector — so they already returned no bearing; the change makes the refusal deliberate and visible instead of incidental.
- **A point selects nothing.** Co-located hydrophones on one streamer collapse to a point under `getSpatiallyUniquePhones`, and PAMGuard returns `null` rather than a degenerate bearing.

There is one branch the port does **not** reproduce: a line array of more than two hydrophones uses `MLLineBearingLocaliser2` when `SMRUEnable.isEnable()`. That gates SMRU-licensed extras absent from the open distribution, and the default build takes the pair branch. The port always takes the pair branch and says so in the header.

## Implementation

`localisation::select_bearing_localiser` is the switch, over the already-ported `array_shape` (`docs/163`, which includes streamer-scoped uniqueness). It returns `None`, `Pair`, or `Grid` — deliberately naming the localiser *class* PAMGuard picks rather than the one the engine runs, so the substitution stays visible rather than being buried in a rename.

`AnalysisSession` computes the sub-array shape for each click's channels and each whistle group's channels, records both the shape and the choice on the result, and gates LSQ output on `Grid`. The existing LSQ preconditions (four or more hydrophones, full geometry, positive `spacingErrorM`) still apply on top; the selector decides whether a grid-class localiser is appropriate at all, and those preconditions decide whether the engine's particular substitute can run.

## The substitution, stated plainly

PAMGuard selects `MLGridBearingLocaliser2` for planes and volumes. That localiser is **not ported** — it is a maximum-likelihood grid search, a different algorithm from anything the engine has. For `Grid`, the engine runs its ported `LSQBearingLocaliser` instead.

That is a substitution, not parity. It is a defensible one: `BearingLocaliserSelector`'s own source carries `LSQBearingLocaliser` as a commented-out alternative on both the plane and volume branches, so PAMGuard treats them as interchangeable enough to have shipped one and kept the other in view. But a plane or volume bearing from this engine is an LSQ bearing, and it will not agree with PAMGuard's grid search to fixture tolerance. The `bearingLocaliser` field says `"grid"` because that is what PAMGuard *selects*; the value it reports comes from LSQ.

## API output

Schema v16 adds `arrayShape` (`none`/`point`/`line`/`plane`/`volume`) and `bearingLocaliser` (`none`/`pair`/`grid`) to every click localisation and whistle delay entry. These answer a question the API previously could not: *why is there no bearing here?* A `point` shape and a `none` localiser is a complete answer; so is a `line` shape next to an absent `lsqBearing`.

**Behaviour change**: a line sub-array of four or more hydrophones no longer emits `lsqBearing`. As above, those entries carried no valid bearing before either, since the solve was rank deficient — what disappears is an object that was already reporting failure.

## Validation

`bearing_localiser_selection` (new) covers all five shapes through the enum switch and again through hydrophone positions: empty, single, separated pair, co-located pair, four-in-a-line, three-hydrophone plane, non-coplanar tetrahedron, and a co-located pair on **different streamers** (a line, not a point, since streamer-scoped uniqueness keeps both).

The four-in-a-line and co-located-pair cases are the ones that would have passed under the old channel-count rule and now do not.

The HTTP smoke asserts the two-hydrophone smoke session reports `arrayShape: "line"` and `bearingLocaliser: "pair"`. Full CTest suite passes `68/68`.

## Claim boundary

The **selection** is ported; one of the selected localisers is not. `Grid` runs LSQ, as above. `MLGridBearingLocaliser2`, `MLLineBearingLocaliser2`, `SimplexBearingLocaliser`, and `CombinedBearingLocaliser` are all unported, so a plane or volume sub-array gets an LSQ bearing where PAMGuard would run a grid search, and an SMRU-enabled PAMGuard would differ from this engine on a multi-hydrophone line.

Shape is computed from static session geometry. An array whose shape changes through a tow — hydrophones that spread from a line into a plane — is not modelled, for the same reason `docs/193` gives: no time-varying locator or sensor feed.
