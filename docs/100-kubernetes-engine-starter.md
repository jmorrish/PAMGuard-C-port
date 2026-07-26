# Kubernetes project-authoritative engine starter

Updated: 2026-07-25

`deploy/kubernetes/engine.yaml` contains:

- a ConfigMap for project-authoritative service settings;
- a PVC for the engine-owned `/data/projects` root;
- a one-replica, `Recreate` Deployment; and
- a cluster-local Service on port 8080.

The default manifest does not configure a session directory, session metadata
policy, or a fixed AnalysisSession ingest runtime. To reopen a particular
saved project after startup, add this ConfigMap entry only after replacing the
placeholder with its stable UUID:

```yaml
PAMGUARD_ACTIVE_PROJECT_ID: "replace-with-saved-project-uuid"
```

The running project must still be started before it accepts PCM.

`PAMGUARD_INGEST_STATUS_FILE` remains a commented option. Enable it only if the
engine and supervisor mount the same status-file volume; the separate ingest
starter uses its own PVC by default.

## Probes

- readiness: `GET /ready`;
- liveness: `GET /health`.

## Apply

```powershell
kubectl apply -f .\deploy\kubernetes\engine.yaml
```

The companion `deploy/kubernetes/ingest-worker.example.yaml` targets a stable
Sound Acquisition unit in the active project. Production deployments still
need ingress/TLS, API-key Secret wiring, storage-class selection, resource
tuning, and deliberate sharding.
