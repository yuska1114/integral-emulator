#!/usr/bin/env python3
"""Run existing SQLite pair-commit assertions through the actual C API client.

Setup uses the existing synthetic fixture; start, valid finish, stale fence and
receipt use HTTP and api_workflow_test. No production server or user SAV is used.
"""
import base64
from pathlib import Path
import subprocess
import sys
import threading
from http.server import ThreadingHTTPServer

from tests.test_api import ApiTests
from integral_emulator.api import create_handler


def main() -> int:
    binary = str(Path(sys.argv[1]).resolve())
    case = ApiTests("test_gb_runtime_fixed_host_trade_pair_commit_requires_matching_remote_receipt_and_fence")
    case.setUp()
    original_post = case.post
    original_handle = case.app.handle_request
    captured = {}
    calls = []

    def record(*args, **kwargs):
        try:
            value = original_handle(*args, **kwargs)
            captured["result"] = value
            return value
        except Exception as exc:
            captured["error"] = exc
            raise

    case.app.handle_request = record
    handler = create_handler(case.app)

    class QuietHandler(handler):
        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), QuietHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    def post(path, body, token=None):
        if path == "/rooms/16/start":
            mode, args = "start", ["unused"]
        elif path.endswith("/host-finish") and body.get("remote_candidate"):
            host = Path(case.temp_dir.name) / "synthetic-host.bin"
            remote = Path(case.temp_dir.name) / "synthetic-remote.bin"
            host.write_bytes(base64.b64decode(body["host_candidate"]))
            remote.write_bytes(base64.b64decode(body["remote_candidate"]))
            mode, args = "finish", [path.split("/")[2], body["game_session_id"],
                                    str(body["fencing_token"]), str(host), str(remote)]
        elif path.endswith("/terminal-receipt"):
            mode, args = "receipt", [path.split("/")[2]]
        else:
            return original_post(path, body, token=token)
        captured.clear()
        result = subprocess.run([binary, f"http://127.0.0.1:{server.server_port}",
                                 token, mode, *args], capture_output=True, text=True, timeout=20)
        calls.append(mode)
        if "error" in captured:
            assert result.returncode != 0
            raise captured["error"]
        assert result.returncode == 0, (mode, result.stderr)
        response = captured["result"]
        expected = (response["link_session"]["id"] if mode == "start" else
                    response["gb_runtime_fixed_host_session"]["state"])
        assert result.stdout.strip() == expected, (mode, result.stdout, expected)
        return response

    case.post = post
    try:
        case.test_gb_runtime_fixed_host_trade_pair_commit_requires_matching_remote_receipt_and_fence()
        assert calls == ["start", "finish", "finish", "receipt", "finish"], calls
        print("C API + HTTP + SQLite: start, stale fence rejection, pending pair, receipt, both SAV revisions/content, idempotency PASS")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(5)
        case.tearDown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
