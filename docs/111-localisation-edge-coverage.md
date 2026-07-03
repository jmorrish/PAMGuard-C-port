# Localisation Edge Coverage

Date: 2026-07-01

This checkpoint expands focused localisation foundation checks.

## Added cases

- delay group estimator returns no pairs for a single channel;
- delay group estimator rejects mismatched `max_delay_samples`;
- far-field bearing localiser keeps a usable one-pair foundation estimate when partial geometry remains;
- far-field bearing localiser returns invalid output when no configured hydrophone pairs remain usable;
- one-pair bearing foundation behaviour is checked;
- invalid far-field bearing config is rejected.

## Tests

- `delay_group_3ch_basic_parity`
- `far_field_bearing_foundation`

These remain foundation checks, not full PAMGuard array-localiser equivalence.
