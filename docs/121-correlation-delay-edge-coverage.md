# Correlation delay edge coverage

Date: 2026-07-01

## What changed

The `correlation_delay_fixture_check` executable now keeps the existing PAMGuard fixture parity check and also covers edge behavior around the correlation delay estimator.

Added checks:

- `fft_length == 0` is rejected.
- Negative `max_delay_samples` is rejected.
- Silent inputs return zero delay and zero score.
- `max_delay_samples == 0` clamps the reported delay to zero.
- Identical non-silent signals produce a positive zero-delay correlation.

The estimator now returns zero delay and zero score when no correlation peak is found, avoiding a misleading search-window-boundary delay on silent input.

## Validation

`correlation_delay_basic_parity` passed after the no-peak guard was added, preserving the existing PAMGuard fixture match for the non-silent reference case.

## Why this matters

Click localisation depends on stable pairwise time-delay estimates. These edge checks reduce the risk that later optimisation or refactoring changes the basic semantics around invalid configs, silent audio, or delay search limits.
