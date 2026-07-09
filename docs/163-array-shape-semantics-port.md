# Array Shape Semantics Port

Date: 2026-07-06

## Purpose

The last open bearing-convention question was PAMGuard's `ArrayManager` array-shape handling: `PairBearingLocaliser.prepare()` negates the pair spacing when the pair vector aligns with the pair's principal array axis, changing the bearing's reference direction. This slice ports `ArrayManager.getArrayShape` and `getArrayDirections`, pins them with a Java fixture driving the real `pamMaths.PamVector` maths, and wires the spacing sign flip into the session pair-bearing path.

## Reference semantics ported

`cpp-engine/src/localisation/ArrayShape.cpp` implements, faithfully to `Array/ArrayManager.java`:

- The spatially-unique phone filter (exact position equality, last duplicate wins).
- Shape classification: point / line (2 phones, or all pair vectors pairwise in-line within PAMGuard's 1/1000-radian parallel tolerance and difference checks) / plane (3 phones, or zero max volume) / volume.
- `getMaxVolume` taking the maximum of **signed** triple products, exactly as the Java does.
- Principal directions: line arrays aligned to the nearest positive Cartesian axis; planar arrays via the closest-pair-to-y/x/z selection and plane-perpendicular cross product (including the loop that overwrites the perpendicular across outer iterations); volumetric arrays return the Cartesian axes.
- `PamVector.angle` uses `acos` of unit dot products without clamping, as in Java.

## Fixture

`ArrayShapeFixtureExporter.java` transcribes the two `ArrayManager` methods (the singleton is `PamController`-coupled) while every vector operation is the real `PamVector` class. Nine cases: duplicate-position point, two-phone diagonal, y and negative-x lines, xy/rect/tilted planes, tetrahedron, and a five-phone towed cluster (genuinely volumetric under PAMGuard's exact tests). `array_shape_fixture_check` mirrors the catalogue and compares shapes exactly and direction vectors within `1e-12`.

## Pair bearing wiring (schema v8)

`attach_pair_geometry` now computes the pair's own array directions (PAMGuard passes just the pair's two phones) and negates the spacing when the baseline's dot product with the principal axis is positive — the exact `prepare()` rule. Pair bearing **values** therefore flip for axis-aligned pairs relative to schema v5-v7 outputs; field shapes are unchanged. Result `schemaVersion` bumps to `8` and `docs/162` gains a pointer to this doc.

## Validation

- `array_shape_basic_parity` passes across all nine Java-generated cases.
- Full CTest suite passes `53/53` including all service smokes at schema version 8.

## Claim boundary

The port covers single-streamer arrays (position-equality uniqueness; PAMGuard also separates phones by streamer). `getArrayDirections` for the *whole* array is available but the session currently applies the pair-level rule only, exactly matching `PairBearingLocaliser.prepare()`. LSQ bearing inputs are unaffected (it uses signed baseline vectors directly).
