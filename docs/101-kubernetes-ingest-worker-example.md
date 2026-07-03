# Kubernetes Ingest Worker Example

Date: 2026-07-01

This checkpoint adds:

```text
deploy/kubernetes/ingest-worker.example.yaml
```

## Purpose

The manifest shows how to run one `ffmpeg_stream_ingest` worker for one source/session against the in-cluster engine service:

```text
http://pamguard-engine:8080
```

## Apply

Edit the source URL, source ID, session ID, owner ID, tenant ID, channel count, and optional audio filter first.

Then apply:

```powershell
kubectl apply -f .\deploy\kubernetes\ingest-worker.example.yaml
```

## Production notes

- Use one worker Deployment per live source/session.
- Put source credentials and API keys in Kubernetes Secrets, not ConfigMaps.
- The example reads `PAMGUARD_API_KEY` from the engine API-key Secret and passes `--api-key-env PAMGUARD_API_KEY`, so the key value is not placed in process arguments.
- The example passes `--owner-id` and `--tenant-id` to satisfy engine deployments that enable `PAMGUARD_REQUIRE_SESSION_METADATA`.
- Keep channel count, audio filter, and session hydrophone geometry aligned for localisation correctness.
- Use `--resume-from-engine` and `--allow-existing-session` for supervised restarts.
