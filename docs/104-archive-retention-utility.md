# Archive Retention Utility

Date: 2026-07-01

This checkpoint adds:

```text
ops/archive_retention.py
```

## Purpose

Result archives are append-only NDJSON files plus detector-event sidecars. Long-running streams need explicit retention until a database-backed archive is introduced.

## Dry run

```powershell
python .\ops\archive_retention.py --archive-dir .\data\results --max-age-days 30
```

## Apply

Deletion only happens with `--apply`:

```powershell
python .\ops\archive_retention.py --archive-dir .\data\results --max-age-days 30 --apply
```

## Other policy

Keep only the newest N archive files:

```powershell
python .\ops\archive_retention.py --archive-dir .\data\results --max-files 100
```

## Safeguard

The utility only considers files ending in:

- `.ndjson`
- `.events.ndjson`
