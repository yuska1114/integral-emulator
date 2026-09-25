from __future__ import annotations

import base64
import copy
from concurrent.futures import ThreadPoolExecutor
import hashlib
import http.client
import io
import json
import os
import re
from multiprocessing import Process, Queue
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from datetime import datetime, timedelta, timezone
from email.message import Message
from http import HTTPStatus
from http.server import ThreadingHTTPServer
from pathlib import Path

import integral_emulator.allowed_roms as allowed_rom_catalog
from integral_emulator.allowed_roms import AllowedRom, allowed_roms
from integral_emulator.api import AUTH_JSON_BODY_BYTES, MAX_JSON_BODY_BYTES, LeagueApplication, RateLimitExceeded, RequestBodyTooLarge, create_handler, normalized_public_base_path
from integral_emulator.errors import (
    AlreadyInRoomError,
    AuthenticationError,
    DuplicateUserError,
    GameSessionFenceError,
    InvalidRoomCodeError,
    InvalidRoomModeError,
    NotFoundError,
    RegistrationDisabledError,
    RevisionConflictError,
    RoomCodeUnavailableError,
    RoomJoinRateLimitedError,
    RoomPoolFullError,
    SaveLockedError,
    ValidationError,
)
from integral_emulator.gb_runtime_fixed_host_protocol import GBRuntimeFixedHostManifest
from integral_emulator.sessions import LinkSessionStatus
from integral_emulator.room_session_policy import RoomSessionPolicy
from integral_emulator.storage import LeagueStorage
from integral_emulator.gb_runtime_link_modes import link_mode_profile
from integral_emulator.ui import render_admin_html, render_admin_login_html


def synthetic_rom(
    game_type: str, digit: str, *, header_title: str
) -> AllowedRom:
    return AllowedRom(
        catalog_id="synthetic_tests",
        catalog_role="primary",
        content_id=game_type,
        platform="gb",
        game_type=game_type,
        display_name=game_type.replace("_", " ").upper(),
        canonical_name=f"Synthetic {game_type}",
        region="JP",
        size=1024 * 1024,
        crc32=digit * 8,
        md5=digit * 32,
        sha1=digit * 40,
        sha256=digit * 64,
        rom_header_title=header_title,
    )


SYNTHETIC_ROMS = (
    synthetic_rom("sample_alpha", "1", header_title="SAMPLE ALPHA"),
    synthetic_rom("sample_beta", "2", header_title="SAMPLE BETA"),
    synthetic_rom("sample_gamma", "3", header_title="SAMPLE GAMMA"),
    synthetic_rom("sample_delta", "4", header_title="SAMPLE DELTA"),
)

def future_lock_expires_at() -> str:
    return (datetime.now(timezone.utc) + timedelta(days=1)).isoformat()


def hold_api_storage_lock(root: str, ready: Queue, release: Queue) -> None:
    storage = LeagueStorage(Path(root))
    with storage.exclusive_lock("api-request"):
        ready.put("locked")
        release.get(timeout=5)


