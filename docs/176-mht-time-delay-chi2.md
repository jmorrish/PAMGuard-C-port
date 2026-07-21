# MHT Time Delay Chi2 Variable

Date: 2026-07-10

## Purpose

Ports `TimeDelayChi2Delta`, the MHT chi2 variable that scores consistency of multi-channel time delays along a candidate click train. This is the last MHT chi2 variable with tractable inputs; only `CorrelationChi2` remains.

## Reference semantics ported

The variable follows the delta pattern (score the change between successive per-pair difference vectors) with one distinctive rule in `calcDeltaChi2`:

```text
sum   = sum over pairs of (lastDelta[i] - newDelta[i])^2
chi2 += (sum - max(term)) / max(timeDiff * error, minError)^2
```

The largest per-pair squared term is **subtracted** before scaling, so a single badly correlated hydrophone pair cannot wreck a track. Defaults: error 1e-6 s, minError 1e-9 s.

The port takes the delay vector as an explicit argument (PAMGuard reads it from the data unit's localisation), and requires stable pair ordering across detections, as the reference does.

## Fixture

`MhtTimeDelayChi2FixtureExporter.java` drives the **real** `TimeDelayChi2Delta` with a minimal `AbstractLocalisation` subclass supplying per-pair delays. Five cases pin the behaviour, and the results make the drop-the-worst rule vivid:

| Case | Java chi2 | Behaviour pinned |
| --- | --- | --- |
| `constant-three-pairs` | 0 | Unchanging delays score zero. |
| `linear-drift` | 7.3e-27 | Steady drift on every pair scores zero (delta of deltas). |
| `one-bad-pair` | 0 | One wildly inconsistent pair is **absorbed entirely** by the subtraction. |
| `two-bad-pairs` | 8.2e7 | Only one bad pair can be dropped, so the second dominates. |
| `single-pair` | 0 | With one pair the subtraction removes the only term, so chi2 is always zero. |

`mht_time_delay_chi2_parity` reproduces all five within `1e-9` relative.

## Claim boundary

The variable is ported and pinned but not wired into the served MHT stack: doing so requires feeding per-click pair delays (available from the click localisation path) into the MHT units with stable pair ordering, which the session does not yet do. The `single-pair` result means the variable contributes nothing for two-hydrophone arrays — worth knowing before enabling it. `CorrelationChi2` remains unported; it needs PAMGuard's `CorrelationManager` waveform cross-correlation between click pairs.
