#!/usr/bin/env python3
"""HTTP-level boundary checks using synthetic payloads, without ROMs or real SAVs."""
import base64
import copy
import json
from pathlib import Path
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def main() -> int:
    binary = str(Path(sys.argv[1]).resolve())
    reply = {"data": b"", "status": 200}

    class Handler(BaseHTTPRequestHandler):
        def respond(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
            if self.path == "/auth/login":
                payload = json.loads(body)
                assert payload == {"username": "version-test", "password": "synthetic-password",
                                   "server_id": "primary", "client_version": "0.3.0-beta"}
                assert self.headers.get("Authorization") is None
            else:
                assert self.headers["Authorization"] == "Bearer synthetic-token"
            self.send_response(reply["status"])
            self.send_header("Content-Length", str(len(reply["data"])))
            self.end_headers()
            try:
                self.wfile.write(reply["data"])
            except (BrokenPipeError, ConnectionResetError):
                pass  # rejection can close before the excess bytes are sent

        do_GET = respond
        do_POST = respond

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    count = 0

    def run(mode, expected, payload, size=None, status=200):
        nonlocal count
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
        if size:
            assert len(data) <= size
            data += b" " * (size - len(data))
        reply.update(data=data, status=status)
        result = subprocess.run([binary, f"http://127.0.0.1:{server.server_port}",
                                 "synthetic-token", mode, str(expected)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (mode, expected, len(data), result.stdout, result.stderr)
        count += 1

    try:
        run("login-version", "ok", {"token": "synthetic-token", "username": "version-test"})
        run("login-version", "error", {"error": {"code": "client_version_not_allowed",
            "message": "ASK SERVER ADMIN FOR SUPPORTED VERSION"}}, status=426)
        snapshots = {"save_policy": "commit_pair",
                     "host": {"save_data": base64.b64encode(b"A" * 131072).decode()},
                     "remote": {"save_data": base64.b64encode(b"B" * 131072).decode()}}
        run("snapshot", "ok", snapshots)
        run("snapshot", "ok", snapshots, 6 * 1024 * 1024)
        run("snapshot", "error", snapshots, 6 * 1024 * 1024 + 1)
        bad = copy.deepcopy(snapshots)
        bad["remote"]["save_data"] = "!!!!"
        run("snapshot", "remote-error", bad)
        bad["host"]["save_data"] = "!!!!"
        run("snapshot", "error", bad)
        bad = copy.deepcopy(snapshots)
        bad["save_policy"] = "unknown"
        run("snapshot", "error", bad)
        run("snapshot", "error", b'{"save_policy":"commit_pair","host":{"save_data":"QQ=="},')
        contract = {
            "id": "mobile-1", "game_session_id": "game-1", "fencing_token": 11,
            "runtime_contract": {
                "schema_version": 2, "adapter_id": "gb_mobile_v2",
                "package_id": "synthetic_numbers", "release_id": "2026-09-02.1",
                "package_digest": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "runtime_capability_version": 2,
                "artifacts": [{"role": "number_payload", "content_id": "random_absent_number_v1",
                               "size": 3, "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                               "data": "YWJj"}],
            },
        }
        run("mobile", 0, contract)
        run("mobile", 0, contract, 45 * 1024 * 1024 - 1024)
        run("mobile", -1, {"id": "mobile", "runtime_contract": {}})
        run("mobile", -2, {"status": "COMPLETED"})
        run("mobile", -2, {"error": "mobile_create_aborted"}, status=409)
        run("mobile", -3, {"error": "mobile_create_auth_session_conflict"}, status=409)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(5)
    print(f"API boundaries: {count} HTTP cases PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
