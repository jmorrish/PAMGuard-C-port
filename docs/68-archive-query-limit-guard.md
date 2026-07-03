# Archive query limit guard

The service now supports an optional archive query cap:

```text
PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS
```

Service default `0` preserves existing unlimited behaviour. The provided container image and compose file set `10000` as a safer deployment default.

When set to a positive number, `GET /sessions/{sessionId}/archive` rejects:

- `limit=0`, because that requests the full archive;
- any positive `limit` greater than the configured cap.

The health endpoint reports the configured value as `maxArchiveQueryRecords`.
