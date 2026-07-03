# Result Archive NDJSON

Date: 2026-07-01

This checkpoint adds opt-in append-only result archiving for processed PCM chunks.

## Implemented

The service reads:

```text
PAMGUARD_RESULT_ARCHIVE_DIR
```

When set:

- each successful `POST /sessions/{sessionId}/pcm-f32le` appends one compact JSON line;
- archive file path is `<sessionId>.ndjson`;
- archived records include compact result fields, `schemaVersion`, `sessionId`, `inputFrames`, `startSample`, and `timeMs`;
- spectrogram frame arrays are not included unless the archive path is later expanded deliberately.

## Validation

CTest status after this checkpoint:

```text
22/22 tests passed
```

Archive smoke test:

```json
{"files":1,"lines":1,"inputFrames":128}
```

## Remaining result-storage work

- Add indexed query endpoints over archived detections.
- Add durable database storage.
- Add result retention policies.
