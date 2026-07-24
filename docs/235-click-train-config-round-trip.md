# Click train configuration round-trip

Date: 2026-07-24

This checkpoint makes the session-status representation of Click Trains
symmetrical with the create-session contract.

## Implemented

`GET /sessions` and the individual session status now return a nested
`click.train` object containing:

- ICI/MHT algorithm selection and the simple train limits
- every resolved MHT variable, penalty, exponent, and kernel limit
- the classifier-chain enable state
- resolved pre-classifier, IDI, bearing, and spectrum-template settings

Bearing values are converted back from the radians used internally to the
degree-valued HTTP contract. Template presets are returned as their resolved
spectrum and sample rate, so the returned object can be posted again without
depending on a preset-name lookup.

The flattened train summary fields and `click.trainMht` remain as diagnostic
aliases. New code should use `click.train`.

## Validation

The registered HTTP service smoke imports the real-Java `.psfx` fixture and
asserts the nested MHT kernel, classifier chain, IDI species, template
threshold/species, and resolved template spectrum. This closes the
create/import/status loop on the configuration shape used by the browser.

## Claim boundary

This is contract serialization and smoke coverage. It changes no train
formation or classification maths and therefore does not change the result
schema version.
