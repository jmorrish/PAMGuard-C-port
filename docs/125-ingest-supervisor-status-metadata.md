# Ingest supervisor status metadata

Updated: 2026-07-25

The supervisor status document is schema version 3. Top-level fields include:

- `health`;
- `workerCount`;
- `statusCounts`; and
- `healthCounts`.

Each worker identifies its stable target with:

- `sourceId`;
- `targetMode`;
- `projectId`;
- `acquisitionUnitId`;
- `compatibilitySessionId` (normally `null`);
- `engine` and `source`;
- `sampleRateHz`, `channelCount`, and `chunkFrames`; and
- `uptimeMs`.

Process fields remain:

- `status` and `health`;
- `pid` and `restarts`;
- `lastStartUnixMs`, `lastObservedUnixMs`, and `lastExitUnixMs`;
- `lastExitCode`; and
- `nextStartUnixMs`.

For the production `active-project` mode, `projectId` and
`acquisitionUnitId` are populated and `compatibilitySessionId` is null. Only
the explicitly deprecated `legacy-session-compatibility` mode populates the
compatibility session ID.

The status file intentionally omits the full worker command and API key. It
also removes URL user-info and query strings from `source`. Operators can
therefore correlate a source with a stable controlled-unit instance without
publishing those launch secrets.
