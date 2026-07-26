#!/usr/bin/env python3
"""Exercise active-project discovery, stable PCM POSTs, and cursor fencing."""

from __future__ import annotations

import argparse
import io
import importlib.util
import json
import sys
import tempfile
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


PROJECT_ID = "11111111-1111-4111-8111-111111111111"
ACQUISITION_ID = "22222222-2222-4222-8222-222222222222"


def load_supervisor_module():
    path = Path(__file__).with_name("ingest_supervisor.py")
    spec = importlib.util.spec_from_file_location("ingest_supervisor", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load ingest_supervisor.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class ProjectApiHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    requests: list[dict[str, object]] = []
    runtime_running = True
    capture_running = False
    redirect_get = False

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def send_json(self, status: int, body: dict[str, object]) -> None:
        encoded = json.dumps(body, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:
        ProjectApiHandler.requests.append(
            {
                "method": "GET",
                "path": self.path,
                "apiKey": self.headers.get("X-API-Key"),
            }
        )
        if self.redirect_get:
            self.send_response(307)
            self.send_header("Location", "/credential-leak")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            return
        if self.path != "/v1/projects/active/acquisitions":
            self.send_json(404, {"error": "not found", "code": "not_found"})
            return
        self.send_json(
            200,
            {
                "schemaVersion": 1,
                "projectId": PROJECT_ID,
                "workingRevision": 7,
                "runtimeRunning": self.runtime_running,
                "captureEnabled": False,
                "urlCaptureCapability": "disabled",
                "audioDeviceCaptureCapability": "disabled",
                "acquisitions": [
                    {
                        "unitId": ACQUISITION_ID,
                        "name": "Hydrophone input",
                        "typeId": "pamguard.acquisition",
                        "sampleRateHz": 48000.0,
                        "channelCount": 2,
                        "hostBindingRevision": None,
                        "captureRunning": self.capture_running,
                    }
                ],
            },
        )

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        pcm = self.rfile.read(length)
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        ProjectApiHandler.requests.append(
            {
                "method": "POST",
                "path": parsed.path,
                "query": query,
                "apiKey": self.headers.get("X-API-Key"),
                "contentType": self.headers.get("Content-Type"),
                "pcm": pcm,
            }
        )
        expected_path = (
            f"/v1/projects/active/acquisitions/{ACQUISITION_ID}/pcm-f32le"
        )
        if parsed.path != expected_path:
            self.send_json(404, {"error": "not found", "code": "not_found"})
            return
        if query.get("expectedProjectId") != [PROJECT_ID]:
            self.send_json(
                409,
                {
                    "error": "wrong project",
                    "code": "active_project_mismatch",
                },
            )
            return
        if query.get("expectedWorkingRevision") != ["7"]:
            self.send_json(
                409,
                {
                    "error": "stale",
                    "code": "working_revision_conflict",
                },
            )
            return
        start_sample = int(query["startSample"][0])
        frame_count = len(pcm) // (2 * 4)
        self.send_json(
            202,
            {
                "accepted": True,
                "projectId": PROJECT_ID,
                "acquisitionUnitId": ACQUISITION_ID,
                "workingRevision": 7,
                "inputFrames": frame_count,
                "startSample": start_sample,
            },
        )


class FakeFfmpegProcess:
    next_stdout = b""
    last_command: list[str] = []

    def __init__(self, command: list[str], **_kwargs: object) -> None:
        FakeFfmpegProcess.last_command = command
        self.stdout = io.BytesIO(FakeFfmpegProcess.next_stdout)
        self.pid = 4321
        self._exit_code: int | None = None

    def poll(self) -> int | None:
        return self._exit_code

    def terminate(self) -> None:
        self._exit_code = 0

    def kill(self) -> None:
        self._exit_code = -9

    def wait(self, timeout: float | None = None) -> int:
        del timeout
        if self._exit_code is None:
            self._exit_code = 0
        return self._exit_code


def expect_project_error(action, status: int, code: str) -> None:
    try:
        action()
    except Exception as error:
        assert getattr(error, "status", None) == status, repr(error)
        assert getattr(error, "code", None) == code, repr(error)
        return
    raise AssertionError("expected ProjectApiError")


def main() -> int:
    supervisor = load_supervisor_module()
    server = ThreadingHTTPServer(("127.0.0.1", 0), ProjectApiHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    engine = f"http://127.0.0.1:{server.server_port}"
    try:
        revision = supervisor.discover_active_acquisition(
            engine,
            PROJECT_ID,
            ACQUISITION_ID,
            48000,
            2,
            "test-secret",
        )
        assert revision == 7
        assert ProjectApiHandler.requests[-1] == {
            "method": "GET",
            "path": "/v1/projects/active/acquisitions",
            "apiKey": "test-secret",
        }

        expect_project_error(
            lambda: supervisor.discover_active_acquisition(
                engine,
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                ACQUISITION_ID,
                48000,
                2,
                "test-secret",
            ),
            409,
            "active_project_mismatch",
        )
        expect_project_error(
            lambda: supervisor.discover_active_acquisition(
                engine,
                PROJECT_ID,
                ACQUISITION_ID,
                96000,
                2,
                "test-secret",
            ),
            409,
            "acquisition_audio_shape_mismatch",
        )
        ProjectApiHandler.runtime_running = False
        expect_project_error(
            lambda: supervisor.discover_active_acquisition(
                engine,
                PROJECT_ID,
                ACQUISITION_ID,
                48000,
                2,
                "test-secret",
            ),
            409,
            "acquisition_runtime_unavailable",
        )
        ProjectApiHandler.runtime_running = True
        ProjectApiHandler.capture_running = True
        expect_project_error(
            lambda: supervisor.discover_active_acquisition(
                engine,
                PROJECT_ID,
                ACQUISITION_ID,
                48000,
                2,
                "test-secret",
            ),
            409,
            "acquisition_writer_conflict",
        )
        ProjectApiHandler.capture_running = False

        request_count = len(ProjectApiHandler.requests)
        ProjectApiHandler.redirect_get = True
        expect_project_error(
            lambda: supervisor.discover_active_acquisition(
                engine,
                PROJECT_ID,
                ACQUISITION_ID,
                48000,
                2,
                "must-not-follow",
            ),
            307,
            "",
        )
        ProjectApiHandler.redirect_get = False
        assert len(ProjectApiHandler.requests) == request_count + 1
        assert ProjectApiHandler.requests[-1]["path"] == (
            "/v1/projects/active/acquisitions"
        )

        pcm = bytes(range(32))
        accepted = supervisor.post_project_pcm(
            engine,
            PROJECT_ID,
            ACQUISITION_ID,
            7,
            128,
            pcm,
            4,
            "test-secret",
        )
        assert accepted["inputFrames"] == 4
        posted = ProjectApiHandler.requests[-1]
        assert posted["method"] == "POST"
        assert posted["path"] == (
            f"/v1/projects/active/acquisitions/{ACQUISITION_ID}/pcm-f32le"
        )
        assert posted["query"]["expectedProjectId"] == [PROJECT_ID]
        assert posted["query"]["expectedWorkingRevision"] == ["7"]
        assert posted["query"]["startSample"] == ["128"]
        assert posted["apiKey"] == "test-secret"
        assert posted["contentType"] == "application/octet-stream"
        assert posted["pcm"] == pcm
        transcript = repr(ProjectApiHandler.requests)
        assert "/sessions" not in transcript
        assert "moduleId" not in transcript
        assert "runtimeNodeId" not in transcript

        with tempfile.TemporaryDirectory(
            prefix="pamguard-project-ingest-cursor-"
        ) as root:
            cursor = Path(root) / "station.cursor.json"
            assert (
                supervisor.load_project_cursor(
                    cursor,
                    PROJECT_ID,
                    ACQUISITION_ID,
                    7,
                    12,
                )
                == 12
            )
            cursor.write_text("{not-json", encoding="utf-8")
            try:
                supervisor.load_project_cursor(
                    cursor,
                    PROJECT_ID,
                    ACQUISITION_ID,
                    8,
                    12,
                )
            except RuntimeError as error:
                assert "valid ingest cursor" in str(error)
            else:
                raise AssertionError("corrupt cursor must fail closed")
            supervisor.write_project_cursor(
                cursor,
                PROJECT_ID,
                ACQUISITION_ID,
                7,
                256,
            )
            assert (
                supervisor.load_project_cursor(
                    cursor,
                    PROJECT_ID,
                    ACQUISITION_ID,
                    7,
                    12,
                )
                == 256
            )
            # A project reconfiguration fences the old cursor.
            assert (
                supervisor.load_project_cursor(
                    cursor,
                    PROJECT_ID,
                    ACQUISITION_ID,
                    8,
                    12,
                )
                == 12
            )

        args = argparse.Namespace(
            ffmpeg="ffmpeg",
            realtime=True,
            ffmpeg_input_option=["-fflags", "nobuffer"],
            source='https://example.invalid/live?x=$(not-a-shell)',
            audio_filter="pan=stereo|c0=c0|c1=c1",
            channels=2,
            sample_rate=48000,
        )
        ffmpeg_command = supervisor.ffmpeg_command_for_project_worker(args)
        assert isinstance(ffmpeg_command, list)
        assert args.source in ffmpeg_command
        assert "$(" in args.source
        assert "-i" in ffmpeg_command
        assert ffmpeg_command[ffmpeg_command.index("-i") + 1] == args.source

        # Exercise the complete internal worker with a fake FFmpeg process.
        # The equals form is important: real FFmpeg input options begin with
        # '-' and must remain argparse values rather than becoming options.
        with tempfile.TemporaryDirectory(
            prefix="pamguard-project-ingest-worker-"
        ) as root:
            root_path = Path(root)
            config_path = root_path / "ingest.json"
            config = {
                "schemaVersion": 2,
                "mode": "active-project",
                "engine": engine,
                "projectId": PROJECT_ID,
                "apiKey": "test-secret",
                "cursorDirectory": str(root_path / "cursors"),
                "defaults": {
                    "sampleRateHz": 48000,
                    "channels": 2,
                    "chunkFrames": 4,
                    "restart": False,
                    "realtime": False,
                },
                "sources": [
                    {
                        "id": "station-001",
                        "acquisitionUnitId": ACQUISITION_ID,
                        "source": (
                            "https://example.invalid/live?"
                            "x=$(not-a-shell)"
                        ),
                        "ffmpeg": "fake-ffmpeg",
                        "ffmpegInputOptions": [
                            "-fflags",
                            "nobuffer",
                        ],
                        "maxChunks": 1,
                    }
                ],
            }
            _, worker_command, _ = supervisor.command_for_source(
                config_path,
                config,
                config["sources"][0],
            )
            cursor = (
                root_path
                / "cursors"
                / "station-001.cursor.json"
            )
            assert "--ffmpeg-input-option=-fflags" in worker_command
            assert "--ffmpeg-input-option=nobuffer" in worker_command
            FakeFfmpegProcess.next_stdout = bytes(range(32))
            original_popen = supervisor.subprocess.Popen
            supervisor.subprocess.Popen = FakeFfmpegProcess
            try:
                worker_code = supervisor.project_worker_main(
                    worker_command[2:]
                )
            finally:
                supervisor.subprocess.Popen = original_popen
            assert worker_code == 0
            assert FakeFfmpegProcess.last_command[:5] == [
                "fake-ffmpeg",
                "-nostdin",
                "-hide_banner",
                "-loglevel",
                "error",
            ]
            assert "-fflags" in FakeFfmpegProcess.last_command
            assert "nobuffer" in FakeFfmpegProcess.last_command
            assert "$(" in FakeFfmpegProcess.last_command[
                FakeFfmpegProcess.last_command.index("-i") + 1
            ]
            assert (
                supervisor.load_project_cursor(
                    cursor,
                    PROJECT_ID,
                    ACQUISITION_ID,
                    7,
                    0,
                )
                == 4
            )
            final_post = ProjectApiHandler.requests[-1]
            assert final_post["method"] == "POST"
            assert final_post["path"] == (
                f"/v1/projects/active/acquisitions/"
                f"{ACQUISITION_ID}/pcm-f32le"
            )
            assert final_post["query"]["startSample"] == ["0"]
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)

    print("ingest supervisor project API smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
