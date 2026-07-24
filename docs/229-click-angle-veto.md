# Click detector angle vetoes

Date: 2026-07-24

Authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

PAMGuard stores detector vetoes in the separate `AngleVetoParameters`
settings object. The default is an empty list. Each entry has a channel bitmap,
start angle, and end angle, but the Java implementation explicitly does not
use the channel bitmap.

## Exact behavior

For every completed click Java:

1. classifies it;
2. measures channel-pair delays and calculates localisation;
3. calls the legacy `ClickDetection.getAngle()`;
4. rejects the click if its absolute angle lies inside any configured range,
   including either endpoint.

The legacy angle is not the modern grid-bearing result. It is:

`degrees(acos(clamp(firstPairDelaySamples / maximumPairDelaySamples, -1, 1)))`

When no bearing exists, `getAngle()` returns zero. The C++ pipeline preserves
this behavior, including performing the required delay measurement internally
when localisation output is disabled. Rejected clicks are removed before
features, matched-template annotations, click-train state, and downstream
localisation summaries.

## Validation

`AngleVetoFixtureExporter` executes the real Java `AngleVetoes` class.
`click_angle_veto_parity` checks eleven cases covering the empty default,
inclusive endpoints, values on either side, absolute negative angles,
multiple vetoes, reversed bounds, and ignored channels.

`session_manager_instanced_pipeline` additionally proves that a 90-degree
synthetic click rejected by an 80–100 degree veto produces no click,
localisation, feature, or click-train output.

## Configuration

```json
{
  "click": {
    "angleVetoes": [
      {
        "channels": 0,
        "startAngleDegrees": 35,
        "endAngleDegrees": 55
      }
    ]
  }
}
```

The API, session readback, browser control, and `.psfx` converter all use this
same representation.
