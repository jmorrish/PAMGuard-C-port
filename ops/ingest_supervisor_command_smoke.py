#!/usr/bin/env python3
"""Smoke-test ingest supervisor manifest expansion and redacted preflight output."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import sys
import tempfile
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


def option_value(command: list[str], option: str) -> str:
    try:
        index = command.index(option)
    except ValueError as exc:
        raise AssertionError(f"missing option {option}") from exc
    try:
        return command[index + 1]
    except IndexError as exc:
        raise AssertionError(f"missing value for {option}") from exc


def main() -> int:
    supervisor = load_supervisor_module()
    previous_api_key = os.environ.get("PAMGUARD_SMOKE_API_KEY")
    os.environ["PAMGUARD_SMOKE_API_KEY"] = "super-secret"
    try:
        with tempfile.TemporaryDirectory(prefix="pamguard-ingest-supervisor-") as root:
            root_path = Path(root)
            config_path = root_path / "sources.json"
            session_config_path = root_path / "array-session.json"
            session_config_path.write_text("{}", encoding="utf-8")
            config = {
                "engine": "http://engine-default",
                "ingestExecutable": "bin/ffmpeg_stream_ingest.exe",
                "ffmpeg": "ffmpeg-custom",
                "apiKeyEnv": "PAMGUARD_SMOKE_API_KEY",
                "defaults": {
                    "sampleRateHz": 96000,
                    "channels": 4,
                    "chunkFrames": 2048,
                    "restart": True,
                    "restartDelayMs": 2500,
                    "allowExistingSession": True,
                    "resumeFromEngine": True,
                    "realtime": True,
                    "audioFilter": "pan=stereo|c0=c0|c1=c1",
                    "ownerId": "owner-default",
                    "tenantId": "tenant-default",
                },
                "sources": [
                    {
                        "enabled": False,
                        "id": "disabled-source",
                        "source": "http://disabled.example/live.mp3",
                    },
                    {
                        "id": "station-001",
                        "session": "session-001",
                        "source": "http://icecast.example/live.mp3",
                        "ownerId": "owner-a",
                        "tenantId": "tenant-a",
                        "sessionConfig": "array-session.json",
                        "channels": 2,
                        "ffmpegInputOptions": ["-fflags", "nobuffer"],
                        "maxRestarts": 3,
                        "startSample": 1024,
                    },
                    {
                        "id": "station-002",
                        "sessionId": "session-002",
                        "sourceUrl": "udp://239.0.0.1:1234",
                        "apiKey": "inline-secret",
                        "restart": False,
                        "realtime": False,
                    },
                ],
            }
            config_path.write_text(json.dumps(config), encoding="utf-8")

            source_id, command, metadata = supervisor.command_for_source(config_path, config, config["sources"][1])
            assert source_id == "station-001"
            assert Path(command[0]) == (root_path / "bin" / "ffmpeg_stream_ingest.exe").resolve()
            assert option_value(command, "--source") == "http://icecast.example/live.mp3"
            assert option_value(command, "--engine") == "http://engine-default"
            assert option_value(command, "--session") == "session-001"
            assert option_value(command, "--source-id") == "station-001"
            assert option_value(command, "--sample-rate") == "96000"
            assert option_value(command, "--channels") == "2"
            assert option_value(command, "--chunk-frames") == "2048"
            assert option_value(command, "--session-config") == str(session_config_path.resolve())
            assert option_value(command, "--owner-id") == "owner-a"
            assert option_value(command, "--tenant-id") == "tenant-a"
            assert option_value(command, "--ffmpeg") == "ffmpeg-custom"
            assert option_value(command, "--audio-filter") == "pan=stereo|c0=c0|c1=c1"
            assert command.count("--ffmpeg-input-option") == 2
            first_input_option = command.index("--ffmpeg-input-option")
            assert command[first_input_option + 1] == "-fflags"
            second_input_option = command.index("--ffmpeg-input-option", first_input_option + 1)
            assert command[second_input_option + 1] == "nobuffer"
            assert option_value(command, "--max-restarts") == "3"
            assert option_value(command, "--restart-delay-ms") == "2500"
            assert option_value(command, "--start-sample") == "1024"
            assert option_value(command, "--api-key-env") == "PAMGUARD_SMOKE_API_KEY"
            assert "--api-key" not in command
            assert "--restart" in command
            assert "--allow-existing-session" in command
            assert "--resume-from-engine" in command
            assert "--realtime" in command
            assert metadata["ownerId"] == "owner-a"
            assert metadata["tenantId"] == "tenant-a"
            assert metadata["channelCount"] == 2

            _, command_two, metadata_two = supervisor.command_for_source(config_path, config, config["sources"][2])
            assert "--restart" not in command_two
            assert "--realtime" not in command_two
            assert "--allow-existing-session" in command_two
            assert "--resume-from-engine" in command_two
            assert option_value(command_two, "--api-key") == "inline-secret"
            assert metadata_two["ownerId"] == "owner-default"
            assert metadata_two["tenantId"] == "tenant-default"
            assert metadata_two["channelCount"] == 4

            display = supervisor.quote_display_command(command_two)
            assert "inline-secret" not in display
            assert "<redacted>" in display

            validate_output = io.StringIO()
            with contextlib.redirect_stdout(validate_output):
                validate_code = supervisor.main(["--config", str(config_path), "--validate"])
            assert validate_code == 0
            assert "validated 2 enabled source(s)" in validate_output.getvalue()

            dry_run_output = io.StringIO()
            with contextlib.redirect_stdout(dry_run_output):
                dry_run_code = supervisor.main(["--config", str(config_path), "--dry-run"])
            dry_run_text = dry_run_output.getvalue()
            assert dry_run_code == 0
            assert "station-001" in dry_run_text
            assert "station-002" in dry_run_text
            assert "disabled-source" in dry_run_text
            assert "super-secret" not in dry_run_text
            assert "inline-secret" not in dry_run_text
            assert "<redacted>" in dry_run_text
    finally:
        if previous_api_key is None:
            os.environ.pop("PAMGUARD_SMOKE_API_KEY", None)
        else:
            os.environ["PAMGUARD_SMOKE_API_KEY"] = previous_api_key

    print("ingest supervisor command smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
