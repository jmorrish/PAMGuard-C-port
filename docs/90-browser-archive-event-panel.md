# Browser Archive Event Panel

Date: 2026-07-01

This checkpoint adds detector archive querying to the browser console.

## UI controls

The engine response panel now includes a `Detection archive` section with:

- event type filter;
- event limit;
- start-sample lower and upper bounds;
- cursor input;
- `Load tail`;
- `Load cursor page`;
- `Load next`.

## Workflow

For a live or streamed source:

- create a session;
- send PCM chunks;
- optionally flush at end-of-file or end-of-stream;
- load the event tail for the latest detector outputs;
- use cursor paging to walk forward through historical detector events.

## API used

```text
GET /sessions/{sessionId}/archive/detections
```

The browser sends the configured API key through `X-API-Key` when present, matching the rest of the console.

## Notes

The event list shows the stable event envelope fields and a compact JSON preview of the detector-specific `payload`.

This is still a proof console, not the final analyst interface, but it now exercises the same archive-event query flow expected by the production web frontend.
