#!/usr/bin/env python3
"""Verify that the C client uses only the canonical API routes."""

from __future__ import annotations

import json
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


N64_RUNTIME_SAVE = "/n64-runtime-media-sessions/current-session/runtime-saves/n64"
N64_ROOM_START = "/rooms/65/start"
MOBILE = "/mobile-sessions"
MOBILE_SCENARIOS = "/mobile-scenarios"
MOBILE_COMPLETE = "/mobile-sessions/mobile-1/complete"
ROOM_HEARTBEAT = "/rooms/heartbeat"
FIXED_BASE = "/gb-runtime-fixed-host-sessions/fixed-session"
FIXED_MANIFEST = f"{FIXED_BASE}/manifest"
FIXED_PREFLIGHT = f"{FIXED_BASE}/preflight"
FIXED_TICKET = f"{FIXED_BASE}/relay-ticket"
FIXED_SNAPSHOTS = f"{FIXED_BASE}/runtime-snapshots"
GAME_STATUS = "/game/status"
GAME_START = "/game/start"
ROOM_CREATE = "/room-matching/create"
ROOM_JOIN = "/room-matching/join"
ROOM_CURRENT = "/room-matching/current"
ROM_APPLY = "/rom-slots/apply"
AUTH_LOGIN = "/auth/login"


