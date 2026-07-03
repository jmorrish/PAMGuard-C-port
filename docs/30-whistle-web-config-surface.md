# Whistle/Moan Web Configuration Surface

Date: 2026-07-01

This checkpoint exposes the connected-region whistle/moan fragmentation controls through the browser console and API schema.

## Implemented

- Added browser controls for:
  - fragmentation method `0/1/2/3`;
  - 4-connect or 8-connect region linking;
  - minimum region pixels;
  - minimum region length;
  - small-shape stub retention;
  - `maxCrossLength` for method `3` rejoining.
- Extended session JSON responses to report the stored whistle/moan region settings.
- Updated the OpenAPI session-create schema and HTTP service example.

## Operator default

The web console now defaults to PAMGuard-style re-linking:

```json
{
  "whistle": {
    "regionEnabled": true,
    "fragmentationMethod": 3,
    "connectType": 8,
    "minPixels": 20,
    "minLength": 10,
    "keepShapeStubs": false,
    "maxCrossLength": 5
  }
}
```

## Remaining web-console work

- Add grouped profiles/presets per deployment.
- Add click detector and classifier configuration controls with validation.
- Add persisted per-user/session templates.
