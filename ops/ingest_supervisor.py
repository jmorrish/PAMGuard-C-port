#!/usr/bin/env python3
"""Supervise project-authoritative PAMGuard Acquisition ingest workers.

The default mode launches one shell-free FFmpeg worker per configured source.
Each worker discovers the exact active project working revision and posts PCM
through the stable Acquisition controlled-unit API. A legacy session bridge is
available only when the manifest explicitly selects
``legacy-session-compatibility``.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shlex
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ACTIVE_PROJECT_MODE = "active-project"
LEGACY_SESSION_MODE = "legacy-session-compatibility"
UUID_V4_PATTERN = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"
)
SOURCE_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


@dataclass
class WorkerState:
    source_id: str
    command: list[str]
    target_mode: str = ACTIVE_PROJECT_MODE
    project_id: str = ""
    acquisition_unit_id: str = ""
    compatibility_session_id: str = ""
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


def value_from(
    source: dict[str, Any],
    defaults: dict[str, Any],
    *names: str,
    fallback: Any = None,
) -> Any:
    for name in names:
        if name in source:
            return source[name]
    for name in names:
        if name in defaults:
            return defaults[name]
    return fallback


def bool_from(
    source: dict[str, Any],
    defaults: dict[str, Any],
    name: str,
    fallback: bool = False,
) -> bool:
    value = value_from(source, defaults, name, fallback=fallback)
    if not isinstance(value, bool):
        raise ValueError(f"{name} must be a JSON boolean")
    return value


def integer_option(
    value: Any,
    field: str,
    source_id: str,
    minimum: int,
) -> int:
    if isinstance(value, bool):
        raise ValueError(f"source {source_id} {field} must be an integer")
    if isinstance(value, str):
        if not re.fullmatch(r"[+-]?[0-9]+", value.strip()):
            raise ValueError(
                f"source {source_id} {field} must be an integer"
            )
    if isinstance(value, float) and (
        not math.isfinite(value) or not value.is_integer()
    ):
        raise ValueError(f"source {source_id} {field} must be an integer")
    try:
        parsed = int(value)
    except (TypeError, ValueError, OverflowError) as error:
        raise ValueError(
            f"source {source_id} {field} must be an integer"
        ) from error
    if parsed < minimum:
        qualifier = "positive" if minimum == 1 else "non-negative"
        raise ValueError(f"source {source_id} {field} must be {qualifier}")
    return parsed


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


def append_internal_option(command: list[str], flag: str, value: Any) -> None:
    """Append an argparse option without letting a leading '-' become syntax."""
    if value is not None and value != "":
        command.append(f"{flag}={value}")


def append_internal_repeated_option(
    command: list[str], flag: str, value: Any
) -> None:
    if value is None or value == "":
        return
    if isinstance(value, list):
        for item in value:
            append_internal_option(command, flag, item)
        return
    append_internal_option(command, flag, value)


def resolve_path(base_dir: Path, value: str | None) -> str | None:
    if not value:
        return None
    path = Path(value)
    if path.is_absolute():
        return str(path)
    return str((base_dir / path).resolve())


def resolve_executable(base_dir: Path, value: str) -> str:
    path = Path(value)
    if path.is_absolute():
        return str(path)
    if "/" in value or "\\" in value or value.startswith("."):
        return str((base_dir / path).resolve())
    return value


def require_uuid_v4(value: Any, field: str, source_id: str) -> str:
    text = str(value or "")
    if not UUID_V4_PATTERN.fullmatch(text):
        raise ValueError(
            f"source {source_id} {field} must be a lowercase UUIDv4"
        )
    return text


def require_source_id(value: Any) -> str:
    source_id = str(value or "")
    if not SOURCE_ID_PATTERN.fullmatch(source_id):
        raise ValueError(
            "source id is required and must contain only letters, numbers, "
            "dot, underscore, or hyphen"
        )
    return source_id


def normalized_engine_url(value: Any) -> str:
    engine = str(value or "").rstrip("/")
    parsed = urllib.parse.urlsplit(engine)
    try:
        parsed_port = parsed.port
    except ValueError as error:
        raise ValueError("engine URL has an invalid port") from error
    if (
        parsed.scheme not in {"http", "https"}
        or not parsed.hostname
        or parsed_port is not None and parsed_port <= 0
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError(
            "engine must be an HTTP(S) URL with a host and no credentials, "
            "query, or fragment"
        )
    return engine


def reject_active_project_session_fields(
    source: dict[str, Any], defaults: dict[str, Any], config: dict[str, Any], source_id: str
) -> None:
    legacy_fields = {
        "session",
        "sessionId",
        "sessionConfig",
        "sessionConfigPath",
        "allowExistingSession",
        "resumeFromEngine",
        "owner",
        "ownerId",
        "tenant",
        "tenantId",
        "module",
        "moduleId",
        "runtimeNodeId",
    }
    configured = sorted(
        field
        for field in legacy_fields
        if field in source or field in defaults or field in config
    )
    if configured:
        raise ValueError(
            f"source {source_id} active-project mode rejects legacy session "
            f"fields: {', '.join(configured)}"
        )
    if (
        "ingestExecutable" in source
        or "ingestExecutable" in defaults
        or "ingestExecutable" in config
    ):
        raise ValueError(
            f"source {source_id} active-project mode does not use "
            "ffmpeg_stream_ingest/ingestExecutable"
        )


def cursor_path_for_source(
    config_path: Path,
    config: dict[str, Any],
    defaults: dict[str, Any],
    source: dict[str, Any],
    source_id: str,
) -> str:
    raw_directory = value_from(
        source,
        defaults,
        "cursorDirectory",
        fallback=config.get("cursorDirectory", ".pamguard-ingest-cursors"),
    )
    directory = Path(str(raw_directory))
    if not directory.is_absolute():
        directory = (config_path.parent / directory).resolve()
    return str(directory / f"{source_id}.cursor.json")


def command_for_source(
    config_path: Path,
    config: dict[str, Any],
    source: dict[str, Any],
) -> tuple[str, list[str], dict[str, Any]]:
    defaults = config.get("defaults", {})
    if not isinstance(defaults, dict):
        raise ValueError("defaults must be an object")
    if not isinstance(source, dict):
        raise ValueError("each source must be an object")
    base_dir = config_path.parent
    source_id = require_source_id(value_from(source, defaults, "id"))
    source_url = value_from(source, defaults, "source", "sourceUrl")
    if not isinstance(source_url, str) or not source_url:
        raise ValueError(
            f"source {source_id} source/sourceUrl must be a non-empty string"
        )

    mode = str(
        value_from(
            source,
            defaults,
            "mode",
            fallback=config.get("mode", ACTIVE_PROJECT_MODE),
        )
    )
    if mode not in {ACTIVE_PROJECT_MODE, LEGACY_SESSION_MODE}:
        raise ValueError(
            f"source {source_id} mode must be {ACTIVE_PROJECT_MODE} or "
            f"{LEGACY_SESSION_MODE}"
        )
    engine = normalized_engine_url(
        value_from(
            source,
            defaults,
            "engine",
            fallback=config.get("engine", "http://127.0.0.1:8080"),
        )
    )
    sample_rate = value_from(source, defaults, "sampleRateHz", "sampleRate", fallback=48000)
    channels = value_from(source, defaults, "channels", "channelCount", fallback=1)
    chunk_frames = value_from(source, defaults, "chunkFrames", fallback=4096)
    sample_rate = integer_option(
        sample_rate,
        "sampleRateHz",
        source_id,
        1,
    )
    channels = integer_option(channels, "channels", source_id, 1)
    chunk_frames = integer_option(
        chunk_frames,
        "chunkFrames",
        source_id,
        1,
    )

    ffmpeg = str(
        value_from(
            source,
            defaults,
            "ffmpeg",
            "ffmpegPath",
            fallback=config.get("ffmpeg", "ffmpeg"),
        )
    )
    ffmpeg = resolve_executable(base_dir, ffmpeg)
    owner_id = ""
    tenant_id = ""
    project_id = ""
    acquisition_unit_id = ""
    compatibility_session_id = ""

    if mode == ACTIVE_PROJECT_MODE:
        reject_active_project_session_fields(source, defaults, config, source_id)
        project_id = require_uuid_v4(
            value_from(
                source,
                defaults,
                "projectId",
                fallback=config.get("projectId"),
            ),
            "projectId",
            source_id,
        )
        acquisition_unit_id = require_uuid_v4(
            value_from(source, defaults, "acquisitionUnitId"),
            "acquisitionUnitId",
            source_id,
        )
        command = [
            sys.executable,
            str(Path(__file__).resolve()),
            "--project-worker",
            f"--source-id={source_id}",
            f"--source={source_url}",
            f"--engine={engine}",
            f"--project-id={project_id}",
            f"--acquisition-unit-id={acquisition_unit_id}",
            f"--sample-rate={sample_rate}",
            f"--channels={channels}",
            f"--chunk-frames={chunk_frames}",
            "--cursor-file="
            + cursor_path_for_source(
                config_path,
                config,
                defaults,
                source,
                source_id,
            ),
            f"--ffmpeg={ffmpeg}",
        ]
    else:
        compatibility_session_id = str(
            value_from(source, defaults, "session", "sessionId", fallback="")
        )
        if not compatibility_session_id:
            raise ValueError(
                f"source {source_id} {LEGACY_SESSION_MODE} requires an "
                "explicit session/sessionId"
            )
        owner_id = str(value_from(source, defaults, "ownerId", "owner", fallback=""))
        tenant_id = str(value_from(source, defaults, "tenantId", "tenant", fallback=""))
        ingest_exe = value_from(
            source,
            defaults,
            "ingestExecutable",
            fallback=config.get("ingestExecutable"),
        )
        if not ingest_exe:
            default_name = (
                "ffmpeg_stream_ingest.exe"
                if os.name == "nt"
                else "ffmpeg_stream_ingest"
            )
            ingest_exe = str(
                (Path.cwd() / "cpp-engine" / "build" / default_name).resolve()
            )
        ingest_exe = resolve_executable(base_dir, str(ingest_exe))
        command = [
            ingest_exe,
            "--source",
            str(source_url),
            "--engine",
            engine,
            "--session",
            compatibility_session_id,
            "--source-id",
            source_id,
            "--sample-rate",
            str(sample_rate),
            "--channels",
            str(channels),
            "--chunk-frames",
            str(chunk_frames),
        ]
        session_config = resolve_path(
            base_dir,
            value_from(
                source,
                defaults,
                "sessionConfig",
                "sessionConfigPath",
            ),
        )
        append_option(command, "--session-config", session_config)
        append_option(command, "--owner-id", owner_id)
        append_option(command, "--tenant-id", tenant_id)
        if bool_from(source, defaults, "allowExistingSession", fallback=True):
            command.append("--allow-existing-session")
        if bool_from(source, defaults, "resumeFromEngine", fallback=True):
            command.append("--resume-from-engine")

    if mode != ACTIVE_PROJECT_MODE:
        # The project worker owns FFmpeg directly. Only the compatibility
        # bridge accepts its executable through --ffmpeg.
        append_option(command, "--ffmpeg", ffmpeg)
    option_appender = (
        append_internal_option
        if mode == ACTIVE_PROJECT_MODE
        else append_option
    )
    repeated_option_appender = (
        append_internal_repeated_option
        if mode == ACTIVE_PROJECT_MODE
        else append_repeated_option
    )
    option_appender(
        command,
        "--audio-filter",
        value_from(source, defaults, "audioFilter", "audio_filter"),
    )
    repeated_option_appender(
        command,
        "--ffmpeg-input-option",
        value_from(
            source,
            defaults,
            "ffmpegInputOptions",
            "ffmpeg_input_options",
        ),
    )
    max_chunks = value_from(source, defaults, "maxChunks")
    if max_chunks is not None and max_chunks != "":
        max_chunks = integer_option(
            max_chunks,
            "maxChunks",
            source_id,
            0,
        )
    max_restarts = value_from(source, defaults, "maxRestarts")
    if max_restarts is not None and max_restarts != "":
        max_restarts = integer_option(
            max_restarts,
            "maxRestarts",
            source_id,
            0,
        )
    restart_delay_ms = integer_option(
        value_from(
            source,
            defaults,
            "restartDelayMs",
            fallback=5000,
        ),
        "restartDelayMs",
        source_id,
        0,
    )
    start_sample = value_from(source, defaults, "startSample")
    if start_sample is not None and start_sample != "":
        start_sample = integer_option(
            start_sample,
            "startSample",
            source_id,
            0,
        )
    option_appender(
        command,
        "--max-chunks",
        max_chunks,
    )
    option_appender(
        command,
        "--max-restarts",
        max_restarts,
    )
    option_appender(
        command,
        "--restart-delay-ms",
        restart_delay_ms,
    )
    option_appender(
        command,
        "--start-sample",
        start_sample,
    )

    api_key = value_from(source, defaults, "apiKey", fallback=config.get("apiKey"))
    api_key_env = value_from(source, defaults, "apiKeyEnv", fallback=config.get("apiKeyEnv"))
    if api_key:
        option_appender(command, "--api-key", api_key)
    elif api_key_env:
        option_appender(command, "--api-key-env", api_key_env)

    if bool_from(source, defaults, "restart", fallback=True):
        command.append("--restart")
    if bool_from(source, defaults, "realtime", fallback=True):
        command.append("--realtime")

    metadata = {
        "targetMode": mode,
        "projectId": project_id,
        "acquisitionUnitId": acquisition_unit_id,
        "compatibilitySessionId": compatibility_session_id,
        "ownerId": owner_id,
        "tenantId": tenant_id,
        "engine": engine,
        "source": str(source_url),
        "sampleRateHz": sample_rate,
        "channelCount": channels,
        "chunkFrames": chunk_frames,
    }
    return source_id, command, metadata


class ProjectApiError(RuntimeError):
    def __init__(self, status: int, message: str, code: str = "") -> None:
        super().__init__(message)
        self.status = status
        self.code = code


class RejectRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(
        self,
        _request: urllib.request.Request,
        _file_pointer: Any,
        _code: int,
        _message: str,
        _headers: Any,
        _new_url: str,
    ) -> None:
        return None


def project_api_headers(api_key: str) -> dict[str, str]:
    headers = {"Accept": "application/json"}
    if api_key:
        headers["X-API-Key"] = api_key
    return headers


def request_project_json(
    method: str,
    url: str,
    api_key: str,
    body: bytes | None = None,
    content_type: str = "application/json",
    timeout_seconds: float = 30.0,
) -> dict[str, Any]:
    headers = project_api_headers(api_key)
    if body is not None:
        headers["Content-Type"] = content_type
    request = urllib.request.Request(
        url,
        data=body,
        headers=headers,
        method=method,
    )
    opener = urllib.request.build_opener(RejectRedirects())
    try:
        with opener.open(request, timeout=timeout_seconds) as response:
            encoded = response.read()
            status = response.status
    except urllib.error.HTTPError as error:
        encoded = error.read()
        detail: dict[str, Any] = {}
        try:
            detail = json.loads(encoded.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            pass
        raise ProjectApiError(
            error.code,
            str(detail.get("error") or f"engine returned HTTP {error.code}"),
            str(detail.get("code") or ""),
        ) from error
    except urllib.error.URLError as error:
        raise ProjectApiError(0, f"could not reach engine: {error.reason}") from error
    if status < 200 or status >= 300:
        raise ProjectApiError(status, f"engine returned HTTP {status}")
    try:
        parsed = json.loads(encoded.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProjectApiError(status, "engine returned invalid JSON") from error
    if not isinstance(parsed, dict):
        raise ProjectApiError(status, "engine returned a non-object JSON response")
    return parsed


def discover_active_acquisition(
    engine: str,
    project_id: str,
    acquisition_unit_id: str,
    sample_rate_hz: int,
    channel_count: int,
    api_key: str,
) -> int:
    body = request_project_json(
        "GET",
        f"{engine}/v1/projects/active/acquisitions",
        api_key,
    )
    if body.get("schemaVersion") != 1:
        raise ProjectApiError(
            500,
            "engine returned an unsupported Acquisition-list schema",
        )
    current_project_id = body.get("projectId")
    if current_project_id != project_id:
        raise ProjectApiError(
            409,
            "configured projectId is not the active project",
            "active_project_mismatch",
        )
    acquisitions = body.get("acquisitions")
    if not isinstance(acquisitions, list):
        raise ProjectApiError(500, "engine omitted the Acquisition list")
    matching_acquisitions = [
        item
        for item in acquisitions
        if isinstance(item, dict)
        and item.get("unitId") == acquisition_unit_id
        and item.get("typeId") == "pamguard.acquisition"
    ]
    if not matching_acquisitions:
        raise ProjectApiError(
            404,
            "configured acquisitionUnitId is not active",
            "acquisition_not_found",
        )
    if len(matching_acquisitions) != 1:
        raise ProjectApiError(
            500,
            "engine returned duplicate Acquisition unit identities",
        )
    acquisition = matching_acquisitions[0]
    capture_running = acquisition.get("captureRunning")
    if not isinstance(capture_running, bool):
        raise ProjectApiError(
            500,
            "engine returned invalid Acquisition capture state",
        )
    if capture_running:
        raise ProjectApiError(
            409,
            "the target Acquisition already has host capture running",
            "acquisition_writer_conflict",
        )
    actual_rate = acquisition.get("sampleRateHz")
    actual_channels = acquisition.get("channelCount")
    if (
        not isinstance(actual_rate, (int, float))
        or isinstance(actual_rate, bool)
        or not math.isfinite(float(actual_rate))
        or round(float(actual_rate)) != sample_rate_hz
    ):
        raise ProjectApiError(
            409,
            f"configured sampleRateHz {sample_rate_hz} does not match "
            f"active Acquisition {actual_rate}",
            "acquisition_audio_shape_mismatch",
        )
    if (
        not isinstance(actual_channels, int)
        or isinstance(actual_channels, bool)
        or actual_channels != channel_count
    ):
        raise ProjectApiError(
            409,
            f"configured channels {channel_count} does not match active "
            f"Acquisition {actual_channels}",
            "acquisition_audio_shape_mismatch",
        )
    if body.get("runtimeRunning") is not True:
        raise ProjectApiError(
            409,
            "active project runtime is not running",
            "acquisition_runtime_unavailable",
        )
    working_revision = body.get("workingRevision")
    if (
        not isinstance(working_revision, int)
        or isinstance(working_revision, bool)
        or working_revision < 0
    ):
        raise ProjectApiError(500, "engine returned an invalid workingRevision")
    return working_revision


def post_project_pcm(
    engine: str,
    project_id: str,
    acquisition_unit_id: str,
    working_revision: int,
    start_sample: int,
    pcm: bytes,
    frame_count: int,
    api_key: str,
) -> dict[str, Any]:
    unit_path = urllib.parse.quote(acquisition_unit_id, safe="")
    query = urllib.parse.urlencode(
        {
            "expectedProjectId": project_id,
            "expectedWorkingRevision": working_revision,
            "startSample": start_sample,
            "timeMs": int(time.time() * 1000),
        }
    )
    body = request_project_json(
        "POST",
        f"{engine}/v1/projects/active/acquisitions/{unit_path}/pcm-f32le?{query}",
        api_key,
        pcm,
        "application/octet-stream",
    )
    returned_revision = body.get("workingRevision")
    returned_frames = body.get("inputFrames")
    returned_start = body.get("startSample")
    if (
        body.get("accepted") is not True
        or body.get("projectId") != project_id
        or body.get("acquisitionUnitId") != acquisition_unit_id
        or not isinstance(returned_revision, int)
        or isinstance(returned_revision, bool)
        or returned_revision != working_revision
        or not isinstance(returned_frames, int)
        or isinstance(returned_frames, bool)
        or returned_frames != frame_count
        or not isinstance(returned_start, int)
        or isinstance(returned_start, bool)
        or returned_start != start_sample
    ):
        raise ProjectApiError(
            500,
            "engine PCM response did not identify the exact Acquisition target",
            "invalid_pcm_response",
        )
    return body


def load_project_cursor(
    path: Path,
    project_id: str,
    acquisition_unit_id: str,
    working_revision: int,
    fallback_start_sample: int,
) -> int:
    if not path.is_file():
        return fallback_start_sample
    try:
        body = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(
            f"could not read a valid ingest cursor from {path}"
        ) from error
    if (
        not isinstance(body, dict)
        or body.get("schemaVersion") != 1
    ):
        raise RuntimeError(f"unsupported ingest cursor schema in {path}")
    if (
        body.get("projectId") != project_id
        or body.get("acquisitionUnitId") != acquisition_unit_id
        or body.get("workingRevision") != working_revision
    ):
        return fallback_start_sample
    next_sample = body.get("nextStartSample")
    if (
        not isinstance(next_sample, int)
        or isinstance(next_sample, bool)
    ):
        raise RuntimeError(f"invalid nextStartSample in ingest cursor {path}")
    return max(next_sample, fallback_start_sample)


def write_project_cursor(
    path: Path,
    project_id: str,
    acquisition_unit_id: str,
    working_revision: int,
    next_start_sample: int,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = {
        "schemaVersion": 1,
        "projectId": project_id,
        "acquisitionUnitId": acquisition_unit_id,
        "workingRevision": working_revision,
        "nextStartSample": next_start_sample,
    }
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(body, separators=(",", ":")))
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(path)


def ffmpeg_command_for_project_worker(args: argparse.Namespace) -> list[str]:
    command = [
        args.ffmpeg,
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
    ]
    if args.realtime:
        command.append("-re")
    command.extend(args.ffmpeg_input_option)
    command.extend(["-i", args.source, "-vn"])
    if args.audio_filter:
        command.extend(["-af", args.audio_filter])
    command.extend(
        [
            "-ac",
            str(args.channels),
            "-ar",
            str(args.sample_rate),
            "-f",
            "f32le",
            "pipe:1",
        ]
    )
    return command


def api_key_from_worker_args(args: argparse.Namespace) -> str:
    if args.api_key:
        return args.api_key
    if args.api_key_env:
        value = os.environ.get(args.api_key_env, "")
        if not value:
            raise ValueError(
                f"--api-key-env variable {args.api_key_env} is empty or missing"
            )
        return value
    return ""


def project_worker_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Internal active-project Acquisition PCM worker."
    )
    parser.add_argument("--project-worker", action="store_true")
    parser.add_argument("--source-id", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--project-id", required=True)
    parser.add_argument("--acquisition-unit-id", required=True)
    parser.add_argument("--sample-rate", type=int, required=True)
    parser.add_argument("--channels", type=int, required=True)
    parser.add_argument("--chunk-frames", type=int, required=True)
    parser.add_argument("--cursor-file", required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffmpeg-input-option", action="append", default=[])
    parser.add_argument("--audio-filter", default="")
    parser.add_argument("--max-chunks", type=int, default=0)
    parser.add_argument("--max-restarts", type=int, default=0)
    parser.add_argument("--restart-delay-ms", type=int, default=5000)
    parser.add_argument("--start-sample", type=int, default=0)
    parser.add_argument("--api-key", default="")
    parser.add_argument("--api-key-env", default="")
    parser.add_argument("--restart", action="store_true")
    parser.add_argument("--realtime", action="store_true")
    args = parser.parse_args(argv)

    require_source_id(args.source_id)
    args.engine = normalized_engine_url(args.engine)
    require_uuid_v4(args.project_id, "projectId", args.source_id)
    require_uuid_v4(
        args.acquisition_unit_id,
        "acquisitionUnitId",
        args.source_id,
    )
    if (
        args.sample_rate <= 0
        or args.channels <= 0
        or args.chunk_frames <= 0
        or args.max_chunks < 0
        or args.max_restarts < 0
        or args.restart_delay_ms < 0
        or args.start_sample < 0
    ):
        raise ValueError("project worker numeric options must be non-negative/positive")

    api_key = api_key_from_worker_args(args)
    cursor_path = Path(args.cursor_file)
    current_process: subprocess.Popen[bytes] | None = None
    stopping = False

    def stop_handler(_signum: int, _frame: Any) -> None:
        nonlocal stopping
        stopping = True
        if current_process is not None and current_process.poll() is None:
            current_process.terminate()

    signal.signal(signal.SIGINT, stop_handler)
    signal.signal(signal.SIGTERM, stop_handler)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, stop_handler)

    chunks_posted = 0
    ffmpeg_restarts = 0
    start_sample = args.start_sample
    while not stopping:
        working_revision = discover_active_acquisition(
            args.engine,
            args.project_id,
            args.acquisition_unit_id,
            args.sample_rate,
            args.channels,
            api_key,
        )
        start_sample = load_project_cursor(
            cursor_path,
            args.project_id,
            args.acquisition_unit_id,
            working_revision,
            args.start_sample,
        )
        command = ffmpeg_command_for_project_worker(args)
        print(
            f"[project-worker] {args.source_id} project={args.project_id} "
            f"acquisition={args.acquisition_unit_id} revision={working_revision} "
            f"startSample={start_sample}",
            flush=True,
        )
        current_process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            bufsize=0,
        )
        if current_process.stdout is None:
            current_process.terminate()
            raise RuntimeError("FFmpeg stdout pipe was not created")
        frame_bytes = args.channels * 4
        chunk_bytes = args.chunk_frames * frame_bytes
        pending = bytearray()
        try:
            while not stopping:
                encoded = current_process.stdout.read(chunk_bytes - len(pending))
                if encoded:
                    pending.extend(encoded)
                if len(pending) == chunk_bytes:
                    pcm = bytes(pending)
                    pending.clear()
                    post_project_pcm(
                        args.engine,
                        args.project_id,
                        args.acquisition_unit_id,
                        working_revision,
                        start_sample,
                        pcm,
                        args.chunk_frames,
                        api_key,
                    )
                    start_sample += args.chunk_frames
                    chunks_posted += 1
                    write_project_cursor(
                        cursor_path,
                        args.project_id,
                        args.acquisition_unit_id,
                        working_revision,
                        start_sample,
                    )
                    if args.max_chunks > 0 and chunks_posted >= args.max_chunks:
                        current_process.terminate()
                        current_process.wait(timeout=10)
                        return 0
                    continue
                if not encoded:
                    break

            whole_frames = len(pending) // frame_bytes
            if whole_frames > 0 and not stopping:
                whole_bytes = whole_frames * frame_bytes
                post_project_pcm(
                    args.engine,
                    args.project_id,
                    args.acquisition_unit_id,
                    working_revision,
                    start_sample,
                    bytes(pending[:whole_bytes]),
                    whole_frames,
                    api_key,
                )
                start_sample += whole_frames
                chunks_posted += 1
                write_project_cursor(
                    cursor_path,
                    args.project_id,
                    args.acquisition_unit_id,
                    working_revision,
                    start_sample,
                )
            if len(pending) % frame_bytes != 0 and not stopping:
                raise RuntimeError(
                    "FFmpeg ended with an incomplete interleaved f32le frame"
                )
            if args.max_chunks > 0 and chunks_posted >= args.max_chunks:
                return 0
        finally:
            if current_process.poll() is None:
                current_process.terminate()
            try:
                exit_code = current_process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                current_process.kill()
                exit_code = current_process.wait()
            current_process = None

        if stopping:
            return 0
        if not args.restart:
            return exit_code
        ffmpeg_restarts += 1
        if args.max_restarts > 0 and ffmpeg_restarts > args.max_restarts:
            return exit_code if exit_code != 0 else 1
        time.sleep(args.restart_delay_ms / 1000.0)
    return 0


def quote_command(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def source_for_status(source: str) -> str:
    """Keep source correlation useful without publishing URL credentials."""
    try:
        parsed = urllib.parse.urlsplit(source)
        if not parsed.scheme or not parsed.hostname:
            return source
        hostname = parsed.hostname
        if ":" in hostname and not hostname.startswith("["):
            hostname = f"[{hostname}]"
        netloc = hostname
        if parsed.port is not None:
            netloc += f":{parsed.port}"
        return urllib.parse.urlunsplit(
            (parsed.scheme, netloc, parsed.path, "", "")
        )
    except ValueError:
        return "<configured-source>"


def redacted_command(command: list[str]) -> list[str]:
    redacted: list[str] = []
    redact_next = False
    sanitize_source_next = False
    for part in command:
        if redact_next:
            redacted.append("<redacted>")
            redact_next = False
            continue
        if sanitize_source_next:
            redacted.append(source_for_status(part))
            sanitize_source_next = False
            continue
        if part.startswith("--api-key="):
            redacted.append("--api-key=<redacted>")
            continue
        if part.startswith("--source="):
            redacted.append(
                "--source=" + source_for_status(part.split("=", 1)[1])
            )
            continue
        redacted.append(part)
        if part == "--api-key":
            redact_next = True
        elif part == "--source":
            sanitize_source_next = True
    return redacted


def quote_display_command(command: list[str]) -> str:
    return quote_command(redacted_command(command))


def launch_worker(state: WorkerState) -> None:
    print(
        f"[supervisor] starting {state.source_id}: "
        f"{quote_display_command(state.command)}",
        flush=True,
    )
    popen_options: dict[str, Any] = {}
    if os.name == "nt" and state.target_mode == ACTIVE_PROJECT_MODE:
        # Popen.terminate() is an abrupt TerminateProcess on Windows. A process
        # group lets normal supervisor shutdown deliver CTRL_BREAK so the
        # Python worker can terminate and reap its FFmpeg child first.
        popen_options["creationflags"] = (
            subprocess.CREATE_NEW_PROCESS_GROUP
        )
    state.process = subprocess.Popen(state.command, **popen_options)
    state.last_start_unix_ms = int(time.time() * 1000)
    state.last_exit_code = None
    state.next_start_time = 0.0


def stop_workers(workers: list[WorkerState]) -> None:
    for worker in workers:
        if worker.process and worker.process.poll() is None:
            print(f"[supervisor] stopping {worker.source_id}", flush=True)
            used_graceful_signal = False
            if (
                os.name == "nt"
                and worker.target_mode == ACTIVE_PROJECT_MODE
                and hasattr(signal, "CTRL_BREAK_EVENT")
            ):
                try:
                    worker.process.send_signal(signal.CTRL_BREAK_EVENT)
                    used_graceful_signal = True
                except OSError:
                    pass
            if not used_graceful_signal:
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
        "targetMode": worker.target_mode,
        "projectId": worker.project_id or None,
        "acquisitionUnitId": worker.acquisition_unit_id or None,
        "compatibilitySessionId": worker.compatibility_session_id or None,
        "ownerId": worker.owner_id or None,
        "tenantId": worker.tenant_id or None,
        "engine": worker.engine,
        "source": source_for_status(worker.source_url),
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
        "nextStartUnixMs": (
            int(worker.next_start_time * 1000)
            if worker.next_start_time > now
            else None
        ),
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
        "schemaVersion": 3,
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
                    print(
                        f"[supervisor] {worker.source_id} reached max "
                        "worker restarts",
                        flush=True,
                    )
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
    if "--project-worker" in argv:
        return project_worker_main(argv)

    parser = argparse.ArgumentParser(
        description=(
            "Supervise active-project PAMGuard Acquisition FFmpeg workers. "
            "Legacy sessions require explicit legacy-session-compatibility mode."
        )
    )
    parser.add_argument("--config", required=True, help="Path to ingest supervisor JSON config.")
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Validate enabled sources without launching workers.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print worker commands without launching them.",
    )
    args = parser.parse_args(argv)

    config_path = Path(args.config).resolve()
    config = load_config(config_path)
    if not isinstance(config, dict):
        raise ValueError("ingest supervisor config must be a JSON object")
    config_mode = str(config.get("mode", ACTIVE_PROJECT_MODE))
    schema_version = config.get("schemaVersion")
    if config_mode == ACTIVE_PROJECT_MODE and schema_version != 2:
        raise ValueError(
            "active-project supervisor configs require schemaVersion 2"
        )
    if config_mode == LEGACY_SESSION_MODE and schema_version != 1:
        raise ValueError(
            "legacy-session-compatibility configs require schemaVersion 1"
        )
    if config_mode not in {ACTIVE_PROJECT_MODE, LEGACY_SESSION_MODE}:
        raise ValueError(
            f"mode must be {ACTIVE_PROJECT_MODE} or {LEGACY_SESSION_MODE}"
        )
    defaults = config.get("defaults", {})
    if not isinstance(defaults, dict):
        raise ValueError("defaults must be an object")
    sources = config.get("sources", [])
    if not isinstance(sources, list):
        raise ValueError("sources must be an array")
    workers: list[WorkerState] = []
    source_ids: set[str] = set()
    acquisition_targets: set[tuple[str, str, str]] = set()
    for source in sources:
        if not isinstance(source, dict):
            raise ValueError("each source must be an object")
        enabled = source.get("enabled", True)
        if not isinstance(enabled, bool):
            raise ValueError("source enabled must be a JSON boolean")
        if not enabled:
            print(
                f"[supervisor] disabled source skipped: "
                f"{source.get('id') or '<missing-id>'}",
                flush=True,
            )
            continue
        source_id, command, metadata = command_for_source(config_path, config, source)
        if metadata["targetMode"] != config_mode:
            raise ValueError(
                f"source {source_id} mode must match top-level mode "
                f"{config_mode}"
            )
        if source_id in source_ids:
            raise ValueError(
                f"enabled source id {source_id} is configured more than once"
            )
        source_ids.add(source_id)
        if metadata["targetMode"] == ACTIVE_PROJECT_MODE:
            target = (
                metadata["engine"],
                metadata["projectId"],
                metadata["acquisitionUnitId"],
            )
            if target in acquisition_targets:
                raise ValueError(
                    "multiple enabled workers target the same active-project "
                    f"Acquisition: {metadata['acquisitionUnitId']}"
                )
            acquisition_targets.add(target)
        workers.append(WorkerState(
            source_id=source_id,
            command=command,
            target_mode=metadata["targetMode"],
            project_id=metadata["projectId"],
            acquisition_unit_id=metadata["acquisitionUnitId"],
            compatibility_session_id=metadata["compatibilitySessionId"],
            owner_id=metadata["ownerId"],
            tenant_id=metadata["tenantId"],
            engine=metadata["engine"],
            source_url=metadata["source"],
            sample_rate_hz=metadata["sampleRateHz"],
            channel_count=metadata["channelCount"],
            chunk_frames=metadata["chunkFrames"],
        ))

    raw_restart_delay = value_from(
        {},
        defaults,
        "workerRestartDelaySeconds",
        fallback=config.get("workerRestartDelaySeconds", 5.0),
    )
    raw_max_restarts = value_from(
        {},
        defaults,
        "maxWorkerRestarts",
        fallback=config.get("maxWorkerRestarts", 0),
    )
    raw_status_interval = value_from(
        {},
        defaults,
        "statusIntervalSeconds",
        fallback=config.get("statusIntervalSeconds", 5.0),
    )
    if isinstance(raw_restart_delay, bool) or isinstance(
        raw_status_interval, bool
    ):
        raise ValueError(
            "worker restart and status intervals must be numeric"
        )
    try:
        restart_delay = float(raw_restart_delay)
        max_restarts = integer_option(
            raw_max_restarts,
            "maxWorkerRestarts",
            "supervisor",
            0,
        )
        status_interval = float(raw_status_interval)
    except (TypeError, ValueError, OverflowError) as error:
        raise ValueError(
            "worker restart and status options must be numeric"
        ) from error
    if (
        not math.isfinite(restart_delay)
        or restart_delay < 0
        or max_restarts < 0
        or not math.isfinite(status_interval)
        or status_interval <= 0
    ):
        raise ValueError(
            "workerRestartDelaySeconds/maxWorkerRestarts must be "
            "non-negative and statusIntervalSeconds must be positive"
        )
    raw_status_path = value_from(
        {},
        defaults,
        "statusFile",
        fallback=config.get("statusFile"),
    )
    status_path = None
    if raw_status_path:
        candidate = Path(str(raw_status_path))
        status_path = (
            candidate
            if candidate.is_absolute()
            else (config_path.parent / candidate).resolve()
        )

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

    return supervise(workers, restart_delay, max_restarts, status_path, status_interval)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
