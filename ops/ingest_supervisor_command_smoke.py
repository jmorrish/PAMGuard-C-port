#!/usr/bin/env python3
"""Smoke-test project-authoritative supervisor command/config expansion."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import sys
import tempfile
from pathlib import Path


PROJECT_ID = "11111111-1111-4111-8111-111111111111"
ACQUISITION_A = "22222222-2222-4222-8222-222222222222"
ACQUISITION_B = "33333333-3333-4333-8333-333333333333"


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
    prefix = option + "="
    for part in command:
        if part.startswith(prefix):
            return part[len(prefix):]
    try:
        index = command.index(option)
    except ValueError as exc:
        raise AssertionError(f"missing option {option}") from exc
    try:
        return command[index + 1]
    except IndexError as exc:
        raise AssertionError(f"missing value for {option}") from exc


def expect_value_error(action, message_fragment: str) -> None:
    try:
        action()
    except ValueError as error:
        assert message_fragment in str(error), str(error)
        return
    raise AssertionError(f"expected ValueError containing {message_fragment!r}")


def main() -> int:
    supervisor = load_supervisor_module()
    previous_api_key = os.environ.get("PAMGUARD_SMOKE_API_KEY")
    os.environ["PAMGUARD_SMOKE_API_KEY"] = "super-secret"
    try:
        with tempfile.TemporaryDirectory(
            prefix="pamguard-project-ingest-supervisor-"
        ) as root:
            root_path = Path(root)
            config_path = root_path / "sources.json"
            config = {
                "schemaVersion": 2,
                # Omitting mode is intentional: active-project is the safe
                # default and cannot silently fall back to a session.
                "projectId": PROJECT_ID,
                "engine": "http://engine-default",
                "ffmpeg": "ffmpeg-custom",
                "apiKeyEnv": "PAMGUARD_SMOKE_API_KEY",
                "cursorDirectory": "cursors",
                "defaults": {
                    "sampleRateHz": 96000,
                    "channels": 4,
                    "chunkFrames": 2048,
                    "restart": True,
                    "restartDelayMs": 2500,
                    "realtime": True,
                    "audioFilter": "pan=stereo|c0=c0|c1=c1",
                },
                "sources": [
                    {
                        "enabled": False,
                        "id": "disabled-source",
                        "acquisitionUnitId": ACQUISITION_A,
                        "source": "http://disabled.example/live.mp3",
                    },
                    {
                        "id": "station-001",
                        "acquisitionUnitId": ACQUISITION_A,
                        "source": "http://icecast.example/live.mp3",
                        "channels": 2,
                        "ffmpegInputOptions": ["-fflags", "nobuffer"],
                        "maxRestarts": 3,
                        "startSample": 1024,
                    },
                    {
                        "id": "station-002",
                        "acquisitionUnitId": ACQUISITION_B,
                        "sourceUrl": "udp://239.0.0.1:1234",
                        "apiKey": "inline-secret",
                        "restart": False,
                        "realtime": False,
                    },
                ],
            }
            config_path.write_text(json.dumps(config), encoding="utf-8")

            source_id, command, metadata = supervisor.command_for_source(
                config_path, config, config["sources"][1]
            )
            assert source_id == "station-001"
            assert Path(command[0]).resolve() == Path(sys.executable).resolve()
            assert Path(command[1]).resolve() == (
                Path(__file__).with_name("ingest_supervisor.py").resolve()
            )
            assert "--project-worker" in command
            assert option_value(command, "--source") == (
                "http://icecast.example/live.mp3"
            )
            assert option_value(command, "--engine") == "http://engine-default"
            assert option_value(command, "--project-id") == PROJECT_ID
            assert option_value(command, "--acquisition-unit-id") == ACQUISITION_A
            assert option_value(command, "--source-id") == "station-001"
            assert option_value(command, "--sample-rate") == "96000"
            assert option_value(command, "--channels") == "2"
            assert option_value(command, "--chunk-frames") == "2048"
            assert option_value(command, "--cursor-file") == str(
                (root_path / "cursors" / "station-001.cursor.json").resolve()
            )
            assert option_value(command, "--ffmpeg") == "ffmpeg-custom"
            assert option_value(command, "--audio-filter") == (
                "pan=stereo|c0=c0|c1=c1"
            )
            assert "--ffmpeg-input-option=-fflags" in command
            assert "--ffmpeg-input-option=nobuffer" in command
            assert option_value(command, "--max-restarts") == "3"
            assert option_value(command, "--restart-delay-ms") == "2500"
            assert option_value(command, "--start-sample") == "1024"
            assert option_value(command, "--api-key-env") == (
                "PAMGUARD_SMOKE_API_KEY"
            )
            assert "--api-key" not in command
            assert "--restart" in command
            assert "--realtime" in command
            assert "--session" not in command
            assert "--module" not in command
            assert "--session-config" not in command
            assert metadata["targetMode"] == supervisor.ACTIVE_PROJECT_MODE
            assert metadata["projectId"] == PROJECT_ID
            assert metadata["acquisitionUnitId"] == ACQUISITION_A
            assert metadata["compatibilitySessionId"] == ""
            assert metadata["channelCount"] == 2

            _, command_two, metadata_two = supervisor.command_for_source(
                config_path, config, config["sources"][2]
            )
            assert "--restart" not in command_two
            assert "--realtime" not in command_two
            assert option_value(command_two, "--api-key") == "inline-secret"
            assert metadata_two["targetMode"] == supervisor.ACTIVE_PROJECT_MODE
            assert metadata_two["acquisitionUnitId"] == ACQUISITION_B
            assert metadata_two["channelCount"] == 4
            display = supervisor.quote_display_command(command_two)
            assert "inline-secret" not in display
            assert "<redacted>" in display

            credential_source = dict(config["sources"][2])
            credential_source["sourceUrl"] = (
                "https://user:source-secret@example.invalid/live"
                "?token=query-secret"
            )
            _, credential_command, _ = supervisor.command_for_source(
                config_path,
                config,
                credential_source,
            )
            credential_display = supervisor.quote_display_command(
                credential_command
            )
            assert "source-secret" not in credential_display
            assert "query-secret" not in credential_display
            assert "https://example.invalid/live" in credential_display

            invalid_session_source = dict(config["sources"][1])
            invalid_session_source["sessionId"] = "must-not-fallback"
            expect_value_error(
                lambda: supervisor.command_for_source(
                    config_path, config, invalid_session_source
                ),
                "rejects legacy session fields",
            )
            invalid_runtime_source = dict(config["sources"][1])
            invalid_runtime_source["moduleId"] = "generated-runtime-node"
            expect_value_error(
                lambda: supervisor.command_for_source(
                    config_path, config, invalid_runtime_source
                ),
                "rejects legacy session fields",
            )
            invalid_unit = dict(config["sources"][1])
            invalid_unit["acquisitionUnitId"] = "runtime-node"
            expect_value_error(
                lambda: supervisor.command_for_source(
                    config_path, config, invalid_unit
                ),
                "lowercase UUIDv4",
            )

            duplicate_config = dict(config)
            duplicate_config["sources"] = [
                dict(config["sources"][1]),
                {
                    **dict(config["sources"][1]),
                    "id": "duplicate-target",
                },
            ]
            duplicate_path = root_path / "duplicate-target.json"
            duplicate_path.write_text(
                json.dumps(duplicate_config),
                encoding="utf-8",
            )
            expect_value_error(
                lambda: supervisor.main(
                    ["--config", str(duplicate_path), "--validate"]
                ),
                "same active-project Acquisition",
            )

            legacy_session_config = root_path / "legacy-session.json"
            legacy_session_config.write_text("{}", encoding="utf-8")
            legacy_config = {
                "schemaVersion": 1,
                "mode": supervisor.LEGACY_SESSION_MODE,
                "engine": "http://legacy-engine",
                "ingestExecutable": "bin/ffmpeg_stream_ingest.exe",
                "defaults": {
                    "sampleRateHz": 48000,
                    "channels": 2,
                    "allowExistingSession": True,
                    "resumeFromEngine": True,
                },
                "sources": [
                    {
                        "id": "legacy-station",
                        "sessionId": "legacy-session",
                        "source": "http://legacy.example/live.mp3",
                        "sessionConfig": "legacy-session.json",
                        "ownerId": "legacy-owner",
                        "tenantId": "legacy-tenant",
                    }
                ],
            }
            _, legacy_command, legacy_metadata = supervisor.command_for_source(
                config_path, legacy_config, legacy_config["sources"][0]
            )
            assert option_value(legacy_command, "--session") == "legacy-session"
            assert option_value(legacy_command, "--session-config") == str(
                legacy_session_config.resolve()
            )
            assert "--allow-existing-session" in legacy_command
            assert "--resume-from-engine" in legacy_command
            assert "--project-worker" not in legacy_command
            assert legacy_metadata["targetMode"] == supervisor.LEGACY_SESSION_MODE
            assert legacy_metadata["compatibilitySessionId"] == "legacy-session"
            assert legacy_metadata["projectId"] == ""

            implicit_legacy = dict(legacy_config)
            implicit_legacy.pop("mode")
            expect_value_error(
                lambda: supervisor.command_for_source(
                    config_path,
                    implicit_legacy,
                    implicit_legacy["sources"][0],
                ),
                "rejects legacy session fields",
            )

            validate_output = io.StringIO()
            with contextlib.redirect_stdout(validate_output):
                validate_code = supervisor.main(
                    ["--config", str(config_path), "--validate"]
                )
            assert validate_code == 0
            assert "validated 2 enabled source(s)" in validate_output.getvalue()

            dry_run_output = io.StringIO()
            with contextlib.redirect_stdout(dry_run_output):
                dry_run_code = supervisor.main(
                    ["--config", str(config_path), "--dry-run"]
                )
            dry_run_text = dry_run_output.getvalue()
            assert dry_run_code == 0
            assert "station-001" in dry_run_text
            assert "station-002" in dry_run_text
            assert "disabled-source" in dry_run_text
            assert "super-secret" not in dry_run_text
            assert "inline-secret" not in dry_run_text
            assert "<redacted>" in dry_run_text
            assert "--project-worker" in dry_run_text
            assert "--session" not in dry_run_text

            repository_root = Path(__file__).resolve().parents[1]
            production_path = (
                repository_root
                / "platform"
                / "ingest-sources.example.json"
            )
            production_config = supervisor.load_config(production_path)
            assert production_config["schemaVersion"] == 2
            assert production_config["mode"] == supervisor.ACTIVE_PROJECT_MODE
            for example_source in production_config["sources"]:
                _, example_command, example_metadata = (
                    supervisor.command_for_source(
                        production_path,
                        production_config,
                        example_source,
                    )
                )
                assert "--project-worker" in example_command
                assert "--session" not in example_command
                assert (
                    example_metadata["targetMode"]
                    == supervisor.ACTIVE_PROJECT_MODE
                )

            compatibility_path = (
                repository_root
                / "platform"
                / "ingest-sources.legacy-session-compat.example.json"
            )
            compatibility_config = supervisor.load_config(
                compatibility_path
            )
            assert compatibility_config["schemaVersion"] == 1
            assert (
                compatibility_config["mode"]
                == supervisor.LEGACY_SESSION_MODE
            )
            _, compatibility_command, compatibility_metadata = (
                supervisor.command_for_source(
                    compatibility_path,
                    compatibility_config,
                    compatibility_config["sources"][0],
                )
            )
            assert "--session" in compatibility_command
            assert "--project-worker" not in compatibility_command
            assert (
                compatibility_metadata["targetMode"]
                == supervisor.LEGACY_SESSION_MODE
            )
    finally:
        if previous_api_key is None:
            os.environ.pop("PAMGUARD_SMOKE_API_KEY", None)
        else:
            os.environ["PAMGUARD_SMOKE_API_KEY"] = previous_api_key

    print("ingest supervisor command smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
