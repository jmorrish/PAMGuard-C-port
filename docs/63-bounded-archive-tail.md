# Bounded archive tail reads

Archive queries now avoid loading an entire session NDJSON file when the caller requests a positive `limit`.

`GET /sessions/{sessionId}/archive?limit=N` keeps only the most recent `N` parsed records in memory while scanning the archive. This preserves chronological output order while bounding memory use for the normal dashboard/operator case.

`limit=0` intentionally keeps the existing contract of returning every archived record. Use that only for offline export or maintenance workflows, not high-frequency dashboard polling.
