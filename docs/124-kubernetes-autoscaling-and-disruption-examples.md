# Kubernetes autoscaling and disruption examples

Date: 2026-07-01

## What changed

Two example manifests were added:

- `deploy/kubernetes/engine-hpa.example.yaml`
- `deploy/kubernetes/engine-pdb.example.yaml`

## Important scaling caveat

The base `engine.yaml` uses one `ReadWriteOnce` PVC and the engine keeps active session state in-process.

Do not simply raise `replicas` and expect correctness.

Before horizontal scaling, production deployment needs one of these patterns:

- Sticky routing by session/source to the same engine pod.
- One engine shard per source group.
- External or `ReadWriteMany` archive storage.
- Separate archive/session storage per engine shard.
- Ingest workers configured to target the correct engine service/shard.

## HPA example

The HPA example uses CPU and memory utilization with conservative scale-down behavior. It is a starter manifest, not a production tuning recommendation.

## PDB example

The PDB example keeps at least one engine pod available during voluntary disruptions.

For single-replica development deployments, this can block node drains. Use it only when that behavior is intentional.