class ApiTests(unittest.TestCase):
    def test_n64_preflight_failure_retains_room_clears_ready_and_locks(self) -> None:
        users, tokens = [], []
        for name in ("preflight_host", "preflight_remote"):
            users.append(self.post("/auth/register", {"username": name, "password": "password123"})["user"])
            tokens.append(self.post("/auth/login", {"username": name, "password": "password123"})["token"]["token"])
            self.join_room_fixture(65, token=tokens[-1])
        rooms = self.app.room_manager
        manager = self.app.n64_runtime_media_sessions
        media = manager.create_or_get(room_number=65,
            host_user_id=users[0]["id"], remote_user_id=users[1]["id"],
            host_n64_slot="ROM1", host_gb_slot="ROM2", remote_gb_slot="ROM1",
            host_n64_rom_id="n64", host_gb_rom_id="gb1", remote_gb_rom_id="gb2",
            host_n64_save_id="", host_save_id="", remote_save_id="", game_type="sample_alpha")
        for user, token in zip(users, tokens):
            self.app.sessions.acquire_media_game_session_lock(user["id"], media.id,
                self.app.game_session_lease_expires_at(), hashlib.sha256(token.encode()).hexdigest(), [])
            rooms.update_user_state(65, user["id"], ready=True)
        rooms.set_game_started(65, True)
        code = rooms.room(65).room_code
        path = f"/n64-runtime-media-sessions/{media.id}/terminate"
        body = {"room_code": code, "reason": "preflight_failed"}
        with self.assertRaises(ValidationError):
            self.post(path, body, token=tokens[1])
        with patch("integral_emulator.sqlite_room.SQLiteRoomManager.end_game_for_room", side_effect=RuntimeError("rollback")):
            with self.assertRaises(RuntimeError):
                self.post(path, body, token=tokens[0])
        self.assertEqual(manager.get(media.id).status, "CREATED")
        result = self.post(path, body, token=tokens[0])["media_session"]
        self.assertEqual((result["status"], result["termination_reason"]), ("CANCELLED", "preflight_failed"))
        self.assertEqual(self.post(path, body, token=tokens[0])["media_session"], result)
        room = rooms.room(65)
        self.assertEqual(room.room_code, code)
        self.assertFalse(room.game_started)
        self.assertEqual(len(room.users), 2)
        with self.app.authority_database.transaction() as connection:
            self.assertEqual(connection.execute("SELECT SUM(ready) FROM room_members WHERE room_number=65").fetchone()[0], 0)
        for user in users:
            self.assertIsNone(self.app.sessions.active_game_session_for_user(user["id"]))
            self.assertIsNotNone(rooms.current_room(user["id"]))
        # Terminal ticket/recovery cannot resurrect media. ROOM remains usable.
        self.assertEqual(manager.recover(media.id).status, "CANCELLED")
        self.post("/rooms/65/chat", {"message": "still here"}, token=tokens[1])
        arguments = {name: getattr(media, name) for name in (
            "room_number", "host_user_id", "remote_user_id", "host_n64_slot", "host_gb_slot",
            "remote_gb_slot", "host_n64_rom_id", "host_gb_rom_id", "remote_gb_rom_id",
            "host_n64_save_id", "host_save_id", "remote_save_id", "game_type")}
        fresh = manager.create_or_get(**arguments)
        self.assertNotEqual(fresh.id, media.id)
        for user in users: rooms.update_user_state(65, user["id"], ready=True)
        self.post(path, body, token=tokens[0])  # Delayed old failure cannot clear new READY.
        with self.app.authority_database.transaction() as connection:
            self.assertEqual(connection.execute("SELECT SUM(ready) FROM room_members WHERE room_number=65").fetchone()[0], 2)
        self.assertEqual(manager.get(fresh.id).status, "CREATED")
        self.post("/room-matching/leave", {}, token=tokens[0])
        self.assertIsNone(rooms.current_room(users[0]["id"]))

    def test_client_version_configuration(self) -> None:
        with patch.dict(os.environ, {"INTEGRAL_EMULATOR_CLIENT_VERSION_CHECK_ENABLED": "0",
                                    "INTEGRAL_EMULATOR_ALLOWED_CLIENT_VERSIONS": ""}):
            app = LeagueApplication(Path(self.temp_dir.name) / "version-default")
        self.assertFalse(app.client_version_check_enabled)
        with patch.dict(os.environ, {"INTEGRAL_EMULATOR_CLIENT_VERSION_CHECK_ENABLED": "1",
                                    "INTEGRAL_EMULATOR_ALLOWED_CLIENT_VERSIONS": " 0.2.0-beta,0.3.0-beta, "}):
            app = LeagueApplication(Path(self.temp_dir.name) / "version-check")
        self.assertTrue(app.client_version_check_enabled)
        self.assertEqual(app.allowed_client_versions, {"0.2.0-beta", "0.3.0-beta"})

    def test_client_version_http_policy_auth_and_rate_limit(self) -> None:
        password = "correct horse battery staple"
        for name in ("VersionOff", "VersionOn", "VersionRate"):
            self.app.auth.register(name, password)
        server = ThreadingHTTPServer(("127.0.0.1", 0), create_handler(self.app))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def login(name, extra, secret=password):
            connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=5)
            try:
                connection.request("POST", "/auth/login", json.dumps(
                    {"username": name, "password": secret, **extra}),
                    {"Content-Type": "application/json"})
                response = connection.getresponse()
                return response.status, json.loads(response.read())
            finally:
                connection.close()

        try:
            self.app.client_version_check_enabled = False
            self.app.allowed_client_versions = frozenset({"0.2.0-beta"})
            for extra in ({}, {"client_version": "unknown"}, {"client_version": "0.2.0-beta"}):
                self.assertEqual(login("VersionOff", extra)[0], 200)
            self.app.client_version_check_enabled = True
            self.assertEqual(login("VersionOn", {"client_version": "0.2.0-beta"})[0], 200)
            for extra in ({}, {"client_version": "0.3.0-beta"}, {"client_version": "0.2.0-BETA"},
                          {"client_version": " 0.2.0-beta"}, {"client_version": None},
                          {"client_version": ["0.2.0-beta"]}):
                with patch.object(self.app.auth.repository, "create_session") as create:
                    status, body = login("VersionOn", extra)
                self.assertEqual(status, 426)
                self.assertEqual(body["error"]["code"], "client_version_not_allowed")
                self.assertEqual(body["error"]["message"], "ASK SERVER ADMIN FOR SUPPORTED VERSION")
                create.assert_not_called()
            status, body = login("VersionOn", {}, "wrong password")
            self.assertEqual(status, 401)
            self.assertEqual(body["error"]["code"], "authentication_failed")
            self.app.allowed_client_versions = frozenset()
            self.assertEqual(login("VersionOn", {"client_version": "0.2.0-beta"})[0], 426)
            for _ in range(10):
                self.assertEqual(login("VersionRate", {})[0], 426)
            status, body = login("VersionRate", {})
            self.assertEqual(status, 429)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.original_allowed_roms = allowed_rom_catalog._ALLOWED_ROMS
        allowed_rom_catalog._ALLOWED_ROMS = SYNTHETIC_ROMS
        self.app = LeagueApplication(Path(self.temp_dir.name))
        self.app.allow_self_registration = True

    def tearDown(self) -> None:
        allowed_rom_catalog._ALLOWED_ROMS = self.original_allowed_roms
        self.temp_dir.cleanup()

    def assert_no_auth_session_digest(self, response: object) -> None:
        self.assertNotIn("auth_session_id_digest", json.dumps(response, sort_keys=True))

    def enforce_rom_allowlist(self) -> None:
        self.app.allow_unlisted_roms = False
        for environment in self.app.environments.values():
            environment.roms.allow_unlisted_roms = False

    def test_request_environment_context_is_thread_local_and_immutable(self) -> None:
        import threading

        barrier = threading.Barrier(2)

        def inspect(server_id: str) -> tuple[str, Path]:
            environment = self.app.environments[server_id]
            with self.app.use_environment(environment) as context:
                barrier.wait(timeout=2)
                self.assertEqual(context.server_id, server_id)
                return self.app.active_server_id, self.app.storage.root

        with ThreadPoolExecutor(max_workers=2) as executor:
            primary = executor.submit(inspect, "primary")
            secondary = executor.submit(inspect, "secondary")
            self.assertEqual(primary.result()[0], "primary")
            self.assertEqual(secondary.result()[0], "secondary")
            self.assertNotEqual(primary.result()[1], secondary.result()[1])
        self.assertNotIn("active_server_id", self.app.__dict__)

    def test_authenticated_request_resolves_identity_once(self) -> None:
        self.app.handle_request(
            "POST", "/auth/register", {"username": "lookup-once", "password": "password123"}
        )
        login = self.app.handle_request(
            "POST",
            "/auth/login",
            {"username": "lookup-once", "password": "password123", "server_id": "secondary"},
        )
        token = login["token"]["token"]
        with patch.object(
            self.app.auth,
            "require_identity",
            wraps=self.app.auth.require_identity,
        ) as require_identity:
            response = self.app.handle_request("GET", "/me", bearer_token=token)
        self.assertEqual(response["server"]["id"], "secondary")
        self.assertEqual(require_identity.call_count, 1)

    def test_health_and_time_skip_request_reconcile(self) -> None:
        with patch.object(self.app, "reconcile_session_lifecycle") as reconcile:
            self.assertEqual(self.app.handle_request("GET", "/health"), {"ok": True})
            self.app.handle_request("GET", "/time")
        reconcile.assert_not_called()

    def test_network_mode_is_explicit_and_controls_relay_and_admin_cookie(self) -> None:
        self.assertEqual(self.app.network_mode, "tls")
        self.assertEqual(self.app.n64_runtime_media_relay_transport, "tls")
        self.assertTrue(self.app.admin_cookie_secure)
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(
                os.environ, {"INTEGRAL_EMULATOR_NETWORK_MODE": "plain"}
            ):
                plain = LeagueApplication(Path(directory))
        self.assertEqual(plain.n64_runtime_media_relay_transport, "plain")
        self.assertFalse(plain.admin_cookie_secure)
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(
                os.environ, {"INTEGRAL_EMULATOR_NETWORK_MODE": "automatic"}
            ):
                with self.assertRaisesRegex(ValidationError, "must be tls or plain"):
                    LeagueApplication(Path(directory))

    def test_api_overview_lists_only_current_implemented_routes(self) -> None:
        overview = (Path(__file__).resolve().parents[1] / "docs" / "API.md").read_text(
            encoding="utf-8"
        )
        documented = set(
            re.findall(r"^- `(GET|POST|PUT) (/[^`]+)`$", overview, re.MULTILINE)
        )
        expected = {
            ("GET", "/health"),
            ("GET", "/time"),
            ("POST", "/auth/register"),
            ("POST", "/auth/login"),
            ("POST", "/auth/change-password"),
            ("POST", "/auth/logout"),
            ("GET", "/me"),
            ("POST", "/admin/users/issue"),
            ("POST", "/admin/users/reset-password"),
            ("GET", "/admin/operations"),
            ("POST", "/admin/saves/{save_id}/replace"),
            ("GET", "/roms"),
            ("POST", "/roms"),
            ("GET", "/roms/{rom_id}"),
            ("GET", "/rom-slots"),
            ("POST", "/rom-slots/apply"),
            ("GET", "/saves"),
            ("GET", "/saves/{save_id}"),
            ("PUT", "/saves/{save_id}"),
            ("POST", "/room-matching/create"),
            ("POST", "/room-matching/join"),
            ("GET", "/room-matching/current"),
            ("POST", "/room-matching/leave"),
            ("POST", "/rooms/{room_number}/state"),
            ("POST", "/rooms/{room_number}/chat"),
            ("POST", "/rooms/{room_number}/start"),
            ("POST", "/rooms/heartbeat"),
            ("POST", "/game/start"),
            ("GET", "/game/status"),
            ("POST", "/game/heartbeat"),
            ("POST", "/game/stop"),
            ("GET", "/gb-runtime-fixed-host-sessions/{session_id}/manifest"),
            ("POST", "/gb-runtime-fixed-host-sessions/{session_id}/preflight"),
            ("GET", "/gb-runtime-fixed-host-sessions/{session_id}/runtime-snapshots"),
            ("POST", "/gb-runtime-fixed-host-sessions/{session_id}/relay-ticket"),
            ("POST", "/gb-runtime-fixed-host-sessions/{session_id}/host-finish"),
            ("POST", "/gb-runtime-fixed-host-sessions/{session_id}/terminal-receipt"),
            ("POST", "/gb-runtime-fixed-host-sessions/{session_id}/cancel"),
            ("POST", "/mobile-scenarios"),
            ("POST", "/mobile-sessions"),
            ("GET", "/mobile-sessions/{session_id}"),
            ("POST", "/mobile-sessions/{session_id}/heartbeat"),
            ("POST", "/mobile-sessions/{session_id}/complete"),
            ("POST", "/mobile-sessions/{session_id}/cancel"),
            ("GET", "/n64-runtime-media-sessions/{session_id}"),
            ("POST", "/n64-runtime-media-sessions/{session_id}/finish"),
            ("POST", "/n64-runtime-media-sessions/{session_id}/recover"),
            ("POST", "/n64-runtime-media-sessions/{session_id}/terminate"),
            ("POST", "/gb-runtime-fixed-host-sessions/{session_id}/blocked"),
            ("GET", "/n64-runtime-media-sessions/{session_id}/runtime-saves/{kind}"),
        }
        self.assertEqual(documented, expected)

        substitutions = {
            "{save_id}": "save-id",
            "{rom_id}": "rom-id",
            "{room_number}": "1",
            "{session_id}": "session-id",
            "{kind}": "n64",
        }
        for method, template in documented:
            path = template
            for marker, value in substitutions.items():
                path = path.replace(marker, value)
            try:
                self.app.handle_request(method, path, {})
            except NotFoundError as error:
                self.fail(f"API overview route is not implemented: {method} {template}: {error}")
            except (AuthenticationError, ValidationError):
                pass

        for retired in (
            "/link-sessions",
            "start-client-link",
            "/extend",
            "resume-finalize",
            "client-saves/",
        ):
            self.assertNotIn(retired, overview)

    def test_rom_allowlist_is_disabled_by_default_and_configurable(self) -> None:
        self.assertTrue(self.app.allow_unlisted_roms)
        self.assertTrue(
            all(
                environment.roms.allow_unlisted_roms
                for environment in self.app.environments.values()
            )
        )
        with patch.dict(
            os.environ,
            {"INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS": "0"},
        ):
            strict = LeagueApplication(Path(self.temp_dir.name) / "strict")
        self.assertFalse(strict.allow_unlisted_roms)
        self.assertTrue(
            all(
                not environment.roms.allow_unlisted_roms
                for environment in strict.environments.values()
            )
        )

    def test_self_registration_is_disabled_when_unspecified(self) -> None:
        name = "INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION"
        with patch.dict(os.environ, {}, clear=False):
            os.environ.pop(name, None)
            app = LeagueApplication(Path(self.temp_dir.name) / "registration-default")
        self.assertFalse(app.allow_self_registration)
        with self.assertRaisesRegex(RegistrationDisabledError, "self-registration is disabled"):
            app.handle_request(
                "POST",
                "/auth/register",
                {"username": "Disabled001", "password": "correct horse battery staple"},
            )

    def test_self_registration_false_values_are_disabled(self) -> None:
        for index, value in enumerate(("0", "false")):
            with self.subTest(value=value), patch.dict(
                os.environ,
                {"INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION": value},
            ):
                app = LeagueApplication(
                    Path(self.temp_dir.name) / f"registration-disabled-{index}"
                )
            self.assertFalse(app.allow_self_registration)
            with self.assertRaises(RegistrationDisabledError):
                app.handle_request(
                    "POST",
                    "/auth/register",
                    {"username": f"Disabled{index}", "password": "correct horse battery staple"},
                )

    def test_self_registration_true_values_preserve_registration(self) -> None:
        for index, value in enumerate(("1", "true")):
            with self.subTest(value=value), patch.dict(
                os.environ,
                {"INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION": value},
            ):
                app = LeagueApplication(
                    Path(self.temp_dir.name) / f"registration-enabled-{index}"
                )
            registered = app.handle_request(
                "POST",
                "/auth/register",
                {"username": f"Enabled{index}", "password": "correct horse battery staple"},
            )
            self.assertEqual(registered["user"]["username"], f"Enabled{index}")

    def test_self_registration_disabled_does_not_affect_admin_issue(self) -> None:
        self.app.allow_self_registration = False
        issued = self.post("/admin/users/issue", {"username": "AdminIssued001"})
        self.assertEqual(issued["user"]["username"], "AdminIssued001")
        self.assertTrue(issued["user"]["must_change_password"])

    def test_self_registration_disabled_returns_http_403(self) -> None:
        self.app.allow_self_registration = False
        server = ThreadingHTTPServer(("127.0.0.1", 0), create_handler(self.app))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        connection = http.client.HTTPConnection(
            "127.0.0.1", server.server_port, timeout=3
        )
        try:
            payload = json.dumps(
                {"username": "DisabledHttp", "password": "correct horse battery staple"}
            )
            connection.request(
                "POST",
                "/auth/register",
                body=payload,
                headers={"Content-Type": "application/json"},
            )
            response = connection.getresponse()
            body = json.loads(response.read())
            self.assertEqual(response.status, HTTPStatus.FORBIDDEN)
            self.assertEqual(body["error"]["code"], "registration_disabled")
        finally:
            connection.close()
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)

    def test_room_configuration_defaults_and_sparse_override(self) -> None:
        self.assertEqual(self.app.enabled_link_rooms, tuple(range(1, 17)))
        self.assertEqual(self.app.enabled_n64_rooms, tuple(range(65, 81)))
        with patch.dict(
            os.environ,
            {
                "INTEGRAL_EMULATOR_LINK_CABLE_ROOMS": "2,64",
                "INTEGRAL_EMULATOR_N64_ROOMS": "65,128",
            },
        ):
            configured = LeagueApplication(Path(self.temp_dir.name) / "configured-rooms")
        self.assertEqual(configured.enabled_link_rooms, (2, 64))
        self.assertEqual(configured.enabled_n64_rooms, (65, 128))
        for environment in configured.environments.values():
            self.assertEqual(
                [room.room_number for room in environment.room_manager.list_rooms()],
                [2, 64, 65, 128],
            )

    def test_invalid_room_configuration_fails_startup(self) -> None:
        with patch.dict(
            os.environ,
            {"INTEGRAL_EMULATOR_LINK_CABLE_ROOMS": "1-65"},
        ):
            with self.assertRaisesRegex(ValidationError, "between 1 and 64"):
                LeagueApplication(Path(self.temp_dir.name) / "invalid-rooms")

    def test_user_initial_save_import_is_disabled_by_default_and_published(self) -> None:
        self.assertFalse(self.app.allow_user_initial_save_import)
        self.post(
            "/auth/register",
            {"username": "Import_Policy", "password": "correct horse battery staple"},
        )
        login = self.post(
            "/auth/login",
            {"username": "import_policy", "password": "correct horse battery staple"},
        )
        token = login["token"]["token"]
        self.assertFalse(login["server"]["allow_user_initial_save_import"])
        self.assertFalse(
            self.get("/me", token=token)["server"]["allow_user_initial_save_import"]
        )

    def test_invalid_user_initial_save_import_setting_fails_startup(self) -> None:
        with patch.dict(
            os.environ,
            {"INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT": "sometimes"},
        ):
            with self.assertRaisesRegex(
                ValidationError,
                "INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT must be a boolean",
            ):
                LeagueApplication(Path(self.temp_dir.name) / "invalid-save-import-policy")

    def test_user_initial_save_import_setting_can_be_enabled(self) -> None:
        with patch.dict(
            os.environ,
            {"INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT": "1"},
        ):
            app = LeagueApplication(Path(self.temp_dir.name) / "enabled-save-import-policy")
        self.assertTrue(app.allow_user_initial_save_import)

    def test_invalid_rom_allowlist_setting_fails_startup(self) -> None:
        with patch.dict(
            os.environ,
            {"INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS": "sometimes"},
        ):
            with self.assertRaisesRegex(
                ValidationError,
                "INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS must be a boolean",
            ):
                LeagueApplication(Path(self.temp_dir.name) / "invalid-policy")

    def test_sqlite_auth_routes_do_not_take_file_lock_or_reconcile(self) -> None:
        with (
            patch.object(
                self.app.storage.files,
                "exclusive_lock",
                side_effect=AssertionError("file lock must not be used"),
            ),
            patch.object(self.app, "reconcile_session_lifecycle") as reconcile,
        ):
            self.app.handle_request(
                "POST", "/auth/register",
                {"username": "sqlite-auth", "password": "password123"},
            )
            login = self.app.handle_request(
                "POST", "/auth/login",
                {"username": "sqlite-auth", "password": "password123"},
            )
            self.app.handle_request("GET", "/me", bearer_token=login["token"]["token"])
        reconcile.assert_not_called()

    def test_sqlite_authority_route_does_not_take_global_lock(self) -> None:
        with patch.object(
            self.app.storage.files,
            "exclusive_lock",
            wraps=self.app.storage.files.exclusive_lock,
        ) as exclusive_lock:
            with self.assertRaises(AuthenticationError):
                self.app.handle_request("GET", "/room-matching/current")
        exclusive_lock.assert_not_called()

    def test_sqlite_auth_register_is_not_blocked_by_external_json_lock(self) -> None:
        ready: Queue = Queue()
        release: Queue = Queue()
        process = Process(
            target=hold_api_storage_lock,
            args=(str(self.app.storage_root), ready, release),
        )
        process.start()
        try:
            self.assertEqual(ready.get(timeout=5), "locked")
            started = time.monotonic()
            self.app.handle_request(
                "POST", "/auth/register",
                {"username": "parallel-auth", "password": "password123"},
            )
            self.assertLess(time.monotonic() - started, 1.0)
        finally:
            release.put("release")
            process.join(timeout=5)
            if process.is_alive():
                process.terminate()
                process.join(timeout=5)
        self.assertEqual(process.exitcode, 0)

    def test_performance_metrics_use_secret_free_route_names(self) -> None:
        username = "metric-secret-user"
        self.app.handle_request(
            "POST", "/auth/register", {"username": username, "password": "password123"}
        )
        snapshot = json.dumps(self.app.performance_metrics.snapshot(), sort_keys=True)
        self.assertNotIn(username, snapshot)
        self.assertNotIn("password123", snapshot)
        self.assertIn("POST/auth/register", snapshot)
        self.assertIn("db_transaction", snapshot)

    def test_battle_turbo_macro_config_is_reloaded_without_process_restart(self) -> None:
        config_path = Path(self.temp_dir.name) / "gb_runtime_link_macros.json"
        payload = {
            "battle_turbo": {
                "gen2": {
                    "player_a": "..|A",
                    "player_b": "..|.A",
                    "step_frames": 60,
                    "press_frames": 8,
                    "sync_every_step": False,
                }
            }
        }
        config_path.write_text(json.dumps(payload), encoding="utf-8")
        self.assertEqual(link_mode_profile("battle", config_path)["turbo_macro"]["player_a"], "..A")

        payload["battle_turbo"]["gen2"]["player_a"] = "..|.A"
        config_path.write_text(json.dumps(payload), encoding="utf-8")
        self.assertEqual(link_mode_profile("battle", config_path)["turbo_macro"]["player_a"], "...A")

    def test_battle_turbo_macro_config_requires_matching_sync_markers(self) -> None:
        config_path = Path(self.temp_dir.name) / "gb_runtime_link_macros.json"
        payload = {
            "battle_turbo": {
                "gen2": {
                    "player_a": "..|A",
                    "player_b": "...|A",
                    "step_frames": 60,
                    "press_frames": 8,
                    "sync_every_step": False,
                }
            }
        }
        config_path.write_text(json.dumps(payload), encoding="utf-8")
        with self.assertRaisesRegex(ValidationError, "same timed position"):
            link_mode_profile("battle", config_path)

        payload["battle_turbo"]["gen2"]["player_b"] = "....A"
        config_path.write_text(json.dumps(payload), encoding="utf-8")
        with self.assertRaisesRegex(ValidationError, "exactly one sync marker"):
            link_mode_profile("battle", config_path)

    def test_generation_one_turbo_profile_excludes_test_title_macro(self) -> None:
        trade = link_mode_profile("trade", rom_generation="gen1")["turbo_macro"]
        battle = link_mode_profile("battle", rom_generation="gen1")["turbo_macro"]

        self.assertEqual(trade["player_a"], "..A......A........A..........A..........R..A")
        self.assertEqual(battle["player_a"], "..A......A........A...........D...A..........R..A")
        self.assertEqual(trade["press_frames"], 20)
        self.assertEqual(battle["press_frames"], 20)
        self.assertTrue(trade["sync_every_step"])
        self.assertTrue(battle["sync_every_step"])
        for macro in (trade["player_a"], trade["player_b"], battle["player_a"], battle["player_b"]):
            self.assertFalse(macro.startswith("...............A.....A......A......A.............."))

    def test_v2_profile_defaults_off_and_uses_the_existing_feature_gate(self) -> None:
        disabled = link_mode_profile("battle")
        enabled = link_mode_profile("battle", byte_preannounce=True)

        self.assertFalse(disabled["byte_preannounce"])
        self.assertNotIn("--byte-protocol-v2", disabled["emulator_args"])
        self.assertFalse(disabled["byte_completion_ack"])
        self.assertFalse(disabled["byte_protocol_v2"])
        self.assertTrue(enabled["byte_preannounce"])
        self.assertIn("--byte-protocol-v2", enabled["emulator_args"])
        self.assertFalse(enabled["byte_completion_ack"])
        self.assertTrue(enabled["byte_protocol_v2"])

    def test_register_login_save_download_upload_flow(self) -> None:
        user = self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        self.assertNotIn("password_hash", user["user"])

        login = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})
        self.assertFalse(login["server"]["allow_user_initial_save_import"])
        token = login["token"]["token"]
        created = self.create_test_save(
            {"game_type": "sample_gamma", "save_data": encode(b"revision-one")},
            token=token,
        )["save"]

        downloaded = self.get(f"/saves/{created['id']}", token=token)
        self.assertEqual(decode(downloaded["save_data"]), b"revision-one")

        updated = self.put(
            f"/saves/{created['id']}",
            {"expected_revision": 1, "save_data": encode(b"revision-two")},
            token=token,
        )["save"]
        self.assertEqual(updated["revision"], 2)
        saves = self.get("/saves", token=token)["saves"]
        self.assertEqual([save["id"] for save in saves], [created["id"]])
        logged_out = self.post("/auth/logout", {}, token=token)
        self.assertTrue(logged_out["logged_out"])

    def test_general_user_save_creation_route_is_removed(self) -> None:
        self.post(
            "/auth/register",
            {"username": "No_Save_Create", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "no_save_create", "password": "correct horse battery staple"},
        )["token"]["token"]
        with self.assertRaisesRegex(NotFoundError, "route not found"):
            self.request(
                "POST",
                "/saves",
                {"rom_id": "rom_unused", "save_data": encode(bytes(32 * 1024))},
                token,
            )

    def test_auth_rejects_short_password_and_non_string_fields(self) -> None:
        with self.assertRaisesRegex(ValidationError, "at least 8 characters"):
            self.post("/auth/register", {"username": "Player_A", "password": "short"})
        with self.assertRaisesRegex(ValidationError, "password must be a string"):
            self.post("/auth/register", {"username": "Player_A", "password": ["bad"]})
        with self.assertRaisesRegex(ValidationError, "username must be a string"):
            self.post("/auth/login", {"username": {"bad": True}, "password": "password123"})

    def test_username_display_case_is_preserved_but_identity_is_case_insensitive(self) -> None:
        registered = self.post(
            "/auth/register",
            {"username": "Yusuke", "password": "correct horse battery staple"},
        )["user"]
        logged_in = self.post(
            "/auth/login",
            {"username": "YUSUKE", "password": "correct horse battery staple"},
        )["user"]

        self.assertEqual(registered["username"], "Yusuke")
        self.assertEqual(logged_in["username"], "Yusuke")
        self.assertNotIn("username_normalized", registered)
        with self.assertRaises(DuplicateUserError):
            self.post(
                "/auth/register",
                {"username": "yusuke", "password": "another correct password"},
            )

    def test_password_policy_is_shared_by_register_change_and_reset(self) -> None:
        user = self.post(
            "/auth/register",
            {"username": "Player_A", "password": "valid passphrase!"},
        )["user"]
        token = self.post(
            "/auth/login",
            {"username": user["username"], "password": "valid passphrase!"},
        )["token"]["token"]
        changed = self.post(
            "/auth/change-password",
            {"new_password": "another passphrase!"},
            token=token,
        )
        self.assertEqual(changed["user"]["id"], user["id"])


    def test_same_account_can_use_separate_server_environments(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        primary_login = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple", "server_id": "primary"},
        )
        secondary_login = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple", "server_id": "secondary"},
        )
        secondary_token = secondary_login["token"]["token"]
        self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"secondary-save")}, token=secondary_token)
        primary_token = primary_login["token"]["token"]

        self.assertNotEqual(primary_token, secondary_token)
        self.assertEqual(primary_login["token"]["server_id"], "primary")
        self.assertEqual(secondary_login["token"]["server_id"], "secondary")
        self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"primary-save")}, token=primary_token)

        primary_saves = self.get("/saves", token=primary_token)["saves"]
        secondary_saves = self.get("/saves", token=secondary_token)["saves"]
        self.assertEqual([save["game_type"] for save in primary_saves], ["sample_alpha"])
        self.assertEqual([save["game_type"] for save in secondary_saves], ["sample_beta"])
        self.assertEqual(self.get("/me", token=secondary_token)["server"]["id"], "secondary")

    def test_game_session_lock_is_shared_across_server_environments(self) -> None:
        user = self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})["user"]
        primary_token = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple", "server_id": "primary"},
        )["token"]["token"]
        secondary_token = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple", "server_id": "secondary"},
        )["token"]["token"]
        primary_save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"primary-a")}, token=primary_token)["save"]
        primary_save_b = self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"primary-b")}, token=primary_token)["save"]
        secondary_save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"secondary-a")}, token=secondary_token)["save"]
        secondary_save_b = self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"secondary-b")}, token=secondary_token)["save"]

        started = self.app.environments["primary"].sessions.create_session(
            user["id"],
            user["id"],
            primary_save_a["id"],
            primary_save_b["id"],
            lock_expires_at=self.app.game_session_lease_expires_at(),
            auth_session_ids={user["id"]: hashlib.sha256(primary_token.encode()).hexdigest()},
        )
        secondary_status = self.get("/game/status", token=secondary_token)

        with self.assertRaisesRegex(ValidationError, "user already has an active game session"):
            self.app.environments["secondary"].sessions.create_session(
                user["id"],
                user["id"],
                secondary_save_a["id"],
                secondary_save_b["id"],
                lock_expires_at=self.app.game_session_lease_expires_at(),
                auth_session_ids={user["id"]: hashlib.sha256(secondary_token.encode()).hexdigest()},
            )

        self.assertTrue(secondary_status["active"])
        self.assertEqual(secondary_status["game_session"]["server_id"], "primary")
        self.assertEqual(secondary_status["game_session"]["link_session_id"], started.id)
        self.assertIsNone(secondary_status["link_session"])

    def test_same_account_can_login_again_after_client_exit_without_logout(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        first = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})
        second = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})

        self.assertNotEqual(first["token"]["token"], second["token"]["token"])
        self.assertEqual(self.get("/me", token=first["token"]["token"])["user"]["username"], "Player_A")
        self.assertEqual(self.get("/me", token=second["token"]["token"])["user"]["username"], "Player_A")

    def test_game_status_and_stop_are_limited_to_starting_auth_session(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        first = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        second = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"a-save")}, token=first)["save"]
        save_b = self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"b-save")}, token=first)["save"]
        started = self.create_link_session_direct(
            save_a["user_id"],
            save_a["user_id"],
            save_a["id"],
            save_b["id"],
            auth_session_ids={save_a["user_id"]: first},
        )

        self.app.sessions.transition(started["id"], LinkSessionStatus.RUNNING)

        owner_status = self.get("/game/status", token=first)
        other_status = self.get("/game/status", token=second)
        with self.assertRaisesRegex(ValidationError, "different auth session"):
            self.post("/game/stop", {}, token=second)
        with self.assertRaisesRegex(ValidationError, "game_session_id and fencing_token are required"):
            self.post("/game/stop", {}, token=first)
        stopped = self.post("/game/stop", self.game_fence(owner_status), token=first)
        inactive = self.get("/game/status", token=first)

        self.assertTrue(owner_status["active"])
        self.assertTrue(owner_status["game_session"]["is_owner_auth_session"])
        self.assertTrue(owner_status["game_session"]["game_session_id"].startswith("game_"))
        self.assertEqual(owner_status["link_session"]["id"], started["id"])
        self.assertTrue(other_status["active"])
        self.assertFalse(other_status["game_session"]["is_owner_auth_session"])
        self.assertNotIn("game_session_id", other_status["game_session"])
        self.assertIsNone(other_status["game_session"]["fencing_token"])
        self.assertEqual(stopped["link_session"]["status"], "CANCELLED")
        self.assertFalse(inactive["active"])

    def test_room_game_stop_response_hides_auth_session_digests(self) -> None:
        user_a = self.post(
            "/auth/register",
            {"username": "Stop_Room_A", "password": "correct horse battery staple"},
        )["user"]
        user_b = self.post(
            "/auth/register",
            {"username": "Stop_Room_B", "password": "another correct password"},
        )["user"]
        token_a = self.post(
            "/auth/login",
            {"username": "stop_room_a", "password": "correct horse battery staple"},
        )["token"]["token"]
        token_b = self.post(
            "/auth/login",
            {"username": "stop_room_b", "password": "another correct password"},
        )["token"]["token"]
        save_a = self.create_test_save(
            {"game_type": "sample_alpha", "save_data": encode(b"a-save")}, token=token_a
        )["save"]
        save_b = self.create_test_save(
            {"game_type": "sample_beta", "save_data": encode(b"b-save")}, token=token_b
        )["save"]
        self.join_room_fixture(1, token=token_a)
        self.join_room_fixture(1, token=token_b)
        session = self.app.sessions.create_session(
            user_a["id"],
            user_b["id"],
            save_a["id"],
            save_b["id"],
            lock_expires_at=self.app.game_session_lease_expires_at(),
            room_number=1,
            base_port=self.app.room_base_port(1),
            auth_session_ids={
                user_a["id"]: hashlib.sha256(token_a.encode()).hexdigest(),
                user_b["id"]: hashlib.sha256(token_b.encode()).hexdigest(),
            },
        )
        self.app.room_manager.set_link_session(1, session.id)
        self.app.sessions.transition(session.id, LinkSessionStatus.RUNNING)
        status = self.get("/game/status", token=token_a)

        stopped = self.post("/game/stop", self.game_fence(status), token=token_a)

        self.assertIsNotNone(stopped["room"])
        self.assertEqual(stopped["link_session"]["status"], "CANCELLED")
        self.assert_no_auth_session_digest(stopped)

    def test_user_room_api_does_not_directly_serialize_internal_rooms(self) -> None:
        source = (Path(__file__).resolve().parents[1] / "src/integral_emulator/api.py").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(source, r'"room"\s*:\s*room\.to_dict\(\)')

    def test_local_game_start_uses_game_session_lock(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        owner_token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        other_token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"a-save")}, token=owner_token)["save"]
        save_b = self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"b-save")}, token=owner_token)["save"]

        started = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save_a["id"]]}, token=owner_token)
        status = self.get("/game/status", token=owner_token)

        self.assertEqual(started["game_session"]["execution_mode"], "LOCAL_CLIENT")
        self.assertTrue(status["active"])
        self.assertEqual(status["game_session"]["execution_mode"], "LOCAL_CLIENT")
        self.assertIsNone(status["link_session"])
        with self.assertRaisesRegex(ValidationError, "different auth session"):
            self.post("/game/stop", self.game_fence(started), token=other_token)
        with self.assertRaisesRegex(ValidationError, "user already has an active game session"):
            self.create_link_session_direct(
                save_a["user_id"],
                save_a["user_id"],
                save_a["id"],
                save_b["id"],
                auth_session_ids={save_a["user_id"]: owner_token},
            )

        stopped = self.post("/game/stop", self.game_fence(started), token=owner_token)
        inactive = self.get("/game/status", token=owner_token)
        next_session = self.create_link_session_direct(
            save_a["user_id"],
            save_a["user_id"],
            save_a["id"],
            save_b["id"],
            auth_session_ids={save_a["user_id"]: owner_token},
        )

        self.assertEqual(stopped["game_session"]["execution_mode"], "LOCAL_CLIENT")
        self.assertIsNone(stopped["link_session"])
        self.assertFalse(inactive["active"])
        self.assertTrue(next_session["id"].startswith("link_"))

    def test_local_game_deadlines_use_mode_policy_and_do_not_slide(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Deadline_User", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "deadline_user", "password": "correct horse battery staple"},
        )["token"]["token"]
        slots = self.post(
            "/rom-slots/apply",
            {"slots": [
                {
                    "slot": 1,
                    "filename": "deadline.gb",
                    "sha256": "e" * 64,
                    "game_type": "deadline_gb",
                    "region": "JP",
                },
                {
                    "slot": 2,
                    "filename": "deadline.z64",
                    "sha256": "f" * 64,
                    "game_type": "deadline_n64",
                    "region": "JP",
                },
            ]},
            token=token,
        )["slots"]
        gb_save, n64_save = slots[0], slots[1]
        self.app.sessions.policy = RoomSessionPolicy(
            gb_local_seconds=101,
            gb_mobile_seconds=102,
            n64_local_seconds=103,
        )

        def duration_seconds(lock: dict) -> int:
            return int(
                (
                    datetime.fromisoformat(lock["expires_at"])
                    - datetime.fromisoformat(lock["started_at"])
                ).total_seconds()
            )

        local = self.post(
            "/game/start",
            {"execution_mode": "LOCAL_CLIENT", "save_ids": [gb_save["save_id"]]},
            token=token,
        )["game_session"]
        self.assertEqual(duration_seconds(local), 101)
        heartbeat = self.post(
            "/game/heartbeat", self.game_fence({"game_session": local}), token=token
        )["game_session"]
        self.assertEqual(heartbeat["expires_at"], local["expires_at"])
        self.post(
            "/game/stop", self.game_fence({"game_session": local}), token=token
        )

        n64 = self.post(
            "/game/start",
            {
                "execution_mode": "N64_RUNTIME_CLIENT",
                "save_ids": [n64_save["save_id"], gb_save["save_id"]],
            },
            token=token,
        )["game_session"]
        self.assertEqual(duration_seconds(n64), 103)
        self.post(
            "/game/stop", self.game_fence({"game_session": n64}), token=token
        )

        mobile = self.app.sessions.acquire_local_game_session_lock(
            gb_save["user_id"],
            self.app.game_session_lease_expires_at(),
            hashlib.sha256(token.encode()).hexdigest(),
            execution_mode="MOBILE_CLIENT",
            saves=[self.app.saves.get_save(gb_save["save_id"], gb_save["user_id"])],
        )
        self.assertEqual(duration_seconds(mobile.to_dict()), 102)
        self.app.sessions.release_user_game_session_lock(
            gb_save["user_id"],
            hashlib.sha256(token.encode()).hexdigest(),
            mobile.game_session_id,
            mobile.fencing_token,
        )

    def test_local_game_start_rejects_duplicate_and_n64_saves(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        gb_save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"gb-save")}, token=token)["save"]
        n64_save = self.post(
            "/rom-slots/apply",
            {"slots": [{
                "slot": 1, "filename": "sample_console.z64", "sha256": "a" * 64,
                "game_type": "custom_console", "region": "US",
            }]},
            token=token,
        )["slots"][0]

        with self.assertRaisesRegex(ValidationError, "save_ids must be unique"):
            self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [gb_save["id"], gb_save["id"]]}, token=token)
        with self.assertRaisesRegex(ValidationError, "local play requires GB/GBC saves"):
            self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [n64_save["save_id"]]}, token=token)

    def test_local_game_heartbeat_renews_owner_lock(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        owner_token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        other_token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"gb-save")}, token=owner_token)["save"]
        started = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]}, token=owner_token)
        old_heartbeat = "2000-01-01T00:00:00+00:00"
        self.set_game_lock_times(
            save["user_id"], last_heartbeat=old_heartbeat,
            lease_expires_at=future_lock_expires_at(),
        )

        with self.assertRaisesRegex(ValidationError, "different auth session"):
            self.post("/game/heartbeat", self.game_fence(started), token=other_token)
        renewed = self.post("/game/heartbeat", self.game_fence(started), token=owner_token)

        self.assertEqual(renewed["game_session"]["game_session_id"], started["game_session"]["game_session_id"])
        self.assertEqual(renewed["game_session"]["execution_mode"], "LOCAL_CLIENT")
        self.assertNotEqual(renewed["game_session"]["last_heartbeat"], old_heartbeat)
        self.assertGreater(datetime.fromisoformat(renewed["game_session"]["lease_expires_at"]), datetime.now(timezone.utc))

    def test_local_game_heartbeat_rejects_expired_lock(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"gb-save")}, token=token)["save"]
        started = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]}, token=token)
        self.set_game_lock_times(
            save["user_id"], lease_expires_at="2000-01-01T00:00:00+00:00"
        )

        with self.assertRaisesRegex(ValidationError, "game session lock expired"):
            self.post("/game/heartbeat", self.game_fence(started), token=token)
        with self.assertRaisesRegex(ValidationError, "game session lock expired"):
            self.put(
                f"/saves/{save['id']}",
                {"expected_revision": 1, "save_data": encode(b"expired-writer"), **self.game_fence(started)},
                token=token,
            )

    def test_local_game_save_writes_and_lifecycle_are_fenced(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        first_token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        other_token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"revision-one")}, token=first_token)["save"]
        first = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]}, token=first_token)
        first_fence = self.game_fence(first)

        self.assertEqual(
            first["game_session"]["save_bindings"],
            [{"save_id": save["id"], "revision": 1, "sha256": save["sha256"]}],
        )
        with self.assertRaisesRegex(ValidationError, "different auth session"):
            self.put(
                f"/saves/{save['id']}",
                {"expected_revision": 1, "save_data": encode(b"other-token"), **first_fence},
                token=other_token,
            )
        updated = self.put(
            f"/saves/{save['id']}",
            {"expected_revision": 1, "save_data": encode(b"first-session"), **first_fence},
            token=first_token,
        )["save"]
        self.assertEqual(updated["revision"], 2)

        self.set_game_lock_times(
            save["user_id"], lease_expires_at="2000-01-01T00:00:00+00:00"
        )
        second = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]}, token=first_token)
        second_fence = self.game_fence(second)

        with self.assertRaisesRegex(ValidationError, "stale game session fence"):
            self.post("/game/heartbeat", first_fence, token=first_token)
        with self.assertRaisesRegex(ValidationError, "stale game session fence"):
            self.put(
                f"/saves/{save['id']}",
                {"expected_revision": 2, "save_data": encode(b"stale-writer"), **first_fence},
                token=first_token,
            )
        with self.assertRaisesRegex(ValidationError, "stale game session fence"):
            self.post("/game/stop", first_fence, token=first_token)

        active = self.get("/game/status", token=first_token)
        self.assertEqual(active["game_session"]["game_session_id"], second["game_session"]["game_session_id"])
        self.post("/game/stop", second_fence, token=first_token)

    def test_active_game_blocks_admin_replace_and_rom_slot_changes(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"revision-one")}, token=token)["save"]
        started = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]}, token=token)

        with self.assertRaisesRegex(ValidationError, "active game session"):
            self.post(f"/admin/saves/{save['id']}/replace", {"save_data": encode(b"admin-write")})
        with self.assertRaisesRegex(ValidationError, "active game session"):
            self.post("/rom-slots/apply", {"slots": []}, token=token)

        self.post("/game/stop", self.game_fence(started), token=token)

    def test_delayed_link_stop_cannot_cancel_replacement_link_session(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"a-save")}, token=token)["save"]
        save_b = self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"b-save")}, token=token)["save"]

        first = self.create_link_session_direct(
            save_a["user_id"],
            save_a["user_id"],
            save_a["id"],
            save_b["id"],
            auth_session_ids={save_a["user_id"]: token},
        )
        self.app.sessions.transition(first["id"], LinkSessionStatus.RUNNING)
        first_fence = self.game_fence(self.get("/game/status", token=token))
        self.post("/game/stop", first_fence, token=token)

        second = self.create_link_session_direct(
            save_a["user_id"],
            save_a["user_id"],
            save_a["id"],
            save_b["id"],
            auth_session_ids={save_a["user_id"]: token},
        )
        self.app.sessions.transition(second["id"], LinkSessionStatus.RUNNING)
        second_fence = self.game_fence(self.get("/game/status", token=token))

        with self.assertRaisesRegex(ValidationError, "stale game session fence"):
            self.post("/game/stop", first_fence, token=token)

        active = self.get("/game/status", token=token)
        self.assertEqual(active["link_session"]["id"], second["id"])
        self.assertEqual(active["link_session"]["status"], LinkSessionStatus.RUNNING.value)
        self.post("/game/stop", second_fence, token=token)

    def test_n64_runtime_game_start_accepts_n64_plus_transfer_pak_saves(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        slots = self.post(
            "/rom-slots/apply",
            {"slots": [
                {"slot": 1, "filename": "sample_console.z64", "sha256": "a" * 64,
                 "game_type": "custom_console", "region": "US"},
                {"slot": 2, "filename": "custom.gbc", "sha256": "b" * 64,
                 "game_type": "custom_gb", "region": "US"},
            ]},
            token=token,
        )["slots"]
        n64_save, gb_save = slots[0], slots[1]

        started = self.post(
            "/game/start",
            {"execution_mode": "N64_RUNTIME_CLIENT", "save_ids": [n64_save["save_id"], gb_save["save_id"]]},
            token=token,
        )
        heartbeat = self.post("/game/heartbeat", self.game_fence(started), token=token)
        stopped = self.post("/game/stop", self.game_fence(started), token=token)

        self.assertEqual(started["game_session"]["execution_mode"], "N64_RUNTIME_CLIENT")
        self.assertEqual(heartbeat["game_session"]["execution_mode"], "N64_RUNTIME_CLIENT")
        self.assertEqual(stopped["game_session"]["execution_mode"], "N64_RUNTIME_CLIENT")

    def test_n64_runtime_game_start_requires_n64_first_and_unique_saves(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        slots = self.post(
            "/rom-slots/apply",
            {"slots": [
                {"slot": 1, "filename": "sample_console.n64", "sha256": "c" * 64,
                 "game_type": "custom_n64", "region": "EU"},
                {"slot": 2, "filename": "custom.gb", "sha256": "d" * 64,
                 "game_type": "custom_gb", "region": "EU"},
            ]},
            token=token,
        )["slots"]
        n64_save, gb_save = slots[0], slots[1]

        with self.assertRaisesRegex(ValidationError, "N64 Runtime requires an N64 ROM save"):
            self.post(
                "/game/start",
                {"execution_mode": "N64_RUNTIME_CLIENT", "save_ids": [gb_save["save_id"], n64_save["save_id"]]},
                token=token,
            )
        with self.assertRaisesRegex(ValidationError, "save_ids must be unique"):
            self.post(
                "/game/start",
                {"execution_mode": "N64_RUNTIME_CLIENT", "save_ids": [n64_save["save_id"], n64_save["save_id"]]},
                token=token,
            )


    def test_unknown_server_environment_is_rejected(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})

        with self.assertRaisesRegex(ValidationError, "server_id is not supported"):
            self.post(
                "/auth/login",
                {"username": "player_a", "password": "correct horse battery staple", "server_id": "unknown"},
            )

    def test_stale_save_upload_returns_conflict(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"one")}, token=token)["save"]
        self.put(f"/saves/{save['id']}", {"expected_revision": 1, "save_data": encode(b"two")}, token=token)

        with self.assertRaises(RevisionConflictError):
            self.put(f"/saves/{save['id']}", {"expected_revision": 1, "save_data": encode(b"stale")}, token=token)

    def test_save_upload_request_id_replays_first_success(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"one")}, token=token)["save"]
        request = {
            "expected_revision": 1,
            "save_data": encode(b"two"),
            "request_id": "save-upload-response-loss-1",
        }

        first = self.put(f"/saves/{save['id']}", request, token=token)
        replay = self.put(f"/saves/{save['id']}", request, token=token)

        self.assertEqual(first["save"]["revision"], 2)
        self.assertEqual(replay["save"], first["save"])
        self.assertTrue(replay["idempotent_replay"])
        self.assertEqual(self.get(f"/saves/{save['id']}", token=token)["save"]["revision"], 2)
        with self.assertRaisesRegex(ValidationError, "different save upload"):
            self.put(
                f"/saves/{save['id']}",
                {**request, "save_data": encode(b"different")},
                token=token,
            )

    def test_committed_save_receipt_after_game_end_and_new_login(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"one")}, token=token)["save"]
        game = self.post("/game/start", {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]}, token=token)
        payload = {"expected_revision": 1, "save_data": encode(b"two"), "request_id": "f5-lost-response"}
        first = self.put(f"/saves/{save['id']}", {**payload, **self.game_fence(game)}, token=token)
        # Unfenced recovery cannot interfere with a still-running monitor.
        with self.assertRaises(GameSessionFenceError):
            self.put(f"/saves/{save['id']}", payload, token=token)
        self.post("/game/stop", self.game_fence(game), token=token)
        token2 = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        replay = self.put(f"/saves/{save['id']}", payload, token=token2)
        self.assertTrue(replay["idempotent_replay"])
        self.assertEqual(first["save"], replay["save"])
        latest = self.put(f"/saves/{save['id']}", {"expected_revision": 2,
            "save_data": encode(b"three"), "request_id": "f5-latest"}, token=token2)
        self.assertEqual(latest["save"]["revision"], 3)
        replay = self.put(f"/saves/{save['id']}", payload, token=token2)
        self.assertEqual(replay["save"]["revision"], 2)
        self.assertEqual(self.get(f"/saves/{save['id']}", token=token2)["save"]["revision"], 3)
        with self.assertRaises(ValidationError):
            self.put(f"/saves/{save['id']}", {**payload, "save_data": encode(b"bad")}, token=token2)
        self.post("/auth/register", {"username": "Player_B", "password": "correct horse battery staple"})
        other = self.post("/auth/login", {"username": "player_b", "password": "correct horse battery staple"})["token"]["token"]
        with self.assertRaises(NotFoundError):
            self.put(f"/saves/{save['id']}", payload, token=other)

    def test_save_upload_request_recovers_after_commit_before_result_record(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"one")}, token=token)["save"]
        request = {
            "expected_revision": 1,
            "save_data": encode(b"two"),
            "request_id": "crash-after-save-commit",
        }
        injected = False

        def fail_committed_request_record(point: str) -> None:
            nonlocal injected
            if point == "before_response" and not injected:
                injected = True
                raise RuntimeError("injected after save commit")

        with patch.object(self.app.save_uploads, "fault_injector", side_effect=fail_committed_request_record):
            with self.assertRaisesRegex(RuntimeError, "injected after save commit"):
                self.put(f"/saves/{save['id']}", request, token=token)

        self.app = LeagueApplication(Path(self.temp_dir.name))
        replay = self.put(f"/saves/{save['id']}", request, token=token)

        self.assertTrue(replay["idempotent_replay"])
        self.assertEqual(replay["save"]["revision"], 2)
        self.assertEqual(decode(self.get(f"/saves/{save['id']}", token=token)["save_data"]), b"two")

    def test_save_upload_request_rolls_forward_after_bytes_before_metadata(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"one")}, token=token)["save"]
        request = {
            "expected_revision": 1,
            "save_data": encode(b"two"),
            "request_id": "crash-before-save-metadata",
        }
        injected = False

        def fail_save_metadata(point: str) -> None:
            nonlocal injected
            if point == "after_atomic_replace" and not injected:
                injected = True
                raise RuntimeError("injected before save metadata")

        with patch.object(self.app.save_uploads, "fault_injector", side_effect=fail_save_metadata):
            with self.assertRaisesRegex(RuntimeError, "injected before save metadata"):
                self.put(f"/saves/{save['id']}", request, token=token)

        backup_path = self.app.saves.storage.backups_dir / save["user_id"] / save["id"] / "backup_0001.sav"
        self.assertEqual(backup_path.read_bytes(), b"one")
        self.app = LeagueApplication(Path(self.temp_dir.name))
        completed = self.put(f"/saves/{save['id']}", request, token=token)

        self.assertEqual(completed["save"]["revision"], 2)
        self.assertEqual(backup_path.read_bytes(), b"one")
        self.assertEqual(decode(self.get(f"/saves/{save['id']}", token=token)["save_data"]), b"two")

    def test_save_upload_request_retries_prepared_request_before_commit(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"one")}, token=token)["save"]
        request = {
            "expected_revision": 1,
            "save_data": encode(b"two"),
            "request_id": "crash-after-prepare",
        }

        def fail_after_prepare(point: str) -> None:
            if point == "after_prepare":
                raise RuntimeError("injected after prepare")

        with patch.object(self.app.save_uploads, "fault_injector", side_effect=fail_after_prepare):
            with self.assertRaisesRegex(RuntimeError, "injected after prepare"):
                self.put(f"/saves/{save['id']}", request, token=token)

        self.app = LeagueApplication(Path(self.temp_dir.name))
        completed = self.put(f"/saves/{save['id']}", request, token=token)

        self.assertEqual(completed["save"]["revision"], 2)
        self.assertEqual(decode(self.get(f"/saves/{save['id']}", token=token)["save_data"]), b"two")

    def test_admin_can_issue_user_and_view_data_status(self) -> None:
        issued = self.post("/admin/users/issue", {"username": "TestUser001", "email": "TestUser001@Example.COM"})
        username = issued["user"]["username"]
        password = issued["initial_password"]
        self.assertEqual(username, "TestUser001")
        self.assertEqual(issued["user"]["email"], "testuser001@example.com")
        self.assertEqual(len(password), 12)
        self.assertTrue(password.isalnum())
        self.assertNotIn("password_hash", issued["user"])
        self.assertTrue(issued["user"]["must_change_password"])

        token = self.post("/auth/login", {"username": username, "password": password})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"admin-save")}, token=token)["save"]

        users = self.get("/admin/users")["users"]
        summary = next(item for item in users if item["user"]["username"] == username)

        self.assertEqual(summary["counts"]["saves"], 1)
        self.assertEqual(summary["user"]["email"], "testuser001@example.com")
        self.assertEqual(summary["saves"][0]["id"], save["id"])
        self.assertEqual(summary["counts"]["rom_slots"], 0)

    def test_admin_can_reset_user_password(self) -> None:
        issued = self.post("/admin/users/issue", {"username": "ResetUser001", "email": "reset@example.com"})
        username = issued["user"]["username"]
        old_password = issued["initial_password"]
        old_token_a = self.post(
            "/auth/login", {"username": username, "password": old_password}
        )["token"]["token"]
        old_token_b = self.post(
            "/auth/login", {"username": username, "password": old_password}
        )["token"]["token"]

        reset = self.post("/admin/users/reset-password", {"username": username})
        new_password = reset["initial_password"]

        self.assertEqual(reset["user"]["username"], username)
        self.assertEqual(len(new_password), 12)
        self.assertTrue(new_password.isalnum())
        self.assertNotEqual(new_password, old_password)
        self.assertTrue(reset["user"]["must_change_password"])

        with self.assertRaisesRegex(Exception, "invalid username or password"):
            self.post("/auth/login", {"username": username, "password": old_password})
        for token in (old_token_a, old_token_b):
            with self.assertRaisesRegex(AuthenticationError, "invalid token"):
                self.get("/me", token=token)
        login = self.post("/auth/login", {"username": username, "password": new_password})
        self.assertTrue(login["user"]["must_change_password"])

    def test_admin_password_reset_rejects_missing_user_without_change(self) -> None:
        before = self.app.auth.list_users()
        with self.assertRaisesRegex(ValidationError, "username not found"):
            self.post("/admin/users/reset-password", {"username": "missing001"})
        self.assertEqual(self.app.auth.list_users(), before)

    def test_admin_password_reset_rejects_user_in_room_without_change(self) -> None:
        issued = self.post("/admin/users/issue", {"username": "RoomReset001"})
        password = issued["initial_password"]
        token = self.post(
            "/auth/login", {"username": "roomreset001", "password": password}
        )["token"]["token"]
        self.join_room_fixture(1, token=token)

        with self.assertRaisesRegex(ValidationError, "participating in a ROOM"):
            self.post("/admin/users/reset-password", {"username": "roomreset001"})

        self.assertEqual(self.get("/me", token=token)["user"]["username"], "RoomReset001")
        self.assertEqual(
            self.post(
                "/auth/login", {"username": "roomreset001", "password": password}
            )["user"]["username"],
            "RoomReset001",
        )

    def test_admin_password_reset_rejects_user_running_game_without_change(self) -> None:
        issued = self.post("/admin/users/issue", {"username": "GameReset001"})
        password = issued["initial_password"]
        token = self.post(
            "/auth/login", {"username": "gamereset001", "password": password}
        )["token"]["token"]
        save = self.create_test_save(
            {"game_type": "sample_alpha", "save_data": encode(b"game-reset")},
            token=token,
        )["save"]
        self.post(
            "/game/start",
            {"execution_mode": "LOCAL_CLIENT", "save_ids": [save["id"]]},
            token=token,
        )

        with self.assertRaisesRegex(ValidationError, "currently running a game"):
            self.post("/admin/users/reset-password", {"username": "gamereset001"})

        self.assertEqual(self.get("/me", token=token)["user"]["username"], "GameReset001")
        self.assertEqual(
            self.post(
                "/auth/login", {"username": "gamereset001", "password": password}
            )["user"]["username"],
            "GameReset001",
        )

    def test_admin_can_replace_save_upload_from_file_data(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        save = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"old-save")}, token=token)["save"]
        self.app.saves.lock_save(
            save["id"], save["user_id"], owner="active_session",
            expires_at=future_lock_expires_at(),
            auth_session_id=hashlib.sha256(token.encode()).hexdigest(),
            fencing_token=1,
        )

        replaced = self.post(
            f"/admin/saves/{save['id']}/replace",
            {"save_data": encode(b"new-save-from-admin")},
        )["save"]
        downloaded = self.get(f"/saves/{save['id']}", token=token)

        self.assertEqual(replaced["revision"], 2)
        self.assertIsNone(replaced["lock_owner"])
        self.assertEqual(decode(downloaded["save_data"]), b"new-save-from-admin")
        backup_path = self.app.storage.backups_dir / save["user_id"] / save["id"] / "backup_0001.sav"
        self.assertEqual(backup_path.read_bytes(), b"old-save")

    def test_time_endpoint_reports_server_unix_time(self) -> None:
        before = int(datetime.now(timezone.utc).timestamp())
        result = self.get("/time")
        after = int(datetime.now(timezone.utc).timestamp())

        self.assertGreaterEqual(result["server_time"]["unix_time"], before)
        self.assertLessEqual(result["server_time"]["unix_time"], after)

    def test_http_handler_rejects_oversized_json_body(self) -> None:
        handler_class = create_handler(self.app)
        handler = handler_class.__new__(handler_class)
        headers = Message()
        headers["Content-Length"] = str(MAX_JSON_BODY_BYTES + 1)
        handler.headers = headers
        handler.rfile = io.BytesIO(b"")

        with self.assertRaisesRegex(RequestBodyTooLarge, "request body must be"):
            handler._read_json()

        headers["Content-Length"] = str(AUTH_JSON_BODY_BYTES + 1)
        with self.assertRaisesRegex(RequestBodyTooLarge, str(AUTH_JSON_BODY_BYTES)):
            handler._read_json(AUTH_JSON_BODY_BYTES)

    def test_gb_runtime_fixed_host_runtime_snapshot_http_headers_disable_storage(self) -> None:
        handler_class = create_handler(self.app)
        headers = dict(handler_class._sensitive_json_headers(
            "/gb-runtime-fixed-host-sessions/link_123/runtime-snapshots"
        ))
        self.assertEqual(headers["Cache-Control"], "no-store")
        self.assertEqual(headers["Pragma"], "no-cache")
        self.assertEqual(headers["X-Content-Type-Options"], "nosniff")
        self.assertEqual(handler_class._sensitive_json_headers("/time"), ())

    def test_auth_rate_limits_are_scoped_by_ip_and_account(self) -> None:
        for attempt in range(10):
            self.app.enforce_request_rate_limit(
                f"192.0.2.{attempt}",
                "/auth/login",
                {"username": "Player_A"},
                now=1.0,
            )
        with self.assertRaisesRegex(RateLimitExceeded, "too many authentication attempts"):
            self.app.enforce_request_rate_limit(
                "192.0.2.250",
                "/auth/login",
                {"username": "player_a"},
                now=1.0,
            )
        self.app.enforce_request_rate_limit(
            "192.0.2.250",
            "/auth/login",
            {"username": "player_a"},
            now=62.0,
        )

    def test_admin_issue_user_rejects_duplicate_username(self) -> None:
        self.post("/admin/users/issue", {"username": "Player001", "email": "player001@example.com"})

        with self.assertRaisesRegex(DuplicateUserError, "username is already registered"):
            self.post("/admin/users/issue", {"username": "player001", "email": "player001b@example.com"})

    def test_admin_issue_user_accepts_empty_email(self) -> None:
        without_field = self.post("/admin/users/issue", {"username": "NoEmail001"})
        whitespace = self.post(
            "/admin/users/issue", {"username": "NoEmail002", "email": "   "}
        )

        self.assertEqual(without_field["user"]["email"], "")
        self.assertEqual(whitespace["user"]["email"], "")

    def test_admin_issue_user_rejects_invalid_nonempty_email(self) -> None:
        with self.assertRaisesRegex(ValidationError, "email is invalid"):
            self.post(
                "/admin/users/issue",
                {"username": "BadEmail001", "email": "not-an-email"},
            )

    def test_admin_session_requires_configured_password(self) -> None:
        with self.assertRaisesRegex(AuthenticationError, "admin password is not configured"):
            self.post("/admin/session", {"password": "anything"})

    def test_admin_session_validates_password(self) -> None:
        self.app.admin_password = "AdminPass123"

        with self.assertRaisesRegex(AuthenticationError, "invalid admin password"):
            self.post("/admin/session", {"password": "wrong"})

        created = self.post("/admin/session", {"password": "AdminPass123"})
        token = created["admin_session"]["token"]
        self.app.require_admin_session(token)
        self.app.destroy_admin_session(token)
        with self.assertRaisesRegex(AuthenticationError, "admin login required"):
            self.app.require_admin_session(token)

    def test_admin_html_is_available_as_asset(self) -> None:
        html = render_admin_html("/sample-api/admin")
        login_html = render_admin_login_html("/sample-api/admin")
        self.assertIn("INTEGRAL EMULATOR Admin", html)
        self.assertIn('const ADMIN_BASE_PATH = "/sample-api/admin";', html)
        self.assertIn('const ADMIN_BASE_PATH = "/sample-api/admin";', login_html)
        self.assertNotIn('fetch("/admin', html)
        self.assertNotIn('fetch("/admin', login_html)
        self.assertNotIn('location.href = "/admin"', html)
        self.assertNotIn('location.href = "/admin"', login_html)
        self.assertIn("Replace SAV", html)
        self.assertIn("User Data", html)
        self.assertIn("Operations", html)
        self.assertIn("Server Logs", html)

    def test_public_base_path_validation(self) -> None:
        self.assertEqual(normalized_public_base_path(" /sample-api "), "/sample-api")
        self.assertEqual(normalized_public_base_path(""), "")
        for invalid in ("sample-api", "/sample-api/", "//sample-api", "/bad path"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValidationError):
                    normalized_public_base_path(invalid)

    def test_admin_http_uses_configured_public_base_path(self) -> None:
        self.app.admin_password = "AdminPass123"
        self.app.public_base_path = "/sample-api"
        self.app.admin_base_path = "/sample-api/admin"
        self.app.write_server_log("INFO", "public admin test")
        server = ThreadingHTTPServer(("127.0.0.1", 0), create_handler(self.app))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def request(method: str, path: str, body: dict | None = None, cookie: str = ""):
            connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
            payload = None if body is None else json.dumps(body).encode("utf-8")
            headers = {}
            if payload is not None:
                headers["Content-Type"] = "application/json"
            if cookie:
                headers["Cookie"] = cookie
            connection.request(method, path, body=payload, headers=headers)
            response = connection.getresponse()
            data = response.read()
            response_headers = dict(response.getheaders())
            connection.close()
            return response.status, response_headers, data

        try:
            for path in ("/", "/?console=1"):
                status, _headers, data = request("GET", path)
                self.assertEqual(status, 404)
                self.assertNotIn(b"Local Console", data)
                self.assertNotIn(b"<script", data)
            status, _headers, data = request("GET", "/health")
            self.assertEqual(status, 200)
            self.assertEqual(json.loads(data), {"ok": True})
            for path in ("/admin/users", "/admin/operations", "/me"):
                status, _headers, _data = request("GET", path)
                self.assertEqual(status, 401)
            status, _headers, login_page = request("GET", "/admin")
            self.assertEqual(status, 200)
            self.assertIn(b'const ADMIN_BASE_PATH = "/sample-api/admin";', login_page)

            status, headers, _data = request(
                "POST", "/admin/session", {"password": "AdminPass123"}
            )
            self.assertEqual(status, 200)
            set_cookie = headers["Set-Cookie"]
            self.assertTrue(set_cookie.startswith("integral_admin_session="))
            self.assertNotIn("gsc" + "_admin_session", set_cookie)
            self.assertIn("Path=/sample-api", set_cookie)
            self.assertIn("Secure", set_cookie)
            self.assertNotIn("Path=/admin", set_cookie)
            cookie = set_cookie.split(";", 1)[0]

            for path in ("/admin/users", "/admin/operations"):
                status, _headers, _data = request("GET", path, cookie=cookie)
                self.assertEqual(status, 200, path)
            status, _headers, logs_data = request("GET", "/admin/logs", cookie=cookie)
            self.assertEqual(status, 200)
            logs = json.loads(logs_data)["log"]
            self.assertEqual(logs["download_url"], "/sample-api/admin/logs/download")
            status, _headers, download = request(
                "GET", "/admin/logs/download", cookie=cookie
            )
            self.assertEqual(status, 200)
            self.assertIn(b"public admin test", download)

            status, headers, _data = request("POST", "/admin/logout", {}, cookie=cookie)
            self.assertEqual(status, 200)
            self.assertIn("Path=/sample-api", headers["Set-Cookie"])
            self.assertIn("Max-Age=0", headers["Set-Cookie"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)

    def test_admin_can_view_server_logs(self) -> None:
        self.app.write_server_log("INFO", "server started")
        self.app.write_server_log("WARN", "sample warning")

        result = self.get("/admin/logs")

        self.assertEqual(result["log"]["download_url"], "/admin/logs/download")
        self.assertTrue(result["log"]["path"].endswith("logs/server.log"))
        self.assertTrue(any("server started" in line for line in result["log"]["lines"]))
        self.assertTrue(any("sample warning" in line for line in result["log"]["lines"]))

    def test_admin_operations_summary_lists_rooms_hosts_and_events(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        self.post("/auth/register", {"username": "Player_B", "password": "another correct password"})
        token_b = self.post("/auth/login", {"username": "player_b", "password": "another correct password"})["token"]["token"]
        user_b = self.app.auth.require_user(token_b)
        save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"a-save")}, token=token_a)["save"]
        save_b = self.app.saves.create_save(user_b.id, "sample_beta", b"b-save")
        session = self.create_link_session_direct(save_a["user_id"], user_b.id, save_a["id"], save_b.id)
        self.join_room_fixture(1, token=token_a)
        self.join_room_fixture(1, token=token_b)
        self.app.room_manager.set_link_session(1, session["id"])
        self.app.room_manager.end_game_for_link_session(session["id"])
        host = self.app.host_processes.create_run(session["id"], ["integral_gb_runtime_dual_server", "--help"], start=False)
        self.app.sessions.attach_host_process(session["id"], host.id, replace_existing=True)

        summary = self.get("/admin/operations")

        self.assertEqual(summary["rooms"][0]["room_status"], "OPEN")
        self.assertIn("sweeper_lag_seconds", summary["lifecycle"])
        self.assertEqual(summary["lifecycle"]["active_game_locks"], 2)
        self.assertEqual(summary["lifecycle"]["gb_runtime_fixed_host_rollout"]["stage"], "default")
        self.assertEqual(summary["lifecycle"]["gb_runtime_fixed_host_sessions"]["total"], 0)
        self.assertIsNone(summary["rooms"][0]["link_session_id"])
        self.assertEqual(summary["host_processes"][0]["command"], ["<redacted>"])
        self.assertEqual(summary["link_sessions"][0]["id"], session["id"])
        self.assertTrue(any(event["event_type"] == "SESSION_CREATED" for event in summary["events"]))

    def test_room_lists_rooms_and_tracks_joined_users(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Player_B", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Player_C", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "player_b", "password": "correct horse battery staple"})["token"]["token"]
        token_c = self.post("/auth/login", {"username": "player_c", "password": "correct horse battery staple"})["token"]["token"]

        room_a = self.join_room_fixture(1, token=token_a)["room"]
        room_b = self.join_room_fixture(1, token=token_b)["room"]
        self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_b)

        self.assertEqual([user["username"] for user in room_a["users"]], ["Player_A"])
        self.assertEqual([user["username"] for user in room_b["users"]], ["Player_A", "Player_B"])
        with self.assertRaisesRegex(RoomCodeUnavailableError, "not available"):
            self.join_room_fixture(1, token=token_c)

        moved = self.join_room_fixture(2, token=token_a)["room"]
        rooms = self.list_rooms_fixture(token=token_a)

        self.assertEqual([user["username"] for user in moved["users"]], ["Player_A"])
        self.assertEqual(rooms[0]["users"], [])
        self.assertEqual([user["username"] for user in rooms[1]["users"]], ["Player_A"])
        self.assertEqual(len(rooms), 32)
        self.assertEqual([room["room_number"] for room in rooms[-16:]], list(range(65, 81)))

        left = self.post("/room-matching/leave", {}, token=token_a)
        rooms = self.list_rooms_fixture(token=token_a)

        self.assertTrue(left["left"])
        self.assertEqual(rooms[1]["users"], [])

    def test_room_matching_create_join_current_and_member_only_code(self) -> None:
        self.post("/auth/register", {"username": "RoomHost", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "RoomGuest", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "RoomOther", "password": "correct horse battery staple"})
        host = self.post("/auth/login", {"username": "roomhost", "password": "correct horse battery staple"})["token"]["token"]
        guest = self.post("/auth/login", {"username": "roomguest", "password": "correct horse battery staple"})["token"]["token"]
        other = self.post("/auth/login", {"username": "roomother", "password": "correct horse battery staple"})["token"]["token"]

        created = self.post("/room-matching/create", {"mode": "link_cable"}, token=host)["room"]
        self.assertIn(created["room_number"], range(1, 17))
        self.assertRegex(created["room_code"], r"^[1-9][0-9]{4}$")
        self.assertEqual(created["room_type"], "link_cable")
        self.assertTrue(created["creator"])
        self.assertEqual([user["username"] for user in created["users"]], ["RoomHost"])

        current = self.get("/room-matching/current", token=host)["room"]
        self.assert_no_auth_session_digest(created)
        self.assert_no_auth_session_digest(current)
        self.assertEqual(current["room_code"], created["room_code"])
        joined = self.post(
            "/room-matching/join", {"room_code": created["room_code"]}, token=guest
        )["room"]
        self.assert_no_auth_session_digest(joined)
        self.assertEqual(joined["room_number"], created["room_number"])
        self.assertFalse(joined["creator"])
        self.assertEqual(len(joined["users"]), 2)
        self.assertEqual(
            self.get("/room-matching/current", token=guest)["room"]["room_code"],
            created["room_code"],
        )
        self.assertIsNone(self.get("/room-matching/current", token=other)["room"])
        public_room = self.list_rooms_fixture(token=other)[created["room_number"] - 1]
        self.assertNotIn("room_code", public_room)
        self.assertNotIn("creator_user_id", public_room)
        with self.assertRaises(NotFoundError):
            self.post(f"/rooms/{created['room_number']}", {}, token=other)
        with self.assertRaises(NotFoundError):
            self.get("/lobby", token=other)
        with self.assertRaises(NotFoundError):
            self.post("/lobby/heartbeat", {}, token=other)
        with self.assertRaises(NotFoundError):
            self.post(
                f"/lobby/rooms/{created['room_number']}/state",
                {"ready": False},
                token=host,
            )
        repeated = self.get("/room-matching/current", token=host)["room"]
        self.assertTrue(repeated["creator"])
        self.assertEqual(len(repeated["users"]), 2)
        self.assert_no_auth_session_digest(repeated)

    def test_link_room_members_receive_selected_rom_header_titles_only(self) -> None:
        self.post("/auth/register", {"username": "LinkHost", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "LinkGuest", "password": "correct horse battery staple"})
        host = self.post("/auth/login", {"username": "linkhost", "password": "correct horse battery staple"})["token"]["token"]
        guest = self.post("/auth/login", {"username": "linkguest", "password": "correct horse battery staple"})["token"]["token"]

        alpha = SYNTHETIC_ROMS[0]
        beta = SYNTHETIC_ROMS[1]
        self.post(
            "/rom-slots/apply",
            {"slots": [
                {"slot": 1, "filename": "host-alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": alpha.game_type, "region": "JP"},
            ]},
            token=host,
        )
        self.post(
            "/rom-slots/apply",
            {"slots": [
                {"slot": 3, "filename": "guest-beta.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": beta.game_type, "region": "JP"},
            ]},
            token=guest,
        )

        created = self.post("/room-matching/create", {"mode": "link_cable"}, token=host)["room"]
        self.post("/room-matching/join", {"room_code": created["room_code"]}, token=guest)
        self.post(f"/rooms/{created['room_number']}/state", {"slot": "ROM1", "ready": False}, token=host)
        guest_view = self.post(
            f"/rooms/{created['room_number']}/state",
            {"slot": "ROM3", "ready": False},
            token=guest,
        )["room"]

        self.assertEqual(
            [user["slot_rom_header_title"] for user in guest_view["users"]],
            [alpha.rom_header_title, beta.rom_header_title],
        )
        self.assertNotIn("slot_filename", guest_view["users"][0])
        self.assertNotIn("slot_filename", guest_view["users"][1])

    def test_room_matching_n64_pool_and_fields_survive_room_updates(self) -> None:
        self.post("/auth/register", {"username": "N64Host", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "n64host", "password": "correct horse battery staple"})["token"]["token"]
        created = self.post("/room-matching/create", {"mode": "n64"}, token=token)["room"]
        self.assertIn(created["room_number"], range(65, 81))
        code = created["room_code"]

        updated = self.post(
            f"/rooms/{created['room_number']}/state",
            {"ready": False},
            token=token,
        )["room"]
        self.assertEqual(updated["room_code"], code)
        chatted = self.post(
            f"/rooms/{created['room_number']}/chat",
            {"message": "hello"},
            token=token,
        )["room"]
        self.assertEqual(chatted["room_code"], code)
        self.assertEqual(self.app.room_manager.room(created["room_number"]).room_code, code)




    def test_room_matching_persists_across_application_restart_and_limits_join_attempts(self) -> None:
        self.post("/auth/register", {"username": "RestartHost", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "restarthost", "password": "correct horse battery staple"})["token"]["token"]
        created = self.post("/room-matching/create", {"mode": "link_cable"}, token=token)["room"]
        restarted = LeagueApplication(Path(self.temp_dir.name))
        persisted = restarted.handle_request(
            "GET", "/room-matching/current", bearer_token=token
        )["room"]
        self.assertEqual(persisted["room_code"], created["room_code"])

        user = self.app.auth.require_user(token)
        for attempt in range(10):
            self.app.enforce_room_matching_account_rate_limit(
                user.id, "join", now=float(attempt)
            )
        with self.assertRaises(RoomJoinRateLimitedError):
            self.app.enforce_room_matching_account_rate_limit(user.id, "join", now=10.0)
        self.app.enforce_room_matching_account_rate_limit(user.id, "join", now=611.0)


    def test_auth_logout_leaves_room(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        self.join_room_fixture(1, token=token)

        self.post("/auth/logout", {}, token=token)
        users = self.app.room_manager.list_rooms()[0].users

        self.assertEqual(users, [])

    def test_room_prunes_stale_room_users(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        self.join_room_fixture(1, token=token)
        self.app.room_manager.stale_after_seconds = 1
        with self.app.authority_database.transaction(write=True) as connection:
            connection.execute(
                "UPDATE room_members SET last_seen_at_ms = 1 WHERE server_id = 'primary' AND room_number = 1"
            )

        users = self.list_rooms_fixture(token=token)[0]["users"]

        self.assertEqual(users, [])

    def test_room_list_rooms_does_not_prune_without_application_workflow(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        self.join_room_fixture(1, token=token)
        self.app.room_manager.stale_after_seconds = 1
        with self.app.authority_database.transaction(write=True) as connection:
            connection.execute(
                "UPDATE room_members SET last_seen_at_ms = 1 WHERE server_id = 'primary' AND room_number = 1"
            )

        raw_users = self.app.room_manager.list_rooms()[0].users
        workflow_users = self.list_rooms_fixture(token=token)[0]["users"]

        self.assertEqual([user["username"] for user in raw_users], ["Player_A"])
        self.assertEqual(workflow_users, [])

    def test_room_prune_clears_remaining_user_ready_state(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Player_B", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "player_b", "password": "correct horse battery staple"})["token"]["token"]
        self.join_room_fixture(1, token=token_a)
        self.join_room_fixture(1, token=token_b)
        self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_b)
        self.app.room_manager.stale_after_seconds = 1
        user_a_id = self.app.auth.require_user(token_a).id
        user_b_id = self.app.auth.require_user(token_b).id
        now_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
        with self.app.authority_database.transaction(write=True) as connection:
            connection.execute(
                "UPDATE room_members SET last_seen_at_ms = CASE WHEN user_id = ? THEN 1 ELSE ? END WHERE server_id = 'primary' AND room_number = 1 AND user_id IN (?, ?)",
                (user_b_id, now_ms, user_a_id, user_b_id),
            )

        users = self.list_rooms_fixture(token=token_a)[0]["users"]

        self.assertEqual([user["username"] for user in users], ["Player_A"])
        self.assertEqual([user["ready"] for user in users], [False])

    def test_room_heartbeat_refreshes_room_user(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        self.join_room_fixture(1, token=token)
        present = self.post("/rooms/heartbeat", {}, token=token)["present"]

        users = self.list_rooms_fixture(token=token)[0]["users"]

        self.assertTrue(present)
        self.assertEqual(users[0]["username"], "Player_A")
        self.assertTrue(users[0]["last_seen_at"])

    def test_room_room_tracks_slot_ready_and_chat(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        self.join_room_fixture(1, token=token_a)

        state = self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_a)["room"]
        chat = self.post("/rooms/1/chat", {"message": "こんにちは"}, token=token_a)["room"]

        self.assert_no_auth_session_digest(state)
        self.assert_no_auth_session_digest(chat)
        self.assertEqual(state["users"][0]["slot"], "ROM1")
        self.assertTrue(state["users"][0]["ready"])
        self.assertEqual(chat["chat"][-1]["username"], "Player_A")
        self.assertEqual(chat["chat"][-1]["message"], "こんにちは")

    def test_room_chat_is_removed_after_everyone_leaves_and_does_not_cross_rooms(self) -> None:
        self.post("/auth/register", {"username": "Chat_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Chat_B", "password": "another correct password"})
        token_a = self.post(
            "/auth/login", {"username": "chat_a", "password": "correct horse battery staple"}
        )["token"]["token"]
        token_b = self.post(
            "/auth/login", {"username": "chat_b", "password": "another correct password"}
        )["token"]["token"]
        self.join_room_fixture(1, token=token_a)
        self.join_room_fixture(1, token=token_b)
        self.post("/rooms/1/chat", {"message": "room-one-only"}, token=token_a)

        first_left = self.post("/room-matching/leave", {}, token=token_a)
        self.assertTrue(first_left["left"])
        closed_room = self.list_rooms_fixture(token=token_b)[0]
        self.assertEqual(closed_room["users"], [])
        self.assertEqual(closed_room["chat"], [])

        second_left = self.post("/room-matching/leave", {}, token=token_b)
        self.assertTrue(second_left["left"])
        self.join_room_fixture(2, token=token_a)
        different_room = self.list_rooms_fixture(token=token_a)[1]
        self.assertEqual(different_room["chat"], [])

        self.join_room_fixture(1, token=token_a)
        reopened_original = self.list_rooms_fixture(token=token_a)[0]
        self.assertEqual(reopened_original["chat"], [])

    def test_only_user1_can_change_link_mode(self) -> None:
        self.post("/auth/register", {"username": "Mode_Owner", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Mode_Peer", "password": "another correct password"})
        token_a = self.post("/auth/login", {"username": "mode_owner", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "mode_peer", "password": "another correct password"})["token"]["token"]
        self.join_room_fixture(1, token=token_a)
        self.join_room_fixture(1, token=token_b)

        room = self.post("/rooms/1/state", {"link_mode": "trade"}, token=token_a)["room"]
        self.assertEqual(room["link_mode"], "trade")

        for alias in (
            "battle_mode",
            "trade_mode",
            "exchange_mode",
            "legacy_mode",
            "unsupported_mode",
            "automatic_mode",
            "unknown_mode",
            "BATTLE",
            " trade ",
            "",
        ):
            with self.subTest(alias=alias), self.assertRaisesRegex(
                ValidationError, "link mode must be battle or trade"
            ):
                self.post(
                    "/rooms/1/state", {"link_mode": alias}, token=token_a
                )

        with self.assertRaisesRegex(ValidationError, "only USER1 can change link mode"):
            self.post("/rooms/1/state", {"link_mode": "unsupported_mode"}, token=token_b)

        room = self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_b)["room"]
        self.assertEqual(room["link_mode"], "trade")
        self.assertTrue(room["users"][1]["ready"])

        self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_a)
        with self.assertRaisesRegex(ValidationError, "room link mode changed"):
            self.post("/rooms/1/start", {"link_mode": "battle"}, token=token_b)

    def test_n64_room_uses_host_local_remote_version_and_no_save_start_boundary(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Player_B", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "player_b", "password": "correct horse battery staple"})["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")
        beta = next(rom for rom in allowed_roms() if rom.game_type == "sample_beta" and rom.display_name == "SAMPLE BETA")

        slots_a = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "sample_console.z64", "sha256": "c" * 64, "sha1": "d" * 40, "game_type": "sample_n64", "region": "JP"},
                    {"slot": 2, "filename": "alpha-a.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"},
                ]
            },
            token=token_a,
        )["slots"]
        slots_b = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "beta-b.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"},
                    {"slot": 2, "filename": "alpha-b.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"},
                ]
            },
            token=token_b,
        )["slots"]
        self.join_room_fixture(65, token=token_a)
        self.join_room_fixture(65, token=token_b)
        host_state = self.post(
            "/rooms/65/state",
            {"n64_slot": "ROM1", "slot": "ROM2", "ready": False},
            token=token_a,
        )["room"]
        with self.assertRaisesRegex(ValidationError, "USER2 cannot select an N64 ROM"):
            self.post("/rooms/65/state", {"n64_slot": "ROM1"}, token=token_b)
        with self.assertRaisesRegex(ValidationError, "USER1 must register a matching USER2 ROM"):
            self.post("/rooms/65/state", {"slot": "ROM1", "ready": True}, token=token_b)
        slots_a = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "sample_console.z64", "sha256": "c" * 64, "sha1": "d" * 40, "game_type": "sample_n64", "region": "JP"},
                    {"slot": 2, "filename": "alpha-a.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"},
                    {"slot": 3, "filename": "beta-local-for-user2.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"},
                ]
            },
            token=token_a,
        )["slots"]
        self.post("/rooms/65/state", {"slot": "ROM1", "ready": False}, token=token_b)
        self.post("/rooms/65/state", {"ready": True}, token=token_a)
        ready_room = self.post("/rooms/65/state", {"ready": True}, token=token_b)["room"]
        self.assertEqual([user["ready"] for user in ready_room["users"]], [True, True])
        host_save_ids = {slot["save_id"] for slot in slots_a if slot.get("save_id")}
        remote_save_ids = {slot["save_id"] for slot in slots_b if slot.get("save_id")}
        save_ids = [*host_save_ids, *remote_save_ids]
        revisions_before = {
            save_id: self.get(
                f"/saves/{save_id}", token=token_a if save_id in host_save_ids else token_b
            )["save"]["revision"]
            for save_id in save_ids
        }

        self.assertEqual(host_state["room_type"], "n64")
        self.assertEqual(host_state["users"][0]["n64_slot"], "ROM1")
        self.assertEqual(host_state["users"][0]["n64_slot_filename"], "sample_console.z64")
        self.assertEqual(host_state["users"][0]["n64_slot_game_type"], "n64_sample_n64")
        self.assertEqual(host_state["users"][0]["slot_filename"], "alpha-a.gbc")
        self.assertEqual(host_state["users"][0]["slot_game_type"], "sample_alpha")
        self.assertEqual(ready_room["users"][1]["n64_slot"], "")
        self.assertEqual(ready_room["users"][1]["slot_filename"], "beta-b.gbc")
        self.assertEqual(ready_room["users"][1]["slot_game_type"], "sample_beta")
        self.assertNotIn("save_id", ready_room["users"][1])
        self.assertNotIn("rom_id", ready_room["users"][1])
        self.assertNotIn("sha256", ready_room["users"][1])
        self.assertTrue(all(user["ready"] for user in ready_room["users"]))
        self.post("/auth/register", {"username": "Player_C", "password": "correct horse battery staple"})
        token_c = self.post(
            "/auth/login",
            {"username": "player_c", "password": "correct horse battery staple"},
        )["token"]["token"]
        outsider_room = next(
            room for room in self.list_rooms_fixture(token=token_c)
            if room["room_number"] == 65
        )
        self.assertNotIn("slot_filename", outsider_room["users"][0])
        self.assertNotIn("n64_slot_filename", outsider_room["users"][0])
        host_start = self.post("/rooms/65/start", {}, token=token_a)
        records_before = self.app.storage.load_n64_runtime_media_sessions()
        with self.assertRaises(NotFoundError):
            self.post("/rooms/65/start", {"expected_media_session_id": "ended-session"}, token=token_a)
        self.assertEqual(records_before, self.app.storage.load_n64_runtime_media_sessions())
        resumed = self.post("/rooms/65/start", {
            "expected_media_session_id": host_start["media_session"]["id"],
        }, token=token_a)
        self.assertEqual(resumed["media_session"]["id"], host_start["media_session"]["id"])
        self.assertNotEqual(resumed["connection"]["ticket"], host_start["connection"]["ticket"])
        self.assertFalse(self.app.n64_runtime_media_sessions.validate_ticket(
            host_start["media_session"]["id"], "host", host_start["connection"]["ticket"]))
        host_start = resumed
        with self.assertRaisesRegex(ValidationError, "participants are not reserved"):
            self.get(
                f"/n64-runtime-media-sessions/{host_start['media_session']['id']}/runtime-saves/n64",
                token=token_a,
            )
        remote_start = self.post("/rooms/65/start", {}, token=token_b)
        self.assert_no_auth_session_digest(host_start)
        self.assert_no_auth_session_digest(remote_start)
        media_session = host_start["media_session"]
        host_ticket = host_start["connection"]["ticket"]
        remote_ticket = remote_start["connection"]["ticket"]

        self.assertEqual(media_session["id"], remote_start["media_session"]["id"])
        self.assertEqual(host_start["connection"]["role"], "host")
        self.assertEqual(remote_start["connection"]["role"], "remote")
        self.assertTrue(host_start["room"]["game_started"])
        self.assertEqual(host_start["media_session"]["status"], "WAITING_PEER")
        self.assertEqual(remote_start["media_session"]["status"], "WAITING_PEER")
        self.assertEqual(host_start["connection"]["scope"], "n64_runtime_media")
        self.assertEqual(host_start["connection"]["relay_host"], "relay.example.invalid")
        self.assertEqual(host_start["connection"]["relay_transport"], "tls")
        self.assertTrue(media_session["no_save"])
        self.assertNotIn("host_save_id", media_session)
        self.assertNotIn("remote_save_id", media_session)
        self.assertNotEqual(host_ticket, remote_ticket)
        stored_media = self.app.storage.load_n64_runtime_media_sessions()[media_session["id"]]
        self.assertNotIn(host_ticket, stored_media.values())
        self.assertNotIn(remote_ticket, stored_media.values())
        self.assertFalse(self.app.n64_runtime_media_sessions.validate_ticket(media_session["id"], "remote", host_ticket))
        self.assertTrue(self.app.n64_runtime_media_sessions.validate_ticket(media_session["id"], "host", host_ticket))
        self.assertEqual(self.app.n64_runtime_media_sessions.get(media_session["id"]).status, "READY")
        self.assertFalse(self.app.n64_runtime_media_sessions.validate_ticket(media_session["id"], "host", host_ticket))
        self.assertTrue(self.app.n64_runtime_media_sessions.validate_ticket(media_session["id"], "remote", remote_ticket))
        self.assertEqual(self.app.n64_runtime_media_sessions.get(media_session["id"]).status, "RUNNING")
        public_session = self.get(f"/n64-runtime-media-sessions/{media_session['id']}", token=token_a)["media_session"]
        neutral_public_session = self.get(
            f"/n64-runtime-media-sessions/{media_session['id']}", token=token_a
        )["media_session"]
        self.assertEqual(public_session["role"], "host")
        self.assertEqual(neutral_public_session, public_session)
        runtime_n64 = self.get(
            f"/n64-runtime-media-sessions/{media_session['id']}/runtime-saves/n64",
            token=token_a,
        )
        runtime_host_gb = self.get(
            f"/n64-runtime-media-sessions/{media_session['id']}/runtime-saves/host-gb",
            token=token_a,
        )
        runtime_remote_gb = self.get(
            f"/n64-runtime-media-sessions/{media_session['id']}/runtime-saves/remote-gb",
            token=token_a,
        )
        token_a_other = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple"},
        )["token"]["token"]
        with self.assertRaisesRegex(ValidationError, "participants are not reserved"):
            self.get(
                f"/n64-runtime-media-sessions/{media_session['id']}/runtime-saves/remote-gb",
                token=token_a_other,
            )
        self.assertEqual(runtime_n64["runtime_save"]["game_type"], "n64_sample_n64")
        self.assertEqual(runtime_host_gb["runtime_save"]["game_type"], "sample_alpha")
        self.assertEqual(runtime_remote_gb["runtime_save"]["game_type"], "sample_beta")
        self.assertTrue(runtime_remote_gb["runtime_save"]["no_save"])
        self.assertNotIn("id", runtime_remote_gb["runtime_save"])
        self.assertNotIn("user_id", runtime_remote_gb["runtime_save"])
        with self.assertRaisesRegex(ValidationError, "requires host role"):
            self.get(
                f"/n64-runtime-media-sessions/{media_session['id']}/runtime-saves/remote-gb",
                token=token_b,
            )
        with self.assertRaisesRegex(ValidationError, "kind is invalid"):
            self.get(
                f"/n64-runtime-media-sessions/{media_session['id']}/runtime-saves/other",
                token=token_a,
            )
        host_user_id = str(ready_room["users"][0]["user_id"])
        remote_user_id = str(ready_room["users"][1]["user_id"])
        game_locks = {
            user_id: self.app.sessions.active_game_session_for_user(user_id)["lock"].to_dict()
            for user_id in (host_user_id, remote_user_id)
        }
        self.assertEqual(set(game_locks), {host_user_id, remote_user_id})
        self.assertEqual(game_locks[host_user_id]["execution_mode"], "N64_RUNTIME_NOSAVE")
        self.assertEqual(game_locks[remote_user_id]["execution_mode"], "N64_RUNTIME_NOSAVE")
        self.assertEqual(game_locks[host_user_id]["game_run_id"], media_session["id"])
        self.assertEqual(game_locks[remote_user_id]["game_run_id"], media_session["id"])
        self.assertEqual(
            {item["save_id"] for item in game_locks[host_user_id]["save_bindings"]},
            {slots_a[0]["save_id"], slots_a[1]["save_id"]},
        )
        self.assertEqual(
            {item["save_id"] for item in game_locks[remote_user_id]["save_bindings"]},
            {slots_b[0]["save_id"]},
        )
        self.assertNotIn(slots_a[2]["save_id"], {
            item["save_id"] for item in game_locks[host_user_id]["save_bindings"]
        })
        host_fence = {
            "game_session_id": game_locks[host_user_id]["game_session_id"],
            "fencing_token": game_locks[host_user_id]["fencing_token"],
        }
        remote_fence = {
            "game_session_id": game_locks[remote_user_id]["game_session_id"],
            "fencing_token": game_locks[remote_user_id]["fencing_token"],
        }
        for save_id in (slots_a[0]["save_id"], slots_a[1]["save_id"]):
            with self.assertRaisesRegex(ValidationError, "uploads are disabled for no-save"):
                self.put(
                    f"/saves/{save_id}",
                    {"expected_revision": 1, "save_data": encode(b"forbidden-host-write"), **host_fence},
                    token=token_a,
                )
        with self.assertRaisesRegex(ValidationError, "uploads are disabled for no-save"):
            self.put(
                f"/saves/{slots_b[0]['save_id']}",
                {"expected_revision": 1, "save_data": encode(b"forbidden-remote-write"), **remote_fence},
                token=token_b,
            )
        revisions_after = {
            save_id: self.get(
                f"/saves/{save_id}", token=token_a if save_id in host_save_ids else token_b
            )["save"]["revision"]
            for save_id in save_ids
        }
        self.assertEqual(revisions_after, revisions_before)
        heartbeat = self.post("/rooms/heartbeat", {}, token=token_a)
        self.assertEqual(heartbeat["lock_renewed"][0]["media_session_id"], media_session["id"])
        self.assertTrue(heartbeat["lock_renewed"][0]["no_save"])
        self.assertGreaterEqual(
            datetime.fromisoformat(heartbeat["lock_renewed"][0]["media_expires_at"]),
            datetime.fromisoformat(media_session["expires_at"]),
        )
        self.post("/room-matching/leave", {}, token=token_b)
        self.assertIsNone(self.app.sessions.active_game_session_for_user(host_user_id))
        self.assertIsNone(self.app.sessions.active_game_session_for_user(remote_user_id))
        stored_media = self.app.storage.load_n64_runtime_media_sessions()[media_session["id"]]
        self.assertEqual(stored_media["status"], "CANCELLED")
        resumed = self.put(
            f"/saves/{slots_a[1]['save_id']}",
            {"expected_revision": 1, "save_data": encode(b"normal-write-after-nosave")},
            token=token_a,
        )["save"]
        self.assertEqual(resumed["revision"], 2)

    def test_n64_finish_is_atomic_authorized_and_idempotent(self) -> None:
        users, tokens, saves = [], [], []
        for name in ("finish_host", "finish_remote"):
            users.append(self.post("/auth/register", {"username": name, "password": "password123"})["user"])
            tokens.append(self.post("/auth/login", {"username": name, "password": "password123"})["token"]["token"])
            saves.append(self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"save")}, token=tokens[-1])["save"])
            self.join_room_fixture(65, token=tokens[-1])
        media = self.app.n64_runtime_media_sessions.create_or_get(
            room_number=65, host_user_id=users[0]["id"], remote_user_id=users[1]["id"],
            host_n64_slot="ROM1", host_gb_slot="ROM2", remote_gb_slot="ROM1",
            host_n64_rom_id="n64", host_gb_rom_id="gb1", remote_gb_rom_id="gb2",
            host_n64_save_id=saves[0]["id"], host_save_id=saves[0]["id"],
            remote_save_id=saves[1]["id"], game_type="sample_alpha")
        for user, token, save in zip(users, tokens, saves):
            self.app.sessions.acquire_media_game_session_lock(
                user["id"], media.id, self.app.game_session_lease_expires_at(),
                hashlib.sha256(token.encode()).hexdigest(), [self.app.saves.get_save(save["id"])])
        path = f"/n64-runtime-media-sessions/{media.id}/finish"
        with self.assertRaisesRegex(ValidationError, "host role"):
            self.post(path, {}, token=tokens[1])
        other = self.post("/auth/login", {"username": "finish_host", "password": "password123"})["token"]["token"]
        with self.assertRaisesRegex(ValidationError, "active host reservation"):
            self.post(path, {}, token=other)
        with patch("integral_emulator.sqlite_room.SQLiteRoomManager._close_room", side_effect=RuntimeError("injected")):
            with self.assertRaisesRegex(RuntimeError, "injected"):
                self.post(path, {}, token=tokens[0])
        self.assertEqual(self.app.n64_runtime_media_sessions.get(media.id).status, "CREATED")
        for user in users:
            self.assertIsNotNone(self.app.sessions.active_game_session_for_user(user["id"]))
        result = self.post(path, {}, token=tokens[0])["media_session"]
        self.assertEqual((result["status"], result["termination_reason"]), ("COMPLETED", "host_finished"))
        self.assertEqual(self.post(path, {}, token=tokens[0])["media_session"], result)
        for user in users:
            self.assertIsNone(self.app.sessions.active_game_session_for_user(user["id"]))
            self.assertIsNone(self.app.room_manager.current_room(user["id"]))
            self.assertEqual(self.app.session_lifecycle_for_user(user["id"])["termination_reason"], "host_finished")
        with self.app.authority_database.transaction() as connection:
            run = connection.execute("SELECT status, termination_reason FROM game_runs WHERE game_run_id = ?", (media.id,)).fetchone()
        self.assertEqual(tuple(run), ("COMPLETED", "host_finished"))
        self.assertEqual(self.get(f"/n64-runtime-media-sessions/{media.id}", token=tokens[1])["media_session"]["status"], "COMPLETED")
        self.join_room_fixture(65, token=tokens[0])
        self.join_room_fixture(65, token=tokens[1])

        # Delay old-session callbacks across immediate room-number reuse.
        # No sleeps/retries: both cancellation and stale expiry race new join.
        for _ in range(10):
            self.app.room_manager.leave_room(users[0]["id"])
            self.join_room_fixture(65, token=tokens[0])
            new_code = self.app.room_manager.room(65).room_code
            with ThreadPoolExecutor(max_workers=3) as pool:
                jobs = [pool.submit(self.app.n64_runtime_media_sessions.cancel, media.id),
                        pool.submit(self.app.n64_runtime_media_sessions.expire, media.id, "session_expired"),
                        pool.submit(self.join_room_fixture, 65, token=tokens[1])]
                for job in jobs:
                    job.result()
            self.assertEqual(self.app.room_manager.room(65).room_code, new_code)
            self.assertEqual(len(self.app.room_manager.room(65).users), 2)

        # An active orphan belongs to its original code, never just number 65.
        orphan = self.app.n64_runtime_media_sessions.create_or_get(
            room_number=65, host_user_id=users[0]["id"], remote_user_id=users[1]["id"],
            host_n64_slot="ROM1", host_gb_slot="ROM2", remote_gb_slot="ROM1",
            host_n64_rom_id="n64", host_gb_rom_id="gb1", remote_gb_rom_id="gb2",
            host_n64_save_id=saves[0]["id"], host_save_id=saves[0]["id"],
            remote_save_id=saves[1]["id"], game_type="sample_alpha")
        self.app.room_manager.leave_room(users[0]["id"])
        context = self.app.build_request_context(tokens[0])
        with patch("integral_emulator.sqlite_room.secrets.choice", return_value=65):
            fresh = self.app.room_manager.create_room("n64", users[0]["id"], users[0]["username"], context.auth_session_id_digest)
        self.post(f"/n64-runtime-media-sessions/{orphan.id}/terminate",
                  {"room_code": self.app.n64_runtime_media_sessions.get(orphan.id).room_code}, token=tokens[0])
        self.assertEqual(self.app.room_manager.room(65).room_code, fresh.room_code)
        with self.assertRaises(ValidationError):
            self.app.storage.mark_n64_room_started(orphan.id)
        self.assertFalse(self.app.room_manager.room(65).game_started)

    def test_round7_explicit_terminal_both_roles_and_stale_request(self) -> None:
        for role in (0, 1):
            for outage in (26, 32):
                users, tokens = [], []
                for seat in (0, 1):
                    name = f"r7_{role}_{outage}_{seat}"
                    users.append(self.post("/auth/register", {"username": name, "password": "password123"})["user"])
                    tokens.append(self.post("/auth/login", {"username": name, "password": "password123"})["token"]["token"])
                    self.join_room_fixture(65, token=tokens[-1])
                rooms = self.app.room_manager
                code = rooms.room(65).room_code
                manager = self.app.n64_runtime_media_sessions
                media = manager.create_or_get(room_number=65,
                    host_user_id=users[0]["id"], remote_user_id=users[1]["id"],
                    host_n64_slot="ROM1", host_gb_slot="ROM2", remote_gb_slot="ROM1",
                    host_n64_rom_id="n64", host_gb_rom_id="gb1", remote_gb_rom_id="gb2",
                    host_n64_save_id="", host_save_id="", remote_save_id="", game_type="sample_alpha")
                for user, token in zip(users, tokens):
                    self.app.sessions.acquire_media_game_session_lock(user["id"], media.id,
                        self.app.game_session_lease_expires_at(), hashlib.sha256(token.encode()).hexdigest(), [])
                path = f"/n64-runtime-media-sessions/{media.id}/terminate"
                with self.assertRaises(ValidationError):
                    self.post(path, {"room_code": "wrong"}, token=tokens[role])
                with patch("integral_emulator.sqlite_room.SQLiteRoomManager._close_room", side_effect=RuntimeError("rollback")):
                    with self.assertRaises(RuntimeError):
                        self.post(path, {"room_code": code}, token=tokens[role])
                self.assertEqual(manager.get(media.id).status, "CREATED")
                started = datetime.now(timezone.utc)
                with patch("integral_emulator.n64_runtime_media_sessions.datetime", wraps=datetime) as clock:
                    clock.now.return_value = started
                    manager.recover(media.id)
                    clock.now.return_value = started + timedelta(seconds=outage)
                    result = self.post(path, {"room_code": code}, token=tokens[role])["media_session"]
                self.assertEqual((result["status"], result["termination_reason"]), ("EXPIRED", "recovery_timeout"))
                for user in users:
                    self.assertIsNone(rooms.current_room(user["id"]))
                    self.assertIsNone(self.app.sessions.active_game_session_for_user(user["id"]))
                self.join_room_fixture(65, token=tokens[0])
                self.join_room_fixture(65, token=tokens[1])
                next_code = rooms.room(65).room_code
                self.post(path, {"room_code": code}, token=tokens[role])
                self.assertEqual(rooms.room(65).room_code, next_code)
                self.assertEqual(len(rooms.room(65).users), 2)
                rooms.leave_room(users[0]["id"])

    def test_round7_link_leave_after_game_stop_closes_both_seats(self) -> None:
        for role in (0, 1):
            users, tokens, saves = [], [], []
            for seat in (0, 1):
                name = f"r7_link_{role}_{seat}"
                users.append(self.post("/auth/register", {"username": name, "password": "password123"})["user"])
                tokens.append(self.post("/auth/login", {"username": name, "password": "password123"})["token"]["token"])
                saves.append(self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"test")}, token=tokens[-1])["save"])
                self.join_room_fixture(1, token=tokens[-1])
            session = self.app.sessions.create_session(users[0]["id"], users[1]["id"],
                saves[0]["id"], saves[1]["id"], room_number=1, base_port=self.app.room_base_port(1),
                lock_expires_at=self.app.game_session_lease_expires_at(),
                auth_session_ids={u["id"]: hashlib.sha256(t.encode()).hexdigest() for u,t in zip(users,tokens)})
            self.app.room_manager.set_link_session(1, session.id)
            self.app.sessions.transition(session.id, LinkSessionStatus.RUNNING)
            self.post("/game/stop", self.game_fence(self.get("/game/status", token=tokens[role])), token=tokens[role])
            self.post("/room-matching/leave", {}, token=tokens[role])
            for user in users:
                self.assertIsNone(self.app.room_manager.current_room(user["id"]))
                self.assertIsNone(self.app.sessions.active_game_session_for_user(user["id"]))
            self.join_room_fixture(1, token=tokens[role])
            self.join_room_fixture(1, token=tokens[1-role])
            self.app.room_manager.leave_room(users[role]["id"])

    def test_n64_expiry_atomically_closes_membership_and_allows_immediate_reentry(self) -> None:
        from dataclasses import replace
        from integral_emulator.sqlite_room import SQLiteRoomManager

        for reason in ("recovery_timeout", "session_expired", "participant_lease_expired"):
            with self.subTest(reason=reason):
                users, tokens = [], []
                for role in ("host", "remote"):
                    name = f"{reason}_{role}"
                    users.append(self.post("/auth/register", {"username": name, "password": "password123"})["user"])
                    tokens.append(self.post("/auth/login", {"username": name, "password": "password123"})["token"]["token"])
                    self.join_room_fixture(65, token=tokens[-1])
                rooms = self.app.room_manager
                code = rooms.room(65).room_code
                manager = self.app.n64_runtime_media_sessions
                media = manager.create_or_get(
                    room_number=65, host_user_id=users[0]["id"], remote_user_id=users[1]["id"],
                    host_n64_slot="ROM1", host_gb_slot="ROM2", remote_gb_slot="ROM1",
                    host_n64_rom_id="n64", host_gb_rom_id="gb1", remote_gb_rom_id="gb2",
                    host_n64_save_id="", host_save_id="", remote_save_id="", game_type="sample_alpha")
                for user, token in zip(users, tokens):
                    self.app.sessions.acquire_media_game_session_lock(
                        user["id"], media.id, self.app.game_session_lease_expires_at(),
                        hashlib.sha256(token.encode()).hexdigest(), [])
                with patch.object(SQLiteRoomManager, "_close_room", side_effect=RuntimeError("rollback probe")):
                    with self.assertRaisesRegex(RuntimeError, "rollback probe"):
                        manager.expire(media.id, reason)
                self.assertEqual(manager.get(media.id).status, "CREATED")
                for user in users:
                    self.assertIsNotNone(self.app.sessions.active_game_session_for_user(user["id"]))
                    self.assertIsNotNone(rooms.current_room(user["id"]))
                # Exercise lazy expiry too: it must not expose terminal before cleanup.
                if reason != "participant_lease_expired":
                    current = manager.get(media.id)
                    field = "recovery_deadline" if reason == "recovery_timeout" else "expires_at"
                    expired = replace(current, **{field: "2000-01-01T00:00:00+00:00"})
                    self.app.storage.update_n64_runtime_media_session(expired.to_storage_dict(), current.row_version)
                    manager.get(media.id)
                else:
                    current = manager.get(media.id)
                    paired = replace(current, host_ticket_used=True, remote_ticket_used=True)
                    self.app.storage.update_n64_runtime_media_session(paired.to_storage_dict(), current.row_version)
                    with self.app.authority_database.transaction(write=True) as connection:
                        connection.execute("UPDATE game_session_locks SET lease_expires_at_ms=1 WHERE game_run_id=?", (media.id,))
                    self.app.reconcile_session_lifecycle()
                terminal = manager.get(media.id)
                self.assertEqual((terminal.status, terminal.termination_reason), ("EXPIRED", reason))
                with self.app.authority_database.transaction() as connection:
                    self.assertEqual(connection.execute("SELECT COUNT(*) FROM rooms WHERE room_number=65").fetchone()[0], 0)
                    self.assertEqual(connection.execute("SELECT COUNT(*) FROM room_members WHERE room_number=65").fetchone()[0], 0)
                    self.assertIsNotNone(connection.execute("SELECT 1 FROM room_code_tombstones WHERE room_code=?", (code,)).fetchone())
                    run = connection.execute("SELECT status, termination_reason FROM game_runs WHERE game_run_id=?", (media.id,)).fetchone()
                    self.assertEqual(tuple(run), ("EXPIRED", reason))
                for user in users:
                    self.assertIsNone(self.app.sessions.active_game_session_for_user(user["id"]))
                    self.assertEqual(rooms.recent_termination_notice(user["id"])["termination_reason"], reason)
                # Both users can create, and the other can join, without leave of the old room.
                for creator, peer in ((0, 1), (1, 0)):
                    new = rooms.create_room("n64", users[creator]["id"], users[creator]["username"], hashlib.sha256(tokens[creator].encode()).hexdigest())
                    rooms.join_room_by_code(new.room_code, users[peer]["id"], users[peer]["username"], hashlib.sha256(tokens[peer].encode()).hexdigest())
                    manager.expire(media.id, reason)
                    self.assertEqual(manager.get(media.id), terminal)
                    self.assertEqual(len(rooms.room(new.room_number).users), 2)
                    rooms.leave_room(users[creator]["id"])

    def test_n64_runtime_media_leave_logout_require_participant_auth_session_owner(self) -> None:
        scenarios = [
            (role, owner, operation)
            for role in ("host", "remote")
            for owner in (True, False)
            for operation in ("leave", "logout")
        ]
        for index, (role, owner, operation) in enumerate(scenarios):
            with self.subTest(role=role, owner=owner, operation=operation):
                host_user = self.post(
                    "/auth/register",
                    {"username": f"Matrix_H_{index}", "password": "correct horse battery staple"},
                )["user"]
                remote_user = self.post(
                    "/auth/register",
                    {"username": f"Matrix_R_{index}", "password": "correct horse battery staple"},
                )["user"]
                host_owner = self.post(
                    "/auth/login",
                    {"username": f"matrix_h_{index}", "password": "correct horse battery staple"},
                )["token"]["token"]
                host_other = self.post(
                    "/auth/login",
                    {"username": f"matrix_h_{index}", "password": "correct horse battery staple"},
                )["token"]["token"]
                remote_owner = self.post(
                    "/auth/login",
                    {"username": f"matrix_r_{index}", "password": "correct horse battery staple"},
                )["token"]["token"]
                remote_other = self.post(
                    "/auth/login",
                    {"username": f"matrix_r_{index}", "password": "correct horse battery staple"},
                )["token"]["token"]
                host_n64 = self.create_test_save(
                    {"game_type": "sample_n64", "save_data": encode(b"n64")}, token=host_owner
                )["save"]
                host_gb = self.create_test_save(
                    {"game_type": "sample_alpha", "save_data": encode(b"host-gb")}, token=host_owner
                )["save"]
                remote_gb = self.create_test_save(
                    {"game_type": "sample_alpha", "save_data": encode(b"remote-gb")}, token=remote_owner
                )["save"]
                room_number = 65 + index
                self.join_room_fixture(room_number, token=host_owner)
                self.join_room_fixture(room_number, token=remote_owner)
                media_session = self.app.n64_runtime_media_sessions.create_or_get(
                    room_number=room_number,
                    host_user_id=host_user["id"],
                    remote_user_id=remote_user["id"],
                    host_n64_slot="ROM1",
                    host_gb_slot="ROM2",
                    remote_gb_slot="ROM1",
                    host_n64_rom_id=f"n64-rom-{index}",
                    host_gb_rom_id=f"host-gb-rom-{index}",
                    remote_gb_rom_id=f"remote-gb-rom-{index}",
                    host_n64_save_id=host_n64["id"],
                    host_save_id=host_gb["id"],
                    remote_save_id=remote_gb["id"],
                    game_type="sample_alpha",
                )
                self.app.sessions.acquire_media_game_session_lock(
                    host_user["id"],
                    media_session.id,
                    self.app.game_session_lease_expires_at(),
                    hashlib.sha256(host_owner.encode()).hexdigest(),
                    [self.app.saves.get_save(host_n64["id"]), self.app.saves.get_save(host_gb["id"])],
                )
                self.app.sessions.acquire_media_game_session_lock(
                    remote_user["id"],
                    media_session.id,
                    self.app.game_session_lease_expires_at(),
                    hashlib.sha256(remote_owner.encode()).hexdigest(),
                    [self.app.saves.get_save(remote_gb["id"])],
                )

                acting_token = {
                    ("host", True): host_owner,
                    ("host", False): host_other,
                    ("remote", True): remote_owner,
                    ("remote", False): remote_other,
                }[(role, owner)]
                if not owner:
                    with self.assertRaisesRegex(ValidationError, "different auth session"):
                        self.join_room_fixture(80, token=acting_token)
                    with self.assertRaisesRegex(ValidationError, "different auth session"):
                        self.post(
                            f"/rooms/{room_number}/state",
                            {"ready": False},
                            token=acting_token,
                        )
                result = self.post(f"/auth/{operation}" if operation == "logout" else "/room-matching/leave", {}, token=acting_token)

                stored_media = self.app.n64_runtime_media_sessions.get(media_session.id)
                locks = {
                    user_id: self.app.sessions.active_game_session_for_user(user_id)
                    for user_id in (host_user["id"], remote_user["id"])
                }
                room = next(
                    item for item in self.list_rooms_fixture(token=host_owner if acting_token != host_owner else host_other)
                    if item["room_number"] == room_number
                )
                if owner:
                    self.assertEqual(stored_media.status, "CANCELLED")
                    self.assertIsNone(locks[host_user["id"]])
                    self.assertIsNone(locks[remote_user["id"]])
                    self.assertEqual(len(room["users"]), 0)
                else:
                    if operation == "leave":
                        self.assertFalse(result["left"])
                    else:
                        self.assertTrue(result["logged_out"])
                    self.assertIn(stored_media.status, {"CREATED", "WAITING_PEER", "READY", "RUNNING"})
                    self.assertIsNotNone(locks[host_user["id"]])
                    self.assertIsNotNone(locks[remote_user["id"]])
                    self.assertEqual(len(room["users"]), 2)
                    cleanup_token = host_owner if role == "host" else remote_owner
                    self.post("/room-matching/leave", {}, token=cleanup_token)

    def test_n64_room_move_clears_orphaned_media_session_without_game_lock(self) -> None:
        host_user = self.post(
            "/auth/register",
            {"username": "Orphan_Host", "password": "correct horse battery staple"},
        )["user"]
        remote_user = self.post(
            "/auth/register",
            {"username": "Orphan_Remote", "password": "correct horse battery staple"},
        )["user"]
        host_owner = self.post(
            "/auth/login",
            {"username": "orphan_host", "password": "correct horse battery staple"},
        )["token"]["token"]
        remote_owner = self.post(
            "/auth/login",
            {"username": "orphan_remote", "password": "correct horse battery staple"},
        )["token"]["token"]
        self.join_room_fixture(65, token=host_owner)
        self.join_room_fixture(65, token=remote_owner)
        media_session = self.app.n64_runtime_media_sessions.create_or_get(
            room_number=65,
            host_user_id=host_user["id"],
            remote_user_id=remote_user["id"],
            host_n64_slot="ROM1",
            host_gb_slot="ROM2",
            remote_gb_slot="ROM2",
            host_n64_rom_id="n64-rom",
            host_gb_rom_id="host-gb-rom",
            remote_gb_rom_id="remote-gb-rom",
            host_n64_save_id="n64-save",
            host_save_id="host-save",
            remote_save_id="remote-save",
            game_type="sample_alpha",
        )
        fresh_host = self.post(
            "/auth/login",
            {"username": "orphan_host", "password": "correct horse battery staple"},
        )["token"]["token"]

        moved = self.join_room_fixture(1, token=fresh_host)["room"]

        self.assertEqual(moved["room_number"], 1)
        self.assertEqual(self.app.n64_runtime_media_sessions.get(media_session.id).status, "CANCELLED")
        old_room = next(
            room for room in self.list_rooms_fixture(token=fresh_host) if room["room_number"] == 65
        )
        self.assertEqual(old_room["users"], [])


    def test_gb_runtime_fixed_host_is_the_current_room_protocol(self) -> None:
        metrics = self.app.admin_lifecycle_summary()["gb_runtime_fixed_host_rollout"]
        self.assertEqual(metrics["stage"], "default")
        self.assertEqual(metrics["room_allowlist_count"], 16)

    def test_gb_runtime_fixed_host_developer_room_preflight_snapshots_tickets_and_cancel(self) -> None:
        self.post("/auth/register", {"username": "Fixed_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Fixed_B", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "fixed_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "fixed_b", "password": "correct horse battery staple"})["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")
        beta = next(rom for rom in allowed_roms() if rom.game_type == "sample_beta" and rom.display_name == "SAMPLE BETA")
        self.post(
            "/rom-slots/apply",
            {"slots": [
                {"slot": 1, "filename": "alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"},
                {"slot": 2, "filename": "beta.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"},
            ]},
            token=token_a,
        )
        self.post(
            "/rom-slots/apply",
            {"slots": [{"slot": 1, "filename": "beta.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"}]},
            token=token_b,
        )
        self.join_room_fixture(16, token=token_a)
        self.join_room_fixture(16, token=token_b)
        self.post("/rooms/16/state", {"slot": "ROM1", "ready": True}, token=token_a)
        self.post("/rooms/16/state", {"slot": "ROM1", "ready": True}, token=token_b)
        self.app.gb_runtime_fixed_host_rollout_stage = "test_accounts"
        self.app.gb_runtime_fixed_host_dev_rooms = frozenset()
        self.app.gb_runtime_fixed_host_canary_users = frozenset({"fixed_a", "fixed_b"})

        started = self.post("/rooms/16/start", {"link_mode": "battle"}, token=token_a)
        self.assert_no_auth_session_digest(started)
        self.assertTrue(
            all(
                member.get("auth_session_id_digest")
                for member in self.app.room_manager.room(16).users
            )
        )
        repeated_start = self.post(
            "/rooms/16/start", {"link_mode": "battle"}, token=token_b
        )
        self.assert_no_auth_session_digest(repeated_start)
        link = started["link_session"]
        fixed = started["gb_runtime_fixed_host_session"]
        self.assertEqual(link["protocol_id"], "gb_runtime_fixed_host_v1")
        self.assertEqual(fixed["role"], "host")
        self.assertEqual(fixed["state"], "PREFLIGHT")
        self.assertEqual(fixed["manifest"]["save_policy"], "discard")
        digest = fixed["manifest_digest"]
        available_roms = self.fixed_host_available_roms(fixed)
        self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/preflight",
            {"manifest_digest": digest, "protocol_id": "gb_runtime_fixed_host_v1", "runtime_build_id": "integral-gb-runtime-fixed-host-v2", "available_roms": available_roms},
            token=token_a,
        )
        ready = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/preflight",
            {"manifest_digest": digest, "protocol_id": "gb_runtime_fixed_host_v1", "runtime_build_id": "integral-gb-runtime-fixed-host-v2", "available_roms": []},
            token=token_b,
        )["gb_runtime_fixed_host_session"]
        self.assertEqual(ready["state"], "READY")
        game_status = self.get("/game/status", token=token_b)
        self.assertTrue(game_status["active"])
        self.assertTrue(game_status["game_session"]["is_owner_auth_session"])
        self.assertEqual(game_status["game_session"]["link_session_id"], link["id"])
        self.assertTrue(game_status["game_session"]["game_session_id"])
        self.assertGreater(game_status["game_session"]["fencing_token"], 0)
        with self.assertRaisesRegex(ValidationError, "host role"):
            self.get(f"/gb-runtime-fixed-host-sessions/{link['id']}/runtime-snapshots", token=token_b)
        snapshots = self.get(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/runtime-snapshots", token=token_a
        )["runtime_snapshots"]
        self.assertEqual(snapshots["manifest_digest"], digest)
        self.assertTrue(snapshots["host"]["save_data"])
        self.assertTrue(snapshots["remote"]["save_data"])

        host_connection = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/relay-ticket", {}, token=token_a
        )["connection"]
        remote_connection = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/relay-ticket", {}, token=token_b
        )["connection"]
        self.assertEqual(host_connection["scope"], "gb-runtime-fixed-host-media-v1")
        self.assertEqual(host_connection["relay_transport"], "tls")
        self.assertEqual((host_connection["role"], remote_connection["role"]), ("host", "remote"))
        self.assertEqual(host_connection["runtime_config"]["save_policy"], "discard")
        self.assertEqual(host_connection["runtime_config"]["requested_mode"], "battle")
        self.assertNotIn("speed_default", host_connection["runtime_config"])
        self.assertNotIn("player_a", host_connection["runtime_config"])
        self.assertNotIn("player_b", host_connection["runtime_config"])
        stored = self.app.storage.load_gb_runtime_fixed_host_sessions()[link["id"]]
        self.assertNotIn(host_connection["ticket"], str(stored))
        self.assertFalse(
            self.app.gb_runtime_fixed_host_sessions.validate_ticket(
                link["id"], "remote", host_connection["scope"], host_connection["ticket"]
            )
        )
        self.assertTrue(
            self.app.gb_runtime_fixed_host_sessions.validate_ticket(
                link["id"], "host", host_connection["scope"], host_connection["ticket"]
            )
        )
        self.assertFalse(
            self.app.gb_runtime_fixed_host_sessions.validate_ticket(
                link["id"], "host", host_connection["scope"], host_connection["ticket"]
            )
        )

        self.app.gb_runtime_fixed_host_sessions.peer_disconnected(link["id"], "host")
        paused_heartbeat = self.post("/rooms/heartbeat", {}, token=token_b)
        self.assertTrue(paused_heartbeat["lock_renewed"])
        paused = self.app.storage.load_gb_runtime_fixed_host_sessions()[link["id"]]
        paused["pause_expires_at"] = "2000-01-01T00:00:00+00:00"
        self.app.storage.update_gb_runtime_fixed_host_session(paused, int(paused["__row_version"]))
        heartbeat = self.post("/rooms/heartbeat", {}, token=token_b)
        self.assertEqual(heartbeat["lock_renewed"], [])
        self.assertEqual(
            self.app.sessions.get_session(link["id"]).status, "CANCELLED"
        )
        cancelled = self.app.gb_runtime_fixed_host_sessions.get(link["id"])
        self.assertEqual(cancelled.state, "ABORTED")
        self.assertIsNone(self.app.saves.get_save(link["save_a_id"]).lock_owner)
        self.assertIsNone(self.app.saves.get_save(link["save_b_id"]).lock_owner)
        self.assertIsNone(
            self.app.sessions.game_session_authority.active_for_user(
                link["player_a_user_id"]
            )
        )
        self.assertIsNone(
            self.app.sessions.game_session_authority.active_for_user(
                link["player_b_user_id"]
            )
        )
        room = self.app.room_manager.room(16)
        self.assertIsNone(room.link_session_id)
        self.assertFalse(room.game_started)
        self.assertTrue(all(not user["ready"] for user in room.users))
        cancel_response = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/cancel", {}, token=token_a
        )
        self.assert_no_auth_session_digest(cancel_response)

    def test_gb_runtime_fixed_host_trade_pair_commit_requires_matching_remote_receipt_and_fence(self) -> None:
        self.post("/auth/register", {"username": "Fixed_Trade_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Fixed_Trade_B", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "fixed_trade_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "fixed_trade_b", "password": "correct horse battery staple"})["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")
        beta = next(rom for rom in allowed_roms() if rom.game_type == "sample_beta" and rom.display_name == "SAMPLE BETA")
        self.post("/rom-slots/apply", {"slots": [
            {"slot": 1, "filename": "alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"},
            {"slot": 2, "filename": "beta.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"},
        ]}, token=token_a)
        self.post("/rom-slots/apply", {"slots": [{"slot": 1, "filename": "beta.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"}]}, token=token_b)
        self.join_room_fixture(16, token=token_a)
        self.join_room_fixture(16, token=token_b)
        self.post("/rooms/16/state", {"slot": "ROM1", "link_mode": "trade"}, token=token_a)
        self.post("/rooms/16/state", {"slot": "ROM1", "ready": True}, token=token_a)
        self.post("/rooms/16/state", {"slot": "ROM1", "ready": True}, token=token_b)
        self.app.gb_runtime_fixed_host_rollout_stage = "test_accounts"
        self.app.gb_runtime_fixed_host_dev_rooms = frozenset()
        self.app.gb_runtime_fixed_host_canary_users = frozenset({"fixed_trade_a", "fixed_trade_b"})
        started = self.post("/rooms/16/start", {"link_mode": "trade"}, token=token_a)
        link = started["link_session"]
        fixed = started["gb_runtime_fixed_host_session"]
        self.assertEqual(fixed["manifest"]["save_policy"], "commit_pair")
        for token, available_roms in (
            (token_a, self.fixed_host_available_roms(fixed)),
            (token_b, []),
        ):
            self.post(
                f"/gb-runtime-fixed-host-sessions/{link['id']}/preflight",
                {"manifest_digest": fixed["manifest_digest"], "protocol_id": "gb_runtime_fixed_host_v1", "runtime_build_id": "integral-gb-runtime-fixed-host-v2", "available_roms": available_roms},
                token=token,
            )
        host_connection = self.post(f"/gb-runtime-fixed-host-sessions/{link['id']}/relay-ticket", {}, token=token_a)["connection"]
        remote_connection = self.post(f"/gb-runtime-fixed-host-sessions/{link['id']}/relay-ticket", {}, token=token_b)["connection"]
        self.assertEqual(host_connection["runtime_config"]["save_policy"], "commit_pair")
        self.app.gb_runtime_fixed_host_sessions.validate_ticket(link["id"], "host", host_connection["scope"], host_connection["ticket"])
        self.app.gb_runtime_fixed_host_sessions.validate_ticket(link["id"], "remote", remote_connection["scope"], remote_connection["ticket"])
        player_a_user_id = link["player_a_user_id"]
        player_b_user_id = link["player_b_user_id"]
        lock = self.app.sessions.active_game_session_for_user(player_a_user_id)["lock"]
        _, original_a = self.app.saves.download(link["save_a_id"], player_a_user_id)
        _, original_b = self.app.saves.download(link["save_b_id"], player_b_user_id)
        candidate_a = original_a[:-1] + bytes([original_a[-1] ^ 1])
        candidate_b = original_b[:-1] + bytes([original_b[-1] ^ 2])
        digest = "7" * 64
        host_body = {
            "game_session_id": lock.game_session_id,
            "fencing_token": lock.fencing_token,
            "terminal_digest": digest,
            "final_frame": 9001,
            "host_candidate": encode(candidate_a),
            "remote_candidate": encode(candidate_b),
        }
        with self.assertRaisesRegex(ValidationError, "both fixed Host"):
            incomplete = dict(host_body)
            incomplete["remote_candidate"] = ""
            self.post(f"/gb-runtime-fixed-host-sessions/{link['id']}/host-finish", incomplete, token=token_a)
        with self.assertRaisesRegex(GameSessionFenceError, "stale game session fence"):
            stale = dict(host_body)
            stale["fencing_token"] += 1
            self.post(f"/gb-runtime-fixed-host-sessions/{link['id']}/host-finish", stale, token=token_a)
        host_waiting = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/host-finish", host_body,
            token=token_a,
        )["gb_runtime_fixed_host_session"]
        self.assertEqual(host_waiting["state"], "FINALIZING")
        self.assertIsNone(self.app.sync_link_session_saves(link["id"]))
        self.assertEqual(self.app.saves.get_save(link["save_a_id"]).revision, 1)
        self.assertEqual(self.app.saves.get_save(link["save_b_id"]).revision, 1)
        finished = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/terminal-receipt",
            {"terminal_digest": digest, "final_frame": 9001}, token=token_b,
        )["gb_runtime_fixed_host_session"]
        self.assertEqual(finished["state"], "FINISHED")
        self.assertEqual(finished["finish_roles"], ["host", "remote"])
        self.assertEqual(self.app.saves.get_save(link["save_a_id"]).revision, 2)
        self.assertEqual(self.app.saves.get_save(link["save_b_id"]).revision, 2)
        self.assertEqual(self.app.saves.download(link["save_a_id"], player_a_user_id)[1], candidate_a)
        self.assertEqual(self.app.saves.download(link["save_b_id"], player_b_user_id)[1], candidate_b)
        duplicate = self.post(
            f"/gb-runtime-fixed-host-sessions/{link['id']}/host-finish", host_body, token=token_a
        )["gb_runtime_fixed_host_session"]
        self.assertEqual(duplicate["commit_result"], finished["commit_result"])
        self.assertEqual(self.app.saves.get_save(link["save_a_id"]).revision, 2)


    def test_room_room_start_rejects_n64_rom_slot(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Player_B", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "player_b", "password": "correct horse battery staple"})["token"]["token"]
        beta = next(rom for rom in allowed_roms() if rom.game_type == "sample_beta" and rom.display_name == "SAMPLE BETA")

        self.post(
            "/rom-slots/apply",
            {"slots": [{"slot": 1, "filename": "sample_console.z64", "sha256": "c" * 64, "sha1": "d" * 40, "game_type": "sample_n64", "region": "JP"}]},
            token=token_a,
        )
        self.post(
            "/rom-slots/apply",
            {"slots": [{"slot": 1, "filename": "beta.gbc", "sha256": beta.sha256, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"}]},
            token=token_b,
        )

        self.join_room_fixture(1, token=token_a)
        self.join_room_fixture(1, token=token_b)
        self.post("/rooms/1/state", {"link_mode": "battle"}, token=token_a)
        self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_a)
        self.post("/rooms/1/state", {"slot": "ROM1", "ready": True}, token=token_b)

        with self.assertRaisesRegex(ValidationError, "ROOM play requires a GB/GBC ROM slot"):
            self.post("/rooms/1/start", {"link_mode": "battle"}, token=token_a)














    def test_gb_runtime_fixed_host_room_allows_different_catalog_roms(self) -> None:
        self.post("/auth/register", {"username": "Catalog_A", "password": "correct horse battery staple"})
        self.post("/auth/register", {"username": "Catalog_B", "password": "another correct password"})
        token_a = self.post("/auth/login", {"username": "catalog_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "catalog_b", "password": "another correct password"})["token"]["token"]
        delta = next(rom for rom in allowed_roms() if rom.game_type == "sample_delta")
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")
        self.post(
            "/rom-slots/apply",
            {"slots": [
                {"slot": 1, "filename": "delta.gb", "sha256": delta.sha256 or "a" * 64, "sha1": delta.sha1, "game_type": "sample_delta", "region": "JP"},
                {"slot": 2, "filename": "alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"},
            ]},
            token=token_a,
        )
        self.post(
            "/rom-slots/apply",
            {"slots": [{"slot": 1, "filename": "alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"}]},
            token=token_b,
        )
        self.join_room_fixture(15, token=token_a)
        self.join_room_fixture(15, token=token_b)
        self.post("/rooms/15/state", {"link_mode": "battle"}, token=token_a)
        self.post("/rooms/15/state", {"slot": "ROM1", "ready": True}, token=token_a)
        self.post("/rooms/15/state", {"slot": "ROM1", "ready": True}, token=token_b)
        self.app.gb_runtime_fixed_host_rollout_stage = "default"

        started = self.post(
            "/rooms/15/start", {"link_mode": "battle"}, token=token_a
        )
        self.assert_no_auth_session_digest(started)
        self.assertEqual(started["link_session"]["protocol_id"], "gb_runtime_fixed_host_v1")
        self.assertEqual(
            started["gb_runtime_fixed_host_session"]["manifest"]["host_game_type"],
            "sample_delta",
        )
        self.assertEqual(
            started["gb_runtime_fixed_host_session"]["manifest"]["remote_game_type"],
            "sample_alpha",
        )
        cancelled = self.post(
            f"/gb-runtime-fixed-host-sessions/{started['link_session']['id']}/cancel",
            {},
            token=token_a,
        )
        self.assert_no_auth_session_digest(cancelled)




    def test_rom_registration_stores_metadata_without_local_path(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        allowed = next(rom for rom in allowed_roms() if rom.game_type == "sample_gamma")
        sha256 = allowed.sha256

        registered = self.post(
            "/roms",
            {"sha256": sha256, "sha1": allowed.sha1, "title": "gamma.gbc", "platform": "gb", "region": "JP", "rom_header_title": "SAMPLE GAMMA"},
            token=token,
        )["rom"]
        listed = self.get("/roms", token=token)["roms"]

        self.assertEqual(registered["sha256"], sha256.lower())
        self.assertEqual(registered["verified_name"], allowed.canonical_name)
        self.assertNotIn("local_path", registered)
        self.assertEqual([item["id"] for item in listed], [registered["id"]])

    def test_unlisted_game_type_is_generated_from_platform_and_header(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Custom_Player", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "custom_player", "password": "correct horse battery staple"},
        )["token"]["token"]

        applied = self.post(
            "/rom-slots/apply",
            {"slots": [{
                "slot": 1,
                "filename": "personal-build.gbc",
                "sha256": "e" * 64,
                "platform": "gb",
                "region": "ZZ",
                "rom_header_title": "My Custom ROM 2026",
            }]},
            token=token,
        )["slots"][0]
        saved = self.get(f"/saves/{applied['save_id']}", token=token)["save"]

        self.assertEqual(applied["game_type"], "gb_my_custom_rom_2026")
        self.assertEqual(saved["game_type"], "gb_my_custom_rom_2026")
        self.assertEqual(saved["sha256"], hashlib.sha256(bytes(32 * 1024)).hexdigest())

    def test_client_game_type_is_rejected_and_invalid_header_is_rejected(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Invalid_Type", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "invalid_type", "password": "correct horse battery staple"},
        )["token"]["token"]

        with self.assertRaisesRegex(ValidationError, "assigned by the server"):
            self.request(
                "POST", "/roms",
                {"sha256": "a" * 64, "title": "sample.gb", "platform": "gb",
                 "region": "JP", "rom_header_title": "SAMPLE", "game_type": "client_value"},
                token,
            )
        for header in ("", "bad\x01header", " " * 21):
            with self.subTest(header=header):
                with self.assertRaisesRegex(ValidationError, "rom_header_title"):
                    self.request(
                        "POST", "/roms",
                        {"sha256": "b" * 64, "title": "sample.gb", "platform": "gb",
                         "region": "JP", "rom_header_title": header},
                        token,
                    )
        with self.assertRaisesRegex(ValidationError, "rom_header_title must be a string"):
            self.request(
                "POST", "/roms",
                {"sha256": "c" * 64, "title": "sample.gb", "platform": "gb",
                 "region": "JP", "rom_header_title": 123},
                token,
            )

    def test_rom_registration_rejects_non_japanese_region(self) -> None:
        self.enforce_rom_allowlist()
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]

        with self.assertRaisesRegex(ValidationError, "ROM is not in the enabled ROM catalog"):
            self.post(
                "/roms",
                {"sha256": "a" * 64, "title": "gamma.gbc", "platform": "gb", "region": "US", "rom_header_title": "SAMPLE GAMMA"},
                token=token,
            )

    def test_rom_registration_accepts_synthetic_catalog_targets(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]

        for index, game_type in enumerate(
            ["sample_alpha", "sample_beta", "sample_gamma", "sample_delta"],
            start=1,
        ):
            allowed = next(rom for rom in allowed_roms() if rom.game_type == game_type)
            registered = self.post(
                "/roms",
                {
                    "sha256": f"{index:x}" * 64,
                    "sha1": allowed.sha1,
                    "title": f"catalog-{index}.gbc",
                    "platform": "gb",
                    "region": "JP",
                    "rom_header_title": allowed.rom_header_title,
                },
                token=token,
            )["rom"]
            self.assertEqual(registered["game_type"], game_type)

    def test_rom_slots_apply_accepts_n64_and_creates_server_save(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]

        result = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {
                        "slot": 1,
                        "filename": "sample_console.z64",
                        "sha256": "a" * 64,
                        "sha1": "b" * 40,
                        "platform": "n64",
                        "region": "JP",
                        "rom_header_title": "CONSOLE SAMPLE",
                    }
                ]
            },
            token=token,
        )

        slot = result["slots"][0]
        self.assertEqual(slot["game_type"], "n64_console_sample")
        self.assertTrue(slot["rom_id"].startswith("rom_"))
        self.assertTrue(slot["save_id"].startswith("save_"))
        downloaded = self.get(f"/saves/{slot['save_id']}", token=token)
        self.assertEqual(downloaded["save"]["game_type"], "n64_console_sample")
        self.assertEqual(decode(downloaded["save_data"]), bytes([0xFF]) * (128 * 1024))

    def test_rom_registration_rejects_hash_outside_allowlist(self) -> None:
        self.enforce_rom_allowlist()
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]

        with self.assertRaisesRegex(ValidationError, "ROM is not in the enabled ROM catalog"):
            self.post(
                "/roms",
                {"sha256": "a" * 64, "sha1": "b" * 40, "title": "gamma.gbc", "platform": "gb", "region": "JP", "rom_header_title": "SAMPLE GAMMA"},
                token=token,
            )

    def test_rom_slots_apply_requires_confirmation_before_replacing_save(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]

        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")
        beta = next(rom for rom in allowed_roms() if rom.game_type == "sample_beta" and rom.display_name == "SAMPLE BETA")

        first = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "alpha.gbc", "sha256": alpha.sha256 or "a" * 64, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"}
                ]
            },
            token=token,
        )
        save_id = first["slots"][0]["save_id"]

        needs_confirm = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "beta.gbc", "sha256": beta.sha256 or "b" * 64, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"}
                ]
            },
            token=token,
        )
        self.assertTrue(needs_confirm["requires_confirmation"])
        self.assertEqual(needs_confirm["delete_save_ids"], [save_id])

        applied = self.post(
            "/rom-slots/apply",
            {
                "confirm_delete_saves": True,
                "slots": [
                    {"slot": 1, "filename": "beta.gbc", "sha256": beta.sha256 or "b" * 64, "sha1": beta.sha1, "game_type": "sample_beta", "region": "JP"}
                ],
            },
            token=token,
        )
        self.assertFalse(applied["requires_confirmation"])
        self.assertNotEqual(applied["slots"][0]["save_id"], save_id)
        saves = self.get("/saves", token=token)["saves"]
        self.assertEqual([save["id"] for save in saves], [applied["slots"][0]["save_id"]])

    def test_rom_slot_apply_uses_imported_initial_save_data(self) -> None:
        self.app.allow_user_initial_save_import = True
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        login = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})
        self.assertTrue(login["server"]["allow_user_initial_save_import"])
        token = login["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")

        applied = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {
                        "slot": 1,
                        "filename": "alpha.gbc",
                        "sha256": alpha.sha256,
                        "sha1": alpha.sha1,
                        "game_type": "sample_alpha",
                        "region": "JP",
                        "initial_save_data": encode(b"existing-local-save"),
                    }
                ]
            },
            token=token,
        )
        downloaded = self.get(f"/saves/{applied['slots'][0]['save_id']}", token=token)

        self.assertEqual(decode(downloaded["save_data"]), b"existing-local-save")

        beta = next(
            rom
            for rom in allowed_roms()
            if rom.game_type == "sample_beta" and rom.display_name == "SAMPLE BETA"
        )
        replacement = {
            "slot": 1,
            "filename": "beta.gbc",
            "sha256": beta.sha256,
            "sha1": beta.sha1,
            "game_type": "sample_beta",
            "region": "JP",
            "initial_save_data": encode(b"replacement-local-save"),
        }
        confirmation = self.post(
            "/rom-slots/apply", {"slots": [replacement]}, token=token
        )
        self.assertTrue(confirmation["requires_confirmation"])

        replaced = self.post(
            "/rom-slots/apply",
            {"confirm_delete_saves": True, "slots": [replacement]},
            token=token,
        )
        replacement_save = self.get(
            f"/saves/{replaced['slots'][0]['save_id']}", token=token
        )
        self.assertEqual(
            decode(replacement_save["save_data"]), b"replacement-local-save"
        )

    def test_rom_slot_apply_same_rom_without_initial_save_is_idempotent(self) -> None:
        self.app.allow_user_initial_save_import = True
        self.post(
            "/auth/register",
            {"username": "Player_A", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom
            for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        request = {
            "slots": [{
                "slot": 1,
                "filename": "alpha.gbc",
                "sha256": alpha.sha256,
                "sha1": alpha.sha1,
                "game_type": "sample_alpha",
                "region": "JP",
            }]
        }

        first = self.post("/rom-slots/apply", request, token=token)["slots"][0]
        second = self.post("/rom-slots/apply", request, token=token)["slots"][0]

        self.assertEqual(second["rom_id"], first["rom_id"])
        self.assertEqual(second["save_id"], first["save_id"])
        self.assertEqual(len(self.get("/saves", token=token)["saves"]), 1)

    def test_same_rom_in_two_slots_uses_independent_saves(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Same_Rom", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "same_rom", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom
            for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        same_rom = {
            "filename": "roms/a.gbc",
            "sha256": alpha.sha256,
            "sha1": alpha.sha1,
            "game_type": "sample_alpha",
            "region": "JP",
        }
        slots = self.post(
            "/rom-slots/apply",
            {"slots": [{"slot": 1, **same_rom}, {"slot": 2, **same_rom}]},
            token=token,
        )["slots"]

        self.assertEqual(slots[0]["rom_id"], slots[1]["rom_id"])
        self.assertNotEqual(slots[0]["save_id"], slots[1]["save_id"])
        started = self.post(
            "/game/start",
            {
                "execution_mode": "LOCAL_CLIENT",
                "save_ids": [slots[0]["save_id"], slots[1]["save_id"]],
            },
            token=token,
        )
        first_data = b"1" + bytes(32 * 1024 - 1)
        second_data = b"2" + bytes(32 * 1024 - 1)
        fence = self.game_fence(started)
        self.put(
            f"/saves/{slots[0]['save_id']}",
            {"expected_revision": 1, "save_data": encode(first_data), **fence},
            token=token,
        )
        self.put(
            f"/saves/{slots[1]['save_id']}",
            {"expected_revision": 1, "save_data": encode(second_data), **fence},
            token=token,
        )
        self.post("/game/stop", fence, token=token)

        first = self.get(f"/saves/{slots[0]['save_id']}", token=token)
        second = self.get(f"/saves/{slots[1]['save_id']}", token=token)
        self.assertEqual(decode(first["save_data"]), first_data)
        self.assertEqual(decode(second["save_data"]), second_data)

    def test_rom_slot_apply_rejects_initial_save_for_unchanged_rom(self) -> None:
        self.app.allow_user_initial_save_import = True
        self.post(
            "/auth/register",
            {"username": "Player_A", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "player_a", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom
            for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        slot = {
            "slot": 1,
            "filename": "alpha.gbc",
            "sha256": alpha.sha256,
            "sha1": alpha.sha1,
            "game_type": "sample_alpha",
            "region": "JP",
        }
        first = self.post(
            "/rom-slots/apply", {"slots": [slot]}, token=token
        )["slots"][0]

        with self.assertRaisesRegex(
            ValidationError, "use the Admin SAV Replace screen"
        ):
            self.post(
                "/rom-slots/apply",
                {
                    "slots": [{
                        **slot,
                        "initial_save_data": encode(b"must-not-replace"),
                    }]
                },
                token=token,
            )

        current = self.get("/rom-slots", token=token)["slots"][0]
        downloaded = self.get(f"/saves/{first['save_id']}", token=token)
        self.assertEqual(current["save_id"], first["save_id"])
        self.assertEqual(decode(downloaded["save_data"]), bytes(32 * 1024))

    def test_rom_slot_apply_rejects_initial_save_when_policy_is_disabled(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")

        with self.assertRaisesRegex(
            ValidationError, "initial_save_data is disabled by server policy"
        ):
            self.post(
                "/rom-slots/apply",
                {"slots": [{
                    "slot": 1,
                    "filename": "alpha.gbc",
                    "sha256": alpha.sha256,
                    "sha1": alpha.sha1,
                    "game_type": "sample_alpha",
                    "region": "JP",
                    "initial_save_data": encode(b"existing-local-save"),
                }]},
                token=token,
            )
        self.assertEqual(self.get("/saves", token=token)["saves"], [])

    def test_rom_slot_apply_accepts_generated_gb_initial_save_when_import_policy_is_disabled(self) -> None:
        self.post("/auth/register", {"username": "Generated_Save", "password": "correct horse battery staple"})
        token = self.post(
            "/auth/login",
            {"username": "generated_save", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        generated = bytes([0xFF]) * (32 * 1024)
        applied = self.post(
            "/rom-slots/apply",
            {"slots": [{
                "slot": 1,
                "filename": "alpha.gbc",
                "sha256": alpha.sha256,
                "sha1": alpha.sha1,
                "platform": "gb",
                "region": "JP",
                "generated_initial_save_data": encode(generated),
            }]},
            token=token,
        )
        downloaded = self.get(
            f"/saves/{applied['slots'][0]['save_id']}", token=token
        )
        self.assertEqual(decode(downloaded["save_data"]), generated)

        with self.assertRaisesRegex(
            ValidationError, "generated_initial_save_data is only valid for GB/GBC ROMs"
        ):
            self.post(
                "/rom-slots/apply",
                {"slots": [{
                    "slot": 2,
                    "filename": "bad.z64",
                    "sha256": "f" * 64,
                    "platform": "n64",
                    "region": "JP",
                    "generated_initial_save_data": encode(b"not-n64-save"),
                }]},
                token=token,
            )

    def test_rom_slot_apply_rejects_generated_gb_initial_save_with_non_ff_ram(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Generated_Invalid", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "generated_invalid", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        invalid = bytearray([0xFF]) * (32 * 1024)
        invalid[123] = 0x00

        with self.assertRaisesRegex(
            ValidationError,
            "generated_initial_save_data RAM must be initialized to 0xFF",
        ):
            self.post(
                "/rom-slots/apply",
                {"slots": [{
                    "slot": 1,
                    "filename": "alpha.gbc",
                    "sha256": alpha.sha256,
                    "sha1": alpha.sha1,
                    "platform": "gb",
                    "region": "JP",
                    "generated_initial_save_data": encode(bytes(invalid)),
                }]},
                token=token,
            )
        self.assertEqual(self.get("/saves", token=token)["saves"], [])

    def test_rom_slot_apply_accepts_generated_gb_initial_save_with_rtc_trailer(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Generated_Rtc", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "generated_rtc", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        generated = bytes([0xFF]) * (32 * 1024) + bytes(range(48))
        applied = self.post(
            "/rom-slots/apply",
            {"slots": [{
                "slot": 1,
                "filename": "alpha.gbc",
                "sha256": alpha.sha256,
                "sha1": alpha.sha1,
                "platform": "gb",
                "region": "JP",
                "generated_initial_save_data": encode(generated),
            }]},
            token=token,
        )
        downloaded = self.get(
            f"/saves/{applied['slots'][0]['save_id']}", token=token
        )
        self.assertEqual(decode(downloaded["save_data"]), generated)

    def test_rom_slot_apply_accepts_rtc_only_generated_gb_initial_save(self) -> None:
        self.post(
            "/auth/register",
            {"username": "Generated_Rtc_Only", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "generated_rtc_only", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        generated = bytes(range(48))
        applied = self.post(
            "/rom-slots/apply",
            {"slots": [{
                "slot": 1,
                "filename": "alpha.gbc",
                "sha256": alpha.sha256,
                "sha1": alpha.sha1,
                "platform": "gb",
                "region": "JP",
                "generated_initial_save_data": encode(generated),
            }]},
            token=token,
        )
        downloaded = self.get(
            f"/saves/{applied['slots'][0]['save_id']}", token=token
        )
        self.assertEqual(decode(downloaded["save_data"]), generated)

    def test_rom_slot_apply_accepts_zero_byte_generated_gb_initial_save(self) -> None:
        self.post(
            "/auth/register",
            {"username": "No_Battery", "password": "correct horse battery staple"},
        )
        token = self.post(
            "/auth/login",
            {"username": "no_battery", "password": "correct horse battery staple"},
        )["token"]["token"]
        alpha = next(
            rom for rom in allowed_roms()
            if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA"
        )
        applied = self.post(
            "/rom-slots/apply",
            {"slots": [{
                "slot": 1,
                "filename": "alpha.gbc",
                "sha256": alpha.sha256,
                "sha1": alpha.sha1,
                "platform": "gb",
                "region": "JP",
                "generated_initial_save_data": "",
            }]},
            token=token,
        )
        downloaded = self.get(
            f"/saves/{applied['slots'][0]['save_id']}", token=token
        )
        self.assertEqual(decode(downloaded["save_data"]), b"")
        save = self.get("/saves", token=token)["saves"][0]
        self.assertEqual(
            save["sha256"],
            hashlib.sha256(b"").hexdigest(),
        )

    def test_rom_slot_apply_default_initial_save_is_full_sized_zero_save(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")

        applied = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"}
                ]
            },
            token=token,
        )
        downloaded = self.get(f"/saves/{applied['slots'][0]['save_id']}", token=token)

        self.assertEqual(decode(downloaded["save_data"]), bytes(32 * 1024))

    def test_rom_slot_replace_rejects_invalid_new_rom_without_deleting_old_save(self) -> None:
        self.enforce_rom_allowlist()
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        alpha = next(rom for rom in allowed_roms() if rom.game_type == "sample_alpha" and rom.display_name == "SAMPLE ALPHA")

        first = self.post(
            "/rom-slots/apply",
            {
                "slots": [
                    {"slot": 1, "filename": "alpha.gbc", "sha256": alpha.sha256, "sha1": alpha.sha1, "game_type": "sample_alpha", "region": "JP"}
                ]
            },
            token=token,
        )
        old_slot = first["slots"][0]
        old_save_id = old_slot["save_id"]
        old_save_path = self.app.saves.get_save(old_save_id, first["slots"][0]["user_id"]).storage_path

        with self.assertRaisesRegex(ValidationError, "ROM is not in the enabled ROM catalog"):
            self.post(
                "/rom-slots/apply",
                {
                    "confirm_delete_saves": True,
                    "slots": [
                        {
                            "slot": 1,
                            "filename": "bad-beta.gbc",
                            "sha256": "a" * 64,
                            "sha1": "b" * 40,
                            "game_type": "sample_beta",
                            "region": "JP",
                        }
                    ],
                },
                token=token,
            )

        current = self.get("/rom-slots", token=token)["slots"][0]
        self.assertEqual(current["save_id"], old_save_id)
        self.assertEqual(self.app.saves.get_save(old_save_id, old_slot["user_id"]).id, old_save_id)
        self.assertTrue(self.app.storage.resolve_relative(old_save_path).exists())




















    def test_recover_orphans_fails_active_session(self) -> None:
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        user_b = self.app.auth.register("Player_B", "another correct password")
        save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"a-save")}, token=token_a)["save"]
        save_b = self.app.saves.create_save(user_b.id, "sample_beta", b"b-save")
        session = self.create_link_session_direct(save_a["user_id"], user_b.id, save_a["id"], save_b.id)

        recovered = self.app.sessions.recover_orphaned_sessions(reason="test recovery")

        self.assertEqual([item.id for item in recovered], [session["id"]])
        self.assertEqual(recovered[0].status, "FAILED")

    def test_lifecycle_sweeper_removes_expired_standalone_game_lock(self) -> None:
        user = self.post(
            "/auth/register",
            {"username": "standalone-user", "password": "correct horse battery staple"},
        )["user"]
        token = self.post(
            "/auth/login",
            {"username": "standalone-user", "password": "correct horse battery staple"},
        )["token"]["token"]
        lock = self.app.sessions.acquire_local_game_session_lock(
            user["id"],
            "2000-01-01T00:00:00+00:00",
            hashlib.sha256(token.encode()).hexdigest(),
        )

        actions = self.app.reconcile_session_lifecycle()

        self.assertTrue(
            any(
                action.get("game_session_status") == "EXPIRED"
                and action.get("reason") == "participant_lease_expired"
                for action in actions
            )
        )
        self.assertFalse(self.app.sessions.active_game_session_for_user(lock.user_id))





    def test_heartbeat_renews_only_calling_participant_lease(self) -> None:
        token_a, token_b, _save_a, _save_b, session = self.prepare_client_finalize_fixture()
        user_a = self.app.auth.require_user(token_a).id
        user_b = self.app.auth.require_user(token_b).id
        peer_expiry = self.game_lock_row(user_b)["lease_expires_at_ms"]

        self.app.sessions.renew_game_session_lock(
            session["id"], user_a, self.app.game_session_lease_expires_at(),
            auth_session_id=hashlib.sha256(token_a.encode()).hexdigest(),
        )
        self.assertEqual(self.game_lock_row(user_b)["lease_expires_at_ms"], peer_expiry)

    def test_battle_expiry_ends_without_save_commit(self) -> None:
        _token_a, _token_b, save_a, save_b, session = self.prepare_client_finalize_fixture()
        sessions = self.app.storage.load_link_sessions()
        sessions[session["id"]]["requested_link_mode"] = "battle"
        sessions[session["id"]]["expires_at"] = "2000-01-01T00:00:00+00:00"
        record = sessions[session["id"]]
        self.app.storage.update_link_session(record, int(record["__row_version"]))

        self.app.reconcile_session_lifecycle()
        ended = self.app.sessions.get_session(session["id"])

        self.assertEqual(ended.status, LinkSessionStatus.EXPIRED.value)
        self.assertEqual(ended.termination_reason, "session_expired")
        self.assertEqual(self.app.saves.get_save(save_a["id"]).revision, 1)
        self.assertEqual(self.app.saves.get_save(save_b["id"]).revision, 1)

    def test_trade_expiry_enters_fixed_finalization_then_expires(self) -> None:
        _token_a, _token_b, save_a, save_b, session = self.prepare_client_finalize_fixture()
        sessions = self.app.storage.load_link_sessions()
        sessions[session["id"]]["expires_at"] = "2000-01-01T00:00:00+00:00"
        record = sessions[session["id"]]
        self.app.storage.update_link_session(record, int(record["__row_version"]))

        first_actions = self.app.reconcile_session_lifecycle()
        finalizing = self.app.sessions.get_session(session["id"])
        self.assertTrue(first_actions)
        self.assertEqual(finalizing.status, LinkSessionStatus.FINALIZING.value)
        self.assertIsNotNone(finalizing.expires_at)

        temp_path = self.app.sessions.session_temp_path(finalizing)
        temp_path.mkdir(parents=True, exist_ok=True)
        self.app.storage.atomic_write_bytes(
            temp_path / "player_a.sav", b"a-unpaired-result"
        )
        self.assertEqual(self.app.saves.get_save(save_a["id"]).revision, 1)
        self.assertEqual(self.app.saves.get_save(save_b["id"]).revision, 1)

        sessions = self.app.storage.load_link_sessions()
        sessions[session["id"]]["expires_at"] = "2000-01-01T00:00:00+00:00"
        record = sessions[session["id"]]
        self.app.storage.update_link_session(record, int(record["__row_version"]))
        self.app.reconcile_session_lifecycle()
        expired = self.app.sessions.get_session(session["id"])
        self.assertEqual(expired.status, LinkSessionStatus.EXPIRED.value)
        self.assertEqual(expired.termination_reason, "trade result deadline expired")
        self.assertEqual(self.app.saves.get_save(save_a["id"]).revision, 1)
        self.assertEqual(self.app.saves.get_save(save_b["id"]).revision, 1)
        self.assertFalse(temp_path.exists())





    def prepare_client_finalize_fixture(self):
        self.post("/auth/register", {"username": "Player_A", "password": "correct horse battery staple"})
        user_b = self.post("/auth/register", {"username": "Player_B", "password": "another correct password"})["user"]
        token_a = self.post("/auth/login", {"username": "player_a", "password": "correct horse battery staple"})["token"]["token"]
        token_b = self.post("/auth/login", {"username": "player_b", "password": "another correct password"})["token"]["token"]
        save_a = self.create_test_save({"game_type": "sample_alpha", "save_data": encode(b"a-before")}, token=token_a)["save"]
        save_b = self.create_test_save({"game_type": "sample_beta", "save_data": encode(b"b-before")}, token=token_b)["save"]
        session = self.create_link_session_direct(
            save_a["user_id"],
            user_b["id"],
            save_a["id"],
            save_b["id"],
            auth_session_ids={save_a["user_id"]: token_a, user_b["id"]: token_b},
            link_mode="trade",
        )
        self.app.sessions.transition(session["id"], LinkSessionStatus.RUNNING)
        return token_a, token_b, save_a, save_b, session


    def join_room_fixture(self, room_number: int, *, token: str) -> dict:
        context = self.app.build_request_context(token)
        with self.app.use_environment(context.environment):
            self.app.cancel_n64_runtime_media_sessions_for_user(
                context.user_id or "",
                context.auth_session_id_digest or "",
                reject_non_owner=True,
            )
            old_session_ids = self.app.room_manager.link_session_ids_for_user(context.user_id or "")
            current = self.app.room_manager.current_room(context.user_id or "")
            if current is not None and current.room_number != room_number:
                self.app.room_manager.leave_room(context.user_id or "")
            target = self.app.room_manager.room(room_number)
            if target.room_code:
                room = self.app.room_manager.join_room_by_code(
                    target.room_code,
                    context.user_id or "",
                    context.username or "",
                    context.auth_session_id_digest or "",
                )
            else:
                mode = "n64" if room_number >= 65 else "link_cable"
                with patch("integral_emulator.sqlite_room.secrets.choice", return_value=room_number):
                    room = self.app.room_manager.create_room(
                        mode,
                        context.user_id or "",
                        context.username or "",
                        context.auth_session_id_digest or "",
                    )
            for session_id in old_session_ids:
                self.app.cancel_room_game_for_session_if_owned(
                    session_id,
                    context.user_id or "",
                    context.auth_session_id_digest or "",
                    reason="test fixture room changed",
                )
            return {
                "room": self.app.public_room(
                    room, viewer_user_id=context.user_id or ""
                )
            }

    def list_rooms_fixture(self, *, token: str) -> list[dict]:
        context = self.app.build_request_context(token)
        with self.app.use_environment(context.environment):
            self.app.prune_stale_room_users()
            return [
                self.app.public_room(room, viewer_user_id=context.user_id or "")
                for room in self.app.room_manager.list_rooms()
            ]

    def get(self, path: str, token: str | None = None) -> dict:
        return self.request("GET", path, None, token)

    def post(self, path: str, body: dict, token: str | None = None) -> dict:
        if path == "/rom-slots/apply":
            body = copy.deepcopy(body)
            for item in body.get("slots", []):
                supplied_game_type = str(item.pop("game_type", ""))
                filename = str(item.get("filename", ""))
                if not filename:
                    continue
                item.setdefault(
                    "platform",
                    "n64" if Path(filename).suffix.lower() in {".z64", ".n64", ".v64"} else "gb",
                )
                matching_catalog = next(
                    (
                        rom
                        for rom in allowed_roms()
                        if item.get("sha256") == rom.sha256 or item.get("sha1") == rom.sha1
                    ),
                    None,
                )
                header = matching_catalog.rom_header_title if matching_catalog else None
                if not header:
                    header = " ".join(
                        part for part in supplied_game_type.upper().replace("-", "_").split("_") if part
                    ) or "TEST ROM"
                item.setdefault("rom_header_title", header[:20])
        return self.request("POST", path, body, token)

    def create_test_save(self, body: dict, token: str | None = None) -> dict:
        """Seed storage directly; the product has no general-user create route."""
        context = self.app.build_request_context(token)
        with self.app.use_environment(context.environment):
            save = self.app.saves.create_save(
                context.user_id or "", str(body["game_type"]), decode(body["save_data"])
            )
            return {"save": save.to_dict()}

    @staticmethod
    def fixed_host_available_roms(fixed: dict) -> list[dict[str, str]]:
        manifest = fixed["manifest"]
        return [
            {
                "game_type": manifest[f"{role}_game_type"],
                "platform": manifest[f"{role}_platform"],
                "rom_header_title": manifest[f"{role}_rom_header_title"],
            }
            for role in ("host", "remote")
        ]

    def put(self, path: str, body: dict, token: str | None = None) -> dict:
        return self.request("PUT", path, body, token)

    def request(self, method: str, path: str, body: dict | None, token: str | None) -> dict:
        return self.app.handle_request(method, path, body, bearer_token=token)

    def game_lock_row(self, user_id: str) -> dict:
        row = self.app.sessions.game_session_authority.repository.get_lock(user_id)
        self.assertIsNotNone(row)
        return row

    def set_game_lock_times(
        self,
        user_id: str,
        *,
        lease_expires_at: str | None = None,
        last_heartbeat: str | None = None,
    ) -> None:
        assignments = []
        values: list[int | str] = []
        if lease_expires_at is not None:
            assignments.append("lease_expires_at_ms = ?")
            values.append(int(datetime.fromisoformat(lease_expires_at).timestamp() * 1000))
        if last_heartbeat is not None:
            assignments.append("last_heartbeat_at_ms = ?")
            values.append(int(datetime.fromisoformat(last_heartbeat).timestamp() * 1000))
        self.assertTrue(assignments)
        with self.app.authority_database.transaction(write=True) as connection:
            cursor = connection.execute(
                f"UPDATE game_session_locks SET {', '.join(assignments)} WHERE user_id = ?",
                (*values, user_id),
            )
        self.assertEqual(cursor.rowcount, 1)

    def create_link_session_direct(
        self,
        player_a_user_id: str,
        player_b_user_id: str,
        save_a_id: str,
        save_b_id: str,
        auth_session_ids: dict[str, str] | None = None,
        link_mode: str | None = None,
    ) -> dict:
        if auth_session_ids:
            resolved_auth_ids = {
                user_id: hashlib.sha256(token.encode()).hexdigest()
                for user_id, token in auth_session_ids.items()
            }
        else:
            resolved_auth_ids = {}
        now_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
        for user_id in {player_a_user_id, player_b_user_id}:
            if user_id not in resolved_auth_ids:
                digest = hashlib.sha256(
                    f"direct-test:{user_id}:{time.time_ns()}".encode()
                ).hexdigest()
                self.app.auth.repository.create_session(
                    {
                        "token_digest": digest, "user_id": user_id,
                        "server_id": self.app.active_server_id,
                        "created_at_ms": now_ms,
                        "expires_at_ms": now_ms + 3_600_000,
                        "last_access_at_ms": now_ms,
                    }
                )
                resolved_auth_ids[user_id] = digest
        session = self.app.sessions.create_session(
            player_a_user_id,
            player_b_user_id,
            save_a_id,
            save_b_id,
            lock_expires_at=self.app.game_session_lease_expires_at(),
            link_mode=link_mode,
            auth_session_ids=resolved_auth_ids,
        )
        return session.to_dict()

    def game_fence(self, started: dict) -> dict:
        game_session = started["game_session"]
        return {
            "game_session_id": game_session["game_session_id"],
            "fencing_token": game_session["fencing_token"],
        }


def encode(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def decode(data: str) -> bytes:
    return base64.b64decode(data.encode("ascii"))


if __name__ == "__main__":
    unittest.main()
