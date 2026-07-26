# Kubernetes active-project ingest example

Updated: 2026-07-25

`deploy/kubernetes/ingest-worker.example.yaml` runs the Python supervisor for
one external source and one existing Sound Acquisition instance. It posts PCM
through the stable project API:

```text
POST /v1/projects/active/acquisitions/{acquisitionUnitId}/pcm-f32le
```

It does not create an AnalysisSession.

## Before applying

1. Save or open the intended project in the engine.
2. Set its stable UUID as the ConfigMap `projectId`.
3. Set the target Sound Acquisition unit's UUID as `acquisitionUnitId`.
4. Match `sampleRateHz` and `channels` to that unit.
5. Replace the source URL.
6. Make the engine API-key Secret available under the example name.
7. Make sure the active project runtime is started.

The unit IDs can be read from:

```text
GET /v1/projects/active/acquisitions
```

Then apply:

```powershell
kubectl apply -f .\deploy\kubernetes\ingest-worker.example.yaml
```

## Production notes

- Keep `replicas: 1` and the `Recreate` update strategy; two overlapping
  writers would race the same Acquisition timeline.
- The PVC persists both the sample cursor and supervisor status document.
- Keep the API key in a Secret. If a source URL itself contains credentials,
  mount the complete credential-bearing manifest from a Secret instead of
  using the example ConfigMap.
- `PAMGUARD_API_KEY` is read from an environment variable and is not placed in
  the launch command or status file.
- The worker verifies project identity, Acquisition identity, audio shape,
  running state, working revision, and absence of built-in capture before
  FFmpeg starts.
- Project revisions fence stale workers. Restart the worker after an intended
  project edit so it discovers the new revision and resets its timeline cursor.
- Use a separate manifest/Deployment per independent source or a single
  supervisor manifest containing multiple non-overlapping Acquisition targets.
