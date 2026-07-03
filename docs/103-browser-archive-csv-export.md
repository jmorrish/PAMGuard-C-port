# Browser Archive CSV Export

Date: 2026-07-01

The browser archive panel now includes `Export CSV`.

It uses the current event filters and downloads:

```text
detections-<sessionId>.csv
```

The request is authenticated with `X-API-Key` when the API key field is populated, matching the rest of the web console.
