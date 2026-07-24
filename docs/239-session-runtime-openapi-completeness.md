# Session runtime OpenAPI completeness

Date: 2026-07-24

This checkpoint reconciles the session-status runtime counters with
`EngineSessionStatus`.

The OpenAPI schema now includes every counter emitted by `config_to_json`:
spectrogram frames; click, feature, classification, train, localisation, and
bearing counts; whistle peak/region counts; process calls; and cumulative/last
processing time.

This is contract documentation only. It does not change runtime accounting,
detector behavior, or result schema version.
