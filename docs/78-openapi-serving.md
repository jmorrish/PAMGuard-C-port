# OpenAPI serving

The service can now serve the OpenAPI document directly:

```text
GET /openapi.yaml
```

Configuration:

```text
PAMGUARD_OPENAPI_FILE=/app/openapi.yaml
```

The container image copies `platform/openapi.yaml` into `/app/openapi.yaml` and enables the route by default. `GET /health` reports `openApiEnabled`.
