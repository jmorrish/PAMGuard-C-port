# Click train sample timeline

Click train summaries now include the click timeline used to form the reported train:

- `clickStartSamples`
- `clickTimeMs`

These arrays make train outputs easier to join with per-click localisation and bearing records, which already expose `clickStartSample`. This preserves the existing train grouping decisions while giving downstream web/API clients enough context to link click tracking with multi-channel localisation results.
