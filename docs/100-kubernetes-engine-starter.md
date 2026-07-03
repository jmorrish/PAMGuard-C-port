# Kubernetes Engine Starter Manifest

Date: 2026-07-01

This checkpoint adds:

```text
deploy/kubernetes/engine.yaml
```

## Contents

- `ConfigMap` for service environment settings.
- `PersistentVolumeClaim` for `/data`.
- `Deployment` for `pamguard_engine_service`.
- `Service` exposing port `8080` inside the cluster.

The example enables `PAMGUARD_REQUIRE_SESSION_METADATA=1`, so ingest workers must provide `ownerId` and `tenantId` when bootstrapping sessions.

`PAMGUARD_INGEST_STATUS_FILE` is shown as a commented option because it should only be enabled when an ingest supervisor writes a shared status file into the engine data volume.

## Probes

The deployment uses:

- readiness: `GET /ready`;
- liveness: `GET /health`.

## Apply

```powershell
kubectl apply -f .\deploy\kubernetes\engine.yaml
```

## Assumptions

- Image name is `pamguard-engine:local`.
- One engine replica owns the PVC.
- Ingest workers can run as separate Deployments or Jobs and target `http://pamguard-engine:8080`.
- A companion example is available at `deploy/kubernetes/ingest-worker.example.yaml`.
- Archive retention can be scheduled with `deploy/kubernetes/archive-retention-cronjob.example.yaml`.
- Production deployments should add TLS/ingress, authentication secret management, resource tuning, and horizontal sharding once load measurements are available.
