# Session ID validation

Session IDs are now constrained to persistence-safe characters:

```text
A-Z a-z 0-9 - _ .
```

This avoids ambiguous persisted config/archive filenames. Previously, unsafe characters were sanitized for filenames, which could make different session IDs collide on disk.
