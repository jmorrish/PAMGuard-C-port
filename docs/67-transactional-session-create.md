# Transactional session creation

`POST /sessions` now treats live session creation, optional persisted config writing, and runtime registry insertion as one operation.

If a later step fails after the engine session has been created, the service removes the live session before returning the error. If a persisted config file was written before the failure, it is removed as part of rollback.

This prevents a dangerous operational state where the API reports session creation failed but a hidden in-memory detector pipeline is still active.
