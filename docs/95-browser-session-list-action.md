# Browser Session List Action

Date: 2026-07-01

The browser console now includes a `List sessions` button.

It calls:

```text
GET /sessions
```

and displays the active session list in the JSON log panel.

This gives operators a quick way to inspect active engine sessions from the web console without knowing session IDs ahead of time.
