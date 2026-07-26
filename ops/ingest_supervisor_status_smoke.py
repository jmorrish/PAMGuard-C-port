#!/usr/bin/env python3
"""Smoke-test ingest supervisor status health summaries."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def load_supervisor_module():
    path = Path(__file__).with_name("ingest_supervisor.py")
    spec = importlib.util.spec_from_file_location("ingest_supervisor", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load ingest_supervisor.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RunningProcess:
    pid = 4242

    def poll(self):
        return None


def main() -> int:
    supervisor = load_supervisor_module()
    now = 1000.0
    running = supervisor.WorkerState(
        source_id="station-001",
        command=["ingest"],
        target_mode=supervisor.ACTIVE_PROJECT_MODE,
        project_id="11111111-1111-4111-8111-111111111111",
        acquisition_unit_id="22222222-2222-4222-8222-222222222222",
        engine="http://engine",
        source_url="http://user:secret@source/live.mp3?token=hidden",
        sample_rate_hz=48000,
        channel_count=2,
        chunk_frames=4096,
        process=RunningProcess(),
        last_start_unix_ms=900000,
    )
    waiting = supervisor.WorkerState(
        source_id="station-002",
        command=["ingest"],
        target_mode=supervisor.ACTIVE_PROJECT_MODE,
        project_id="11111111-1111-4111-8111-111111111111",
        acquisition_unit_id="33333333-3333-4333-8333-333333333333",
        process=None,
        restarts=1,
        next_start_time=now + 5.0,
        last_exit_unix_ms=999000,
        last_exit_code=1,
    )
    stopped = supervisor.WorkerState(
        source_id="station-003",
        command=["ingest"],
        target_mode=supervisor.LEGACY_SESSION_MODE,
        compatibility_session_id="deprecated-session",
        stopped=True,
    )

    body = supervisor.supervisor_status_body([running, waiting, stopped], now=now)
    assert body["schemaVersion"] == 3
    assert body["workerCount"] == 3
    assert body["health"] == "degraded"
    assert body["statusCounts"]["running"] == 1
    assert body["statusCounts"]["waiting_restart"] == 1
    assert body["statusCounts"]["stopped"] == 1
    assert body["healthCounts"]["healthy"] == 1
    assert body["healthCounts"]["degraded"] == 1
    assert body["healthCounts"]["stopped"] == 1

    workers = {worker["sourceId"]: worker for worker in body["workers"]}
    assert workers["station-001"]["health"] == "healthy"
    assert workers["station-001"]["targetMode"] == "active-project"
    assert (
        workers["station-001"]["projectId"]
        == "11111111-1111-4111-8111-111111111111"
    )
    assert (
        workers["station-001"]["acquisitionUnitId"]
        == "22222222-2222-4222-8222-222222222222"
    )
    assert workers["station-001"]["compatibilitySessionId"] is None
    assert "sessionId" not in workers["station-001"]
    assert workers["station-001"]["source"] == (
        "http://source/live.mp3"
    )
    assert workers["station-001"]["pid"] == 4242
    assert workers["station-001"]["uptimeMs"] == 100000
    assert workers["station-002"]["health"] == "degraded"
    assert workers["station-002"]["nextStartUnixMs"] == 1005000
    assert workers["station-003"]["health"] == "stopped"
    assert workers["station-003"]["targetMode"] == (
        "legacy-session-compatibility"
    )
    assert workers["station-003"]["compatibilitySessionId"] == (
        "deprecated-session"
    )
    assert "sessionId" not in workers["station-003"]
    print("ingest supervisor status smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
