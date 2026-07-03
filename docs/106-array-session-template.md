# Four-Channel Array Session Template

Date: 2026-07-01

This checkpoint adds:

```text
platform/array-session.example.json
```

## Purpose

The template provides a ready-to-edit four-channel click/localisation session for streamed array inputs.

## Defaults

- `channelCount`: `4`
- square hydrophone geometry;
- click detector enabled;
- click localisation enabled;
- click feature extraction enabled;
- basic classifier enabled;
- click train tracking enabled;
- whistle modules disabled by default.

## Ingest pairing

The multi-source ingest example now points the `harbour-array` source at this template and uses:

```text
audioFilter = pan=4c|c0=c0|c1=c1|c2=c2|c3=c3
```

This keeps decoded channel order explicit before the engine applies hydrophone geometry.

## Validation

The template has been posted to the service through `POST /sessions` and accepted by the current build.
