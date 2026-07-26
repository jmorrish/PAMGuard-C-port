# Kubernetes API key secret example

Date: 2026-07-01

## What changed

`deploy/kubernetes/engine-api-key-secret.example.yaml` shows a Kubernetes Secret for the engine API key and a deployment patch sketch using:

```text
PAMGUARD_API_KEY_FILE=/var/run/secrets/pamguard/api-key
```

## Why this matters

Mounted secrets are safer than putting API keys in ConfigMaps or plain deployment environment values.

The service trims whitespace from the secret file, so standard Kubernetes secret file mounts with trailing newlines are accepted.

The active-project ingest supervisor can consume the same Secret as an
environment variable named `PAMGUARD_API_KEY`. A schema-v2 manifest selects
`"apiKeyEnv": "PAMGUARD_API_KEY"`, so the value is not placed in process
arguments or the supervisor status file.
