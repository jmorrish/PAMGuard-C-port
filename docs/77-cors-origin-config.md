# CORS origin configuration

The service now supports:

```text
PAMGUARD_CORS_ORIGIN
```

Default is `*` for local development and simple demos. Production browser deployments should set it to the exact UI origin, for example:

```text
PAMGUARD_CORS_ORIGIN=https://pamguard.example.org
```

`GET /health` reports the active value as `corsOrigin`.
