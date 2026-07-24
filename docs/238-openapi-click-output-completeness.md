# OpenAPI click output completeness

Date: 2026-07-24

This checkpoint reconciles the click-result serializer with the documented
`EngineProcessResult` contract.

## Added response schemas

The OpenAPI result schema now describes four arrays the service already
emitted:

- `clickNoiseSamples`, including optional waveform matrices
- `clickTriggerBackground`, with the resolved long-filter values
- `clickTriggerFunction`, with per-channel signal-excess series
- `clickTrainClassifications`, with junk/species decisions and optional
  template correlation

## Validation

A static comparison of the top-level keys assigned by
`analysis_result_to_json` against `EngineProcessResult.properties` now reports
no serializer key missing from the schema. The OpenAPI YAML parses and all
component-schema references resolve.

## Claim boundary

This is contract documentation for existing output and changes neither
runtime behavior nor result schema version.
