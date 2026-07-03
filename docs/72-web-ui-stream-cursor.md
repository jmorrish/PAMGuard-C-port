# Web UI stream cursor

The browser's synthetic PCM sender now keeps a local `nextStartSample` cursor.

Behaviour:

- creating a session resets the cursor to `0`;
- each PCM POST uses the current cursor as `startSample`;
- a successful response advances the cursor from `nextExpectedStartSample`;
- deleting the session resets the cursor.

This lets repeated `Send PCM block` clicks behave like a continuous stream instead of repeatedly posting overlapping chunks at `startSample=0`.
