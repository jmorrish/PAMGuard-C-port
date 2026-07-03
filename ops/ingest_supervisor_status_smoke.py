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
        session_id="station-session",
        owner_id="owner-a",
        tenant_id="tenant-a",
        engine="http://engine",
        source_url="http://source/live.mp3",
        sample_rate_hz=48000,
        channel_count=2,
        chunk_frames=4096,
        process=RunningProcess(),
        last_start_unix_ms=900000,
    )
    waiting = supervisor.WorkerState(
        source_id="station-002",
        command=["ingest"],
        session_id="station-session-2",
        process=None,
        restarts=1,
        next_start_time=now + 5.0,
        last_exit_unix_ms=999000,
        last_exit_code=1,
    )
    stopped = supervisor.WorkerState(
        source_id="station-003",
        command=["ingest"],
        session_id="station-session-3",
        stopped=True,
    )

    body = supervisor.supervisor_status_body([running, waiting, stopped], now=now)
    assert body["schemaVersion"] == 2
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
    assert workers["station-001"]["pid"] == 4242
    assert workers["station-001"]["uptimeMs"] == 100000
    assert workers["station-002"]["health"] == "degraded"
    assert workers["station-002"]["nextStartUnixMs"] == 1005000
    assert workers["station-003"]["health"] == "stopped"
    print("ingest supervisor status smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