def run_case(binary: Path, base_path: str = "", relay_transport: str = "tls") -> list[str]:
    paths: list[str] = []

    def canonical_path(raw_path: str) -> str:
        if base_path and raw_path.startswith(base_path):
            return raw_path[len(base_path):] or "/"
        return raw_path

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802
            paths.append(self.path)
            path = canonical_path(self.path)
            if path == ROOM_CURRENT:
                status = 200
                payload = {
                    "room": {
                        "room_number": 7, "room_code": "48291",
                        "room_type": "link_cable", "creator": True,
                        "users": [{"username": "current-user", "ready": False}],
                        "chat": [], "link_mode": "battle", "game_started": False,
                    }
                }
            elif path == GAME_STATUS:
                status = 200
                payload = {
                    "active": True,
                    "game_session": {
                        "link_session_id": "fixed-session",
                        "game_session_id": "game-fixed-1",
                        "fencing_token": 77,
                        "is_owner_auth_session": True,
                    },
                    "link_session": {"id": "fixed-session"},
                }
            elif path == FIXED_SNAPSHOTS:
                status = 200
                payload = {
                    "runtime_snapshots": {
                        "save_policy": "discard",
                        "host": {"save_data": "YWJj"},
                        "remote": {"save_data": "ZGVm"},
                    }
                }
            elif path == FIXED_MANIFEST:
                status = 200
                payload = {
                    "gb_runtime_fixed_host_session": {
                        "role": "host",
                        "manifest_digest": "d" * 64,
                        "manifest": {
                            "host_game_type": "catalog_alpha",
                            "remote_game_type": "catalog_beta",
                            "host_platform": "gb",
                            "remote_platform": "gb",
                            "host_rom_header_title": "ALPHA CORE",
                            "remote_rom_header_title": "BETA CORE",
                            "runtime_build_id": "integral-gb-runtime-fixed-host-v2",
                        },
                        "state": "PREFLIGHT",
                        "pause_remaining_seconds": 0,
                    }
                }
            elif path == N64_RUNTIME_SAVE:
                status = 200
                payload = {"revision": 7, "save_data": "YWJj"}
            else:
                status = 500
                payload = {"error": {"code": "unexpected", "message": "unexpected route"}}
            data = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self) -> None:  # noqa: N802
            paths.append(self.path)
            path = canonical_path(self.path)
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length)) if length else {}
            if path == AUTH_LOGIN:
                status = 200
                payload = {
                    "token": {"token": "current-token"},
                    "user": {"username": "MixedCase_User"},
                    "server": {
                        "allow_user_initial_save_import": False,
                    },
                }
            elif path == ROOM_CREATE:
                status = 200
                payload = {
                    "room": {
                        "room_number": 7, "room_code": "48291",
                        "room_type": "link_cable", "creator": True,
                        "users": [{"username": "current-user", "ready": False}],
                        "chat": [], "link_mode": "battle", "game_started": False,
                    }
                }
            elif path == ROOM_JOIN:
                status = 200
                payload = {
                    "room": {
                        "room_number": 65, "room_code": "48291",
                        "room_type": "n64", "creator": False,
                        "users": [{"username": "owner"}, {"username": "current-user"}],
                        "chat": [], "link_mode": "battle", "game_started": False,
                    }
                }
            elif path == ROOM_HEARTBEAT:
                status = 200
                payload = {
                    "session_lifecycle": {
                        "kind": "link",
                        "session_id": "completed-session",
                        "status": "CANCELLED",
                        "termination_reason": "game stopped",
                        "expires_at": "2026-09-04T01:00:00+00:00",
                    },
                    "server_time": {"unix_time": 1},
                }
            elif path == ROM_APPLY:
                if request.get("slots", [{}])[0].get("slot") != 6:
                    status = 400
                    payload = {"error": {"code": "invalid", "message": "wrong slot"}}
                else:
                    status = 200
                    payload = {
                        "requires_confirmation": False,
                        "slots": [
                            {
                                "slot": 1, "rom_id": "rom-slot1",
                                "save_id": "save-slot1", "filename": "sample_alpha.gbc",
                                "game_type": "sample_alpha",
                            },
                            {
                                "slot": 6, "rom_id": "rom-slot6",
                                "save_id": "save-slot6", "filename": "sample.z64",
                                "game_type": "n64_sample_n64", "platform": "n64",
                                "rom_header_title": "SAMPLE N64",
                            },
                        ],
                    }
            elif path == GAME_START:
                expected = {
                    "execution_mode": "N64_RUNTIME_CLIENT",
                    "save_ids": ["save-n64", "save-gb"],
                }
                status = 200 if request == expected else 400
                payload = {
                    "game_session": {
                        "game_session_id": "game-n64_runtime-1",
                        "fencing_token": 64,
                    }
                } if request == expected else {
                    "error": {
                        "code": "invalid",
                        "message": "wrong N64 Runtime execution mode",
                    }
                }
            elif path == FIXED_PREFLIGHT:
                expected = {
                    "manifest_digest": "d" * 64,
                    "protocol_id": "gb_runtime_fixed_host_v1",
                    "runtime_build_id": "integral-gb-runtime-fixed-host-v2",
                    "available_roms": [
                        {"game_type": "catalog_alpha", "platform": "gb", "rom_header_title": "ALPHA CORE"},
                        {"game_type": "catalog_beta", "platform": "gb", "rom_header_title": "BETA CORE"},
                    ],
                }
                status = 200 if request == expected else 400
                payload = {
                    "gb_runtime_fixed_host_session": {
                        "state": "READY" if request == expected else "PREFLIGHT"
                    }
                }
            elif path == FIXED_TICKET:
                status = 200
                payload = {
                    "gb_runtime_fixed_host_session": {"role": "host"},
                    "connection": {
                        "relay_host": "relay.example",
                        "relay_port": 25164,
                        "relay_transport": relay_transport,
                        "role": "host",
                        "scope": "gb-runtime-fixed-host-media-v1",
                        "ticket": "gbfixedticket-current-secret",
                        "runtime_config": {
                            "player_a": "A|A",
                            "player_b": "..|A",
                            "press_frames": 8,
                            "step_frames": 60,
                            "save_policy": "discard",
                        },
                    },
                }
            elif path == N64_ROOM_START:
                status = 200
                payload = {
                    "media_session": {"id": "n64-media-current"},
                    "connection": {
                        "relay_host": "relay.example",
                        "relay_port": 25164,
                        "relay_transport": relay_transport,
                        "role": "remote",
                        "scope": "n64_runtime_media",
                        "ticket": "n64ticket-current-secret",
                    },
                }
            elif path == MOBILE_SCENARIOS:
                status = 200
                payload = {
                    "rom_header_title": "INTEGRAL DEMO A",
                    "scenarios": [
                        {"scenario_id": "scenario_alpha", "display_name": "SYNTHETIC ALPHA", "release_id": "2026-09-02.synthetic-a", "default": False},
                        {"scenario_id": "scenario_beta", "display_name": "SYNTHETIC BETA", "release_id": "2026-09-02.synthetic-b", "default": True},
                    ],
                }
            elif path == MOBILE_COMPLETE:
                expected = {"game_session_id": "game-1", "fencing_token": 11}
                status = 200 if request == expected else 400
                payload = (
                    {"mobile_session": {"status": "COMPLETED"}}
                    if request == expected else
                    {"error": {"code": "invalid", "message": "wrong Mobile completion body"}}
                )
            elif path != MOBILE:
                status = 500
                payload = {"error": {"code": "unexpected", "message": "unexpected route"}}
            else:
                if request.get("scenario_id") == "auth-conflict":
                    status = 409
                    payload = {"error": {"code": "mobile_create_auth_session_conflict", "message": "different login"}}
                    data = json.dumps(payload).encode("utf-8")
                    self.send_response(status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
                if request.get("scenario_id") == "aborted":
                    status = 409
                    payload = {"error": {"code": "mobile_create_aborted", "message": "aborted"}}
                    data = json.dumps(payload).encode("utf-8")
                    self.send_response(status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
                if request.get("scenario_id") != "default":
                    status = 400
                    payload = {"error": {"code": "invalid", "message": "wrong scenario"}}
                    data = json.dumps(payload).encode("utf-8")
                    self.send_response(status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
                status = 200
                payload = {
                    "mobile_session": {
                        "id": "mobile-1",
                        "game_session_id": "game-1",
                        "fencing_token": 11,
                    },
                    "runtime_contract": {
                        "schema_version": 2,
                        "adapter_id": "gb_mobile_v2",
                        "package_id": "synthetic_numbers",
                        "release_id": "2026-09-02.1",
                        "package_digest": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                        "runtime_capability_version": 2,
                        "artifacts": [{
                            "role": "number_payload",
                            "content_id": "random_absent_number_v1",
                            "size": 3,
                            "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                            "data": "YWJj",
                        }],
                    },
                }
            data = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, _format: str, *args: object) -> None:
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        result = subprocess.run(
            [str(binary), f"http://127.0.0.1:{server.server_port}{base_path}",
             relay_transport],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"current contract client failed: rc={result.returncode} "
                f"stdout={result.stdout!r} stderr={result.stderr!r}"
            )
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    return paths


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} CURRENT_API_CONTRACT_BINARY")
    binary = Path(sys.argv[1]).resolve()
    fixed_paths = [FIXED_MANIFEST, FIXED_PREFLIGHT, GAME_STATUS, FIXED_TICKET, FIXED_SNAPSHOTS]
    room_paths = [ROOM_CREATE, ROOM_CURRENT, ROOM_JOIN]
    expected = [AUTH_LOGIN] + room_paths + [ROM_APPLY, N64_RUNTIME_SAVE,
        N64_ROOM_START, ROOM_HEARTBEAT, GAME_START, MOBILE_SCENARIOS, MOBILE,
        MOBILE, MOBILE, MOBILE_COMPLETE] + fixed_paths
    if run_case(binary, relay_transport="tls") != expected:
        raise RuntimeError("client request sequence was not canonical-only")
    prefixed_expected = [f"/sample-api{path}" for path in expected]
    if run_case(binary, "/sample-api", relay_transport="plain") != prefixed_expected:
        raise RuntimeError("client request sequence did not preserve the configured API prefix")
    print("C current API contract: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
