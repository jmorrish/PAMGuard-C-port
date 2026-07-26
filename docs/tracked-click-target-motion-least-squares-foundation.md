# Tracked-click target-motion Least Squares foundation

## Authority and scope

The authority is PAMGuard 2.02.18e at commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

The traced runtime path is:

1. `clickDetector.TrackedClickLocaliser.localiseGroup`
2. `clickDetector.localisation.GeneralGroupLocaliser.runModel`
3. `Localiser.detectionGroupLocaliser.DetectionGroupLocaliser2`
4. bearing-group `Chi2Bearings`
5. `Localiser.algorithms.genericLocaliser.leastSquares.LeastSquares`
6. `Stats.LinFit`

`TrackedTargetMotionLeastSquares` ports the pure two-dimensional numerical
algorithm, the Java point-limit selection, the raw/reduced chi-square and AIC
presentation values, `SimpleError`'s perpendicular/parallel errors, and the two
post-fit limit stages.

The input is deliberately the data that `LeastSquares` actually consumes after
`TMGroupLocInfo` has separated ambiguities: one local Cartesian origin, array
heading, and resolved world bearing per click and per ambiguity side. Bearing
angle-error vectors are not part of this API because PAMGuard's Least Squares
implementation ignores them and uses its own fixed
`3 degrees / sin(bearing offset)^2` error model.

## Headless Java boundary

A real `TMGroupLocInfo` cannot be constructed as a deterministic headless
numerical fixture. Its constructor and lazy accessors require live
`PamController`, `ArrayManager`, `PamArray`, `PamDataUnit` localisation,
`GPSDataBlock`, and time-dependent hydrophone state.

The fixture therefore does not invent a replacement group object. It:

- constructs real pinned-PAMGuard `PamVector`, `PamQuaternion`,
  `Chi2Bearings`, `LeastSquares`, `LinFit`, and `SimpleError` objects;
- records their actual result, error, and chi-square values;
- exposes already-resolved ambiguity sides and their per-click inputs;
- extracts `TMGroupLocInfo.copySubDetections` point selection literally,
  retaining Java `float` and `Math.round(float)` behaviour; and
- extracts the two simple post-fit comparisons literally while treating
  closest-GPS-path perpendicular distance and georeferenced height as explicit
  adapter inputs.

This also captures a pinned-Java edge case: perfectly parallel bearings return
algorithm success with infinite x/y and NaN z. The later range filters reject
that result. The C++ numerical layer preserves this instead of silently
changing the scientific behaviour.

## Fixture coverage

`tracked-target-motion.csv` contains 13 deterministic cases covering:

- moving x/y/z origins;
- constant and changing per-click headings;
- exact and perturbed per-click bearings;
- both port/starboard ambiguity sides;
- raw and reduced chi-square, AIC, perpendicular/parallel errors, and error
  direction;
- unlimited and four-point endpoint-preserving selection plus one- and
  zero-point limits;
- inclusive configured range/height boundaries, configured rejection, and the
  `2*pi*Earth radius` runaway rejection;
- divergent-bearing, singleton, and zero-selected-point Java failures; and
- the parallel-bearing non-finite Java-success edge case.

Regenerate with:

```powershell
.\reference-tools\scripts\generate-tracked-target-motion-fixture.ps1
```

## Runtime and service integration

The numerical foundation is now wired through the project-authoritative click
path:

1. Project PCM ingest accepts heading, pitch, and roll only as a complete
   finite triplet, and east, north, and height only as a complete finite
   origin. The stable Acquisition controlled-unit ID is the navigation
   reference.
2. The click trigger snapshots the latest declared pose at trigger onset
   rather than using whichever pose happens to be current when a later chunk
   completes the click.
3. Per-click localisation applies that pose, retains the local origin and
   reference, and publishes every earth-frame bearing ambiguity in
   deterministic grid, Least Squares, then pair-bearing priority.
4. Manual tracked-event observations preserve click order, origin, heading,
   navigation reference, and ordered ambiguities.
5. The localisation action selects the event's bounded navigation-track
   window, separates ambiguity sides, applies Java's point limit, runs the
   ported Least Squares algorithm, finds the navigation-beam sample, and
   returns the range/height filter decisions with each fit.
6. Events lacking two compatible observations, a common navigation reference,
   complete pose/origin data, or bearing ambiguities remain explicitly
   unavailable.

`project_tracked_click_localisation_http_smoke` exercises that complete service
boundary. It builds a project with Acquisition and Click Detector, injects five
posed PCM chunks around a known target, verifies trigger-onset pose and ordered
ambiguities on the retained clicks, assigns them to one event, and requires an
accepted Least Squares solution within 50 m of the target together with beam
and filter evidence.

The API deliberately uses a caller-supplied local Cartesian metre frame. It
does not pretend that east/north values are geodetic `LatLong`, and it does not
create a GPS feed. Live GPS/attitude capture, sub-chunk interpolation,
time-varying towed-array deformation, and the alternative simplex/MCMC
target-motion algorithms remain outside this slice.

## Build and fixture integration

The tracked-target-motion source, fixture checker, Java exporter, and fixture
generator are registered in the normal build and aggregate fixture workflow.
The focused permanent checks are:

- `tracked_target_motion_java_fixture`;
- `tracked_click_events_java_semantics`;
- `click_localiser_platform_pose`; and
- `project_tracked_click_localisation_http_smoke`.
