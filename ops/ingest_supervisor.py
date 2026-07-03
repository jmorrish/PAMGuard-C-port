#!/usr/bin/env python3
"""Supervise multiple ffmpeg_stream_ingest workers.

The engine remains one deterministic session per source/input. This supervisor
is an operations wrapper that starts one ingest bridge process per configured
source and restarts a bridge if the bridge process exits.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class WorkerState:
    source_id: str
    command: list[str]
    session_id: str = ""
    owner_id: str = ""
    tenant_id: str = ""
    engine: str = ""
    source_url: str = ""
    sample_rate_hz: Any = None
    channel_count: Any = None
    chunk_frames: Any = None
    process: subprocess.Popen[Any] | None = None
    restarts: int = 0
    next_start_time: float = 0.0
    stopped: bool = False
    last_start_unix_ms: int = 0
    last_exit_unix_ms: int = 0
    last_exit_code: int | None = None


def load_config(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def value_from(source: dict[str, Any], defaults: dict[str, Any], *names: str, fallback: Any = None) -> Any:
    for name in names:
        if name in source:
            return source[name]
    for name in names:
        if name in defaults:
            return defaults[name]
    return fallback


def bool_from(source: dict[str, Any], defaults: dict[str, Any], name: str, fallback: bool = False) -> bool:
    value = value_from(source, defaults, name, fallback=fallback)
    return bool(value)


def append_option(command: list[str], flag: str, value: Any) -> None:
    if value is not None and value != "":
        command.extend([flag, str(value)])


def append_repeated_option(command: list[str], flag: str, value: Any) -> None:
    if value is None or value == "":
        return
    if isinstance(value, list):
        for item in value:
            append_option(command, flag, item)
        return
    append_option(command, flag, value)


def resolve_path(base_dir: Path, value: str | None) -> str | None:
    if not value:
        return None
    path = Path(value)
    if path.is_absolute():
        return str(path)
    return str((base_dir / path).resolve())


def command_for_source(config_path: Path, config: dict[str, Any], source: dict[str, Any]) -> tuple[str, list[str], dict[str, Any]]:
    defaults = config.get("defaults", {})
    base_dir = config_path.parent
    source_id = str(value_from(source, defaults, "id", "session", fallback="source"))
    source_url = value_from(source, defaults, "source", "sourceUrl")
    session_id = value_from(source, defaults, "session", "sessionId", fallback=source_id)
    owner_id = value_from(source, defaults, "ownerId", "owner", fallback="")
    tenant_id = value_from(source, defaults, "tenantId", "tenant", fallback="")
    if not source_url:
        raise ValueError(f"source {source_id} is missing source/sourceUrl")
    if not session_id:
        raise ValueError(f"source {source_id} is missing session/sessionId")

    ingest_exe = value_from(source, defaults, "ingestExecutable", fallback=config.get("ingestExecutable"))
    if not ingest_exe:
        ingest_exe = str((Path.cwd() / "cpp-engine" / "build" / "ffmpeg_stream_ingest.exe").resolve())
    ingest_exe = resolve_path(base_dir, str(ingest_exe)) or str(ingest_exe)

    engine = value_from(source, defaults, "engine", fallback=config.get("engine", "http://127.0.0.1:8080"))
    sample_rate = value_from(source, defaults, "sampleRateHz", "sampleRate", fallback=48000)
    channels = value_from(source, defaults, "channels", "channelCount", fallback=1)
    chunk_frames = value_from(source, defaults, "chunkFrames", fallback=4096)

    command = [
        ingest_exe,
        "--source",
        str(source_url),
        "--engine",
        str(engine),
        "--session",
        str(session_id),
        "--source-id",
        str(source_id),
        "--sample-rate",
        str(sample_rate),
        "--channels",
        str(channels),
        "--chunk-frames",
        str(chunk_frames),
    ]

    session_config = resolve_path(base_dir, value_from(source, defaults, "sessionConfig", "sessionConfigPath"))
    append_option(command, "--session-config", session_config)
    append_option(command, "--owner-id", owner_id)
    append_option(command, "--tenant-id", tenant_id)
    append_option(command, "--ffmpeg", value_from(source, defaults, "ffmpeg", "ffmpegPath", fallback=config.get("ffmpeg")))
    append_option(command, "--audio-filter", value_from(source, defaults, "audioFilter", "audio_filter"))
    append_repeated_option(command, "--ffmpeg-input-option", value_from(source, defaults, "ffmpegInputOptions", "ffmpeg_input_options"))
    append_option(command, "--max-chunks", value_from(source, defaults, "maxChunks"))
    append_option(command, "--max-restarts", value_from(source, defaults, "maxRestarts"))
    append_option(command, "--restart-delay-ms", value_from(source, defaults, "restartDelayMs", fallback=5000))
    append_option(command, "--start-sample", value_from(source, defaults, "startSample"))

    api_key = value_from(source, defaults, "apiKey", fallback=config.get("apiKey"))
    api_key_env = value_from(source, defaults, "apiKeyEnv", fallback=config.get("apiKeyEnv"))
    if api_key:
        append_option(command, "--api-key", api_key)
    elif api_key_env:
        append_option(command, "--api-key-env", api_key_env)

    if bool_from(source, defaults, "restart", fallback=True):
        command.append("--restart")
    if bool_from(source, defaults, "allowExistingSession", fallback=True):
        command.append("--allow-existing-session")
    if bool_from(source, defaults, "resumeFromEngine", fallback=True):
        command.append("--resume-from-engine")
    if bool_from(source, defaults, "realtime", fallback=True):
        command.append("--realtime")

    metadata = {
        "sessionId": str(session_id),
        "ownerId": str(owner_id) if owner_id is not None else "",
        "tenantId": str(tenant_id) if tenant_id is not None else "",
        "engine": str(engine),
        "source": str(source_url),
        "sampleRateHz": sample_rate,
        "channelCount": channels,
        "chunkFrames": chunk_frames,
    }
    return source_id, command, metadata


def quote_command(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def redacted_command(command: list[str]) -> list[str]:
    redacted: list[str] = []
    redact_next = False
    for part in command:
        if redact_next:
            redacted.append("<redacted>")
            redact_next = False
            continue
        if part.startswith("--api-key="):
            redacted.append("--api-key=<redacted>")
            continue
        redacted.append(part)
        if part == "--api-key":
            redact_next = True
    return redacted


def quote_display_command(command: list[str]) -> str:
    return quote_command(redacted_command(command))


def launch_worker(state: WorkerState) -> None:
    print(f"[supervisor] starting {state.source_id}: {quote_display_command(state.command)}", flush=True)
    state.process = subprocess.Popen(state.command)
    state.last_start_unix_ms = int(time.time() * 1000)
    state.last_exit_code = None
    state.next_start_time = 0.0


def stop_workers(workers: list[WorkerState]) -> None:
    for worker in workers:
        if worker.process and worker.process.poll() is None:
            print(f"[supervisor] stopping {worker.source_id}", flush=True)
            worker.process.terminate()
    deadline = time.time() + 10.0
    for worker in workers:
        process = worker.process
        if not process:
            continue
        while process.poll() is None and time.time() < deadline:
            time.sleep(0.1)
        if process.poll() is None:
            print(f"[supervisor] killing {worker.source_id}", flush=True)
            process.kill()


def health_for_status(status: str) -> str:
    if status == "running":
        return "healthy"
    if status == "not_started":
        return "pending"
    if status == "stopped":
        return "stopped"
    return "degraded"


def overall_health(health_counts: dict[str, int], worker_count: int) -> str:
    if worker_count == 0:
        return "empty"
    if health_counts.get("degraded", 0) > 0:
        return "degraded"
    if health_counts.get("healthy", 0) == worker_count:
        return "healthy"
    if health_counts.get("stopped", 0) == worker_count:
        return "stopped"
    if health_counts.get("pending", 0) == worker_count:
        return "pending"
    return "mixed"


def worker_status(worker: WorkerState, now: float | None = None) -> dict[str, Any]:
    process = worker.process
    running = process is not None and process.poll() is None
    now = time.time() if now is None else now
    now_ms = int(now * 1000)
    if worker.stopped:
        status = "stopped"
    elif running:
        status = "running"
    elif worker.next_start_time > now:
        status = "waiting_restart"
    elif process is None:
        status = "not_started"
    else:
        status = "exited"
    uptime_ms = None
    if running and worker.last_start_unix_ms:
        uptime_ms = now_ms - worker.last_start_unix_ms
    health = health_for_status(status)
    return {
        "sourceId": worker.source_id,
        "sessionId": worker.session_id,
        "ownerId": worker.owner_id,
        "tenantId": worker.tenant_id,
        "engine": worker.engine,
        "source": worker.source_url,
        "sampleRateHz": worker.sample_rate_hz,
        "channelCount": worker.channel_count,
        "chunkFrames": worker.chunk_frames,
        "status": status,
        "health": health,
        "pid": process.pid if running and process is not None else None,
        "restarts": worker.restarts,
        "uptimeMs": uptime_ms,
        "lastObservedUnixMs": now_ms,
        "lastStartUnixMs": worker.last_start_unix_ms or None,
        "lastExitUnixMs": worker.last_exit_unix_ms or None,
        "lastExitCode": worker.last_exit_code,
        "nextStartUnixMs": int(worker.next_start_time * 1000) if worker.next_start_time > now else None,
    }


def supervisor_status_body(workers: list[WorkerState], now: float | None = None) -> dict[str, Any]:
    now = time.time() if now is None else now
    now_ms = int(now * 1000)
    worker_items = [worker_status(worker, now) for worker in workers]
    status_counts = {
        "running": 0,
        "waiting_restart": 0,
        "not_started": 0,
        "exited": 0,
        "stopped": 0,
    }
    health_counts = {
        "healthy": 0,
        "degraded": 0,
        "pending": 0,
        "stopped": 0,
    }
    for item in worker_items:
        status = item.get("status", "exited")
        health = item.get("health", "degraded")
        status_counts[status] = status_counts.get(status, 0) + 1
        health_counts[health] = health_counts.get(health, 0) + 1
    worker_count = len(worker_items)
    return {
        "schemaVersion": 2,
        "generatedUnixMs": now_ms,
        "health": overall_health(health_counts, worker_count),
        "workerCount": worker_count,
        "statusCounts": status_counts,
        "healthCounts": health_counts,
        "workers": worker_items,
    }


def write_status_file(status_path: Path, workers: list[WorkerState]) -> None:
    status_path.parent.mkdir(parents=True, exist_ok=True)
    body = supervisor_status_body(workers)
    temp_path = status_path.with_name(status_path.name + ".tmp")
    temp_path.write_text(json.dumps(body, indent=2), encoding="utf-8")
    temp_path.replace(status_path)


def supervise(
    workers: list[WorkerState],
    worker_restart_delay_seconds: float,
    max_worker_restarts: int,
    status_path: Path | None,
    status_interval_seconds: float) -> int:
    stopping = False
    last_status_write = 0.0

    def handle_signal(signum: int, _frame: Any) -> None:
        nonlocal stopping
        print(f"[supervisor] received signal {signum}, shutting down", flush=True)
        stopping = True

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    for worker in workers:
        launch_worker(worker)
    if status_path is not None:
        write_status_file(status_path, workers)
        last_status_write = time.time()

    try:
        while not stopping:
            now = time.time()
            active = False
            for worker in workers:
                process = worker.process
                if worker.stopped:
                    continue
                if process is None:
                    if now >= worker.next_start_time:
                        launch_worker(worker)
                    active = True
                    continue
                code = process.poll()
                if code is None:
                    active = True
                    continue
                print(f"[supervisor] {worker.source_id} exited with code {code}", flush=True)
                worker.process = None
                worker.last_exit_code = code
                worker.last_exit_unix_ms = int(now * 1000)
                worker.restarts += 1
                if max_worker_restarts > 0 and worker.restarts > max_worker_restarts:
                    worker.stopped = True
                    print(f"[supervisor] {worker.source_id} reached max worker restarts", flush=True)
                else:
                    worker.next_start_time = now + worker_restart_delay_seconds
                    active = True
            if status_path is not None and now - last_status_write >= status_interval_seconds:
                write_status_file(status_path, workers)
                last_status_write = now
            if not active:
                if status_path is not None:
                    write_status_file(status_path, workers)
                return 1
            time.sleep(1.0)
    finally:
        stop_workers(workers)
        if status_path is not None:
            write_status_file(status_path, workers)
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Supervise multiple PAMGuard FFmpeg ingest workers.")
    parser.add_argument("--config", required=True, help="Path to ingest supervisor JSON config.")
    parser.add_argument("--validate", action="store_true", help="Validate enabled sources without launching workers.")
    parser.add_argument("--dry-run", action="store_true", help="Print worker commands without launching them.")
    args = parser.parse_args(argv)

    config_path = Path(args.config).resolve()
    config = load_config(config_path)
    defaults = config.get("defaults", {})
    workers: list[WorkerState] = []
    for source in config.get("sources", []):
        if not source.get("enabled", True):
            print(f"[supervisor] disabled source skipped: {source.get('id') or source.get('session')}", flush=True)
            continue
        source_id, command, metadata = command_for_source(config_path, config, source)
        workers.append(WorkerState(
            source_id=source_id,
            command=command,
            session_id=metadata["sessionId"],
            owner_id=metadata["ownerId"],
            tenant_id=metadata["tenantId"],
            engine=metadata["engine"],
            source_url=metadata["source"],
            sample_rate_hz=metadata["sampleRateHz"],
            channel_count=metadata["channelCount"],
            chunk_frames=metadata["chunkFrames"],
        ))

    if not workers:
        print("[supervisor] no enabled sources configured", flush=True)
        return 0

    if args.validate:
        print(f"[supervisor] validated {len(workers)} enabled source(s)", flush=True)
        return 0

    if args.dry_run:
        for worker in workers:
            print(f"{worker.source_id}: {quote_display_command(worker.command)}")
        return 0

    restart_delay = float(value_from({}, defaults, "workerRestartDelaySeconds", fallback=config.get("workerRestartDelaySeconds", 5.0)))
    max_restarts = int(value_from({}, defaults, "maxWorkerRestarts", fallback=config.get("maxWorkerRestarts", 0)))
    raw_status_path = value_from({}, defaults, "statusFile", fallback=config.get("statusFile"))
    status_path = None
    if raw_status_path:
        candidate = Path(str(raw_status_path))
        status_path = candidate if candidate.is_absolute() else (config_path.parent / candidate).resolve()
    status_interval = float(value_from({}, defaults, "statusIntervalSeconds", fallback=config.get("statusIntervalSeconds", 5.0)))
    return supervise(workers, restart_delay, max_restarts, status_path, status_interval)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
