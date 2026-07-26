# Typed data model and module graph evidence

Date: 2026-07-24

Authority: PAMGuard Java `2.02.18e`,
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Claim

The composable runtime now implements the operator-visible semantics of
`PamDataUnit`, `PamDataBlock`, `PamProcess`, `PamControlledUnit`,
`PamModuleInfo`, dependencies, and configuration without copying Java
serialisation or Swing classes.

This is an architectural-semantics claim. Detector parity continues to be
claimed only where an existing Java fixture proves it.

## Implementation

- `DataUnitMetadata` carries type/schema, stable source block, UID/sequence,
  wall time, sample position/duration, channel/sequence maps, clock domain,
  and discontinuity.
- Data-block descriptors carry sample rate, frequency range, exact available
  channel map, clock domain, capabilities, and optional per-channel calibration
  offsets. Publication fills and enforces descriptor-owned metadata, and
  acquisition marks sample-position gaps as discontinuities.
- `DataBlock` has typed publication, bounded history, multiple subscribers,
  synchronous scientific delivery, queued drop-oldest presentation delivery,
  observer isolation, queue depth, maximum queue depth, and drop counters.
- Module descriptors expose typed ports, settings schema/defaults, instance
  limits, required dependencies, run modes, display types, implementation
  state, and parity state.
- Graph documents persist stable instance/block identities, connections,
  acquisition/clock/persistence policy objects, schema version, and optimistic
  revision.
- Validation rejects unknown types/ports, incompatible types/capabilities,
  multiplicity violations, missing required inputs, disabled sources, instance
  limit violations, cycles, unavailable channel selections, incompatible
  clock domains/sample rates, and incomplete calibration maps.
- Service graph replacement validates and preflights a candidate runtime,
  atomically replaces the persisted graph, and restores the prior graph,
  runtime, and file if any apply step fails.
- Both dry-run validation and persisted-startup restoration lifecycle-preflight
  executable settings; a topology-valid but unbuildable graph never becomes
  authoritative.
- Lifecycle covers prepare, start, stop, flush, reset, disconnect, and destroy.
  Starts roll back partially started nodes, and teardown runs consumers before
  producers while still attempting every stop after an error.

## Evidence

- `module_graph_and_typed_data_blocks` covers graph validation, compatible
  source discovery, stable JSON round trip, fan-out, bounded ordering,
  reconnect/unsubscribe, type rejection, slow-subscriber shedding, observer
  errors, queued self-unsubscribe lifetime, unordered-document topological
  construction, executable graph construction, clock-domain rejection,
  discontinuity detection, channel-leakage prevention, FFT
  channel/frequency metadata, and calibrated acquisition/amplifier lineage.
- `module_runtime_http_smoke` covers persisted graph apply, stale-revision
  rejection, invalid executable graph rollback, stable block IDs, lifecycle
  status/control, process restart, ingest, history, streaming, executable
  validation, and graph revision.
- `/module-types`, `/module-graph`, `/module-graph/validate`,
  `/module-graph/compatible-sources`, `/module-runtime/status`, and
  `/data-blocks` expose the same state inspected by the browser graph editor.

## Claim boundary

The registry is compiled in. A public binary plugin ABI remains intentionally
deferred until the internal contracts have settled. The legacy fixed-session
API still exists as a compatibility/reference path, but it is no longer the
primary browser acquisition or operator-workspace path.
