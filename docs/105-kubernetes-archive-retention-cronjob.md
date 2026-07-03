# Kubernetes Archive Retention CronJob Example

Date: 2026-07-01

This checkpoint adds:

```text
deploy/kubernetes/archive-retention-cronjob.example.yaml
```

## Purpose

The CronJob runs the container-packaged archive retention utility against `/data/results`.

Default example policy:

```text
delete archive files older than 30 days
```

## Apply

Review the schedule and retention policy first, then apply:

```powershell
kubectl apply -f .\deploy\kubernetes\archive-retention-cronjob.example.yaml
```

## Safety note

Unlike the local utility default, the CronJob example includes `--apply`. Remove `--apply` for a dry-run CronJob.
