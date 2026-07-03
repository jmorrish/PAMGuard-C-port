# Archive access locking

The service now serializes in-process archive reads and writes with a dedicated mutex.

This protects the single-service/thread-pool deployment model from:

- two request threads appending to the same NDJSON archive at the same time;
- an archive query reading while another request is appending a record.

This is not a distributed lock. Multi-instance deployments should move result storage to a database or object-store workflow with explicit concurrency semantics.
