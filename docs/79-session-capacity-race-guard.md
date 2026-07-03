# Session capacity race guard

`POST /sessions` now checks `PAMGUARD_MAX_SESSIONS` while holding the same mutex used for the service config/runtime registry.

This serializes concurrent session creation so two requests cannot both pass the capacity check before either one inserts its session. The live manager session, persisted config, config registry, and runtime stats remain part of the same transactional create path.
