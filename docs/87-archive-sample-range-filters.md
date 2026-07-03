# Archive sample range filters

`GET /sessions/{sessionId}/archive` now supports inclusive sample-position filters:

```text
startSampleFrom
startSampleTo
```

Examples:

```text
GET /sessions/station-001/archive?startSampleFrom=48000
GET /sessions/station-001/archive?startSampleFrom=48000&startSampleTo=96000&limit=100
```

Filtering is applied before positive-limit tail selection, so `limit=100` returns the most recent 100 matching records.

Records without `startSample`, such as some flush records, are excluded when a sample range filter is supplied.
