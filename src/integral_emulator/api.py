# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Small JSON HTTP API for the INTEGRAL EMULATOR online service."""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import re
import socket
import secrets
import struct
import threading
import time
import traceback
from collections import deque
from contextlib import contextmanager
from contextvars import ContextVar
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from .database import AuthorityDatabase
from .auth_validation import username_comparison_key
from .errors import AuthenticationError, ClientVersionNotAllowedError, DuplicateUserError, GameSessionExpiredError, GameSessionFenceError, LeagueError, NotFoundError, RegistrationDisabledError, RevisionConflictError
from .errors import (
    AlreadyInRoomError,
    InvalidRoomCodeError,
    InvalidRoomModeError,
    RoomCodeUnavailableError,
    RoomJoinRateLimitedError,
    RoomPoolFullError,
    SaveLockedError,
    ValidationError,
    MobileCreateAbortedError,
    MobileCreateAuthSessionConflictError,
)
from .gb_runtime_fixed_host_protocol import (
    GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
    GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
    GBRuntimeFixedHostManifest,
)
from .gb_runtime_fixed_host_sessions import GBRuntimeFixedHostSessionManager
from .gb_runtime_host import GBRuntimeHostProcessManager
from .gb_runtime_link_modes import normalize_link_mode
from .room import (
    DEFAULT_LINK_ROOM_NUMBERS,
    DEFAULT_N64_ROOM_NUMBERS,
    LINK_ROOM_FIRST,
    LINK_ROOM_LAST,
    LINK_ROOMS_ENV,
    N64_ROOM_FIRST,
    N64_ROOM_LAST,
    N64_ROOMS_ENV,
    parse_enabled_room_numbers,
)
from .n64_runtime_media_sessions import (
    ACTIVE_MEDIA_STATUSES, N64_RUNTIME_MEDIA_SESSION_LOCK,
    N64RuntimeMediaSessionManager,
)
from .network_mode import NETWORK_MODE_TLS, configured_network_mode
from .mobile.session_service import MobileSessionManager
from .mobile.package_catalog import MobilePackageCatalog
from .models import SaveRecord, User
from .observability import TimingMetrics
from .rom_metadata import rom_platform_from_filename
from .rom_slots import RomSlotManager
from .room_session_policy import RoomSessionPolicy
from .save_contract import SaveUploadAuthority
from .security import issue_secret
from .sessions import ACTIVE_STATUSES, LEASE_RENEWABLE_STATUSES, MATCHABLE_STATUSES, LinkSessionManager, LinkSessionStatus
from .storage import LeagueStorage
from .sqlite_auth import SQLiteAuthService
from .sqlite_assets import SaveCommitService, SQLiteRomRegistry, SQLiteSaveManager
from .sqlite_game_sessions import SQLiteGameSessionAuthority
from .sqlite_room import SQLiteRoomManager
from .sqlite_repositories import (
    SQLiteAssetRepository,
    SQLiteAuthRepository,
    SQLiteGameSessionRepository,
    SQLiteRateLimitRepository,
    SQLiteSessionAuthorityRepository,
)
from .runtime_repositories import SQLiteRuntimeRepositories
from .ui import render_admin_html, render_admin_login_html
from .user_issuance import issue_user, reset_user_password


GSC_ROOM_BASE_PORT = 25100
GSC_ROOM_PORTS_PER_ROOM = 2
DEFAULT_SERVER_ID = "primary"
MAX_JSON_BODY_BYTES = 24 * 1024 * 1024
AUTH_JSON_BODY_BYTES = 16 * 1024
REQUEST_TIMEOUT_SECONDS = 15
ROOM_LEAVE_CANCEL_STATUSES = {
    LinkSessionStatus.CREATED.value,
    LinkSessionStatus.WAITING_PLAYER_A.value,
    LinkSessionStatus.WAITING_PLAYER_B.value,
    LinkSessionStatus.PREPARING.value,
    LinkSessionStatus.RUNNING.value,
}


class RequestBodyTooLarge(Exception):
    pass


class RateLimitExceeded(Exception):
    pass


def normalized_public_base_path(value: str) -> str:
    path = value.strip()
    if not path:
        return ""
    if not path.startswith("/") or path.endswith("/"):
        raise ValidationError(
            "INTEGRAL_EMULATOR_PUBLIC_BASE_PATH must start with '/' and not end with '/'"
        )
    if "//" in path or not re.fullmatch(r"/[A-Za-z0-9._~/-]+", path):
        raise ValidationError("INTEGRAL_EMULATOR_PUBLIC_BASE_PATH is invalid")
    return path


def environment_boolean(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None or not raw.strip():
        return default
    normalized = raw.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise ValidationError(
        f"{name} must be a boolean (0/1, true/false, yes/no, or on/off)"
    )


@dataclass
class ServerEnvironment:
    id: str
    name: str
    storage: SQLiteRuntimeRepositories
    roms: SQLiteRomRegistry
    saves: SQLiteSaveManager
    rom_slots: RomSlotManager
    sessions: LinkSessionManager
    host_processes: GBRuntimeHostProcessManager
    room_manager: SQLiteRoomManager
    n64_runtime_media_sessions: N64RuntimeMediaSessionManager
    mobile_sessions: MobileSessionManager
    save_uploads: SaveCommitService
    gb_runtime_fixed_host_sessions: GBRuntimeFixedHostSessionManager


@dataclass(frozen=True)
class RequestContext:
    request_id: str
    server_id: str
    environment: ServerEnvironment
    auth_session_id_digest: str | None
    user_id: str | None
    username: str | None
    issued_at: str
    user: User | None = None


@dataclass(frozen=True)
class BinaryResponse:
    data: bytes
    content_type: str
    headers: tuple[tuple[str, str], ...] = ()


class LeagueApplication:
    def __init__(self, storage_root: Path | str):
        self.storage_root = Path(storage_root)
        self.performance_metrics = TimingMetrics()
        self.authority_database = AuthorityDatabase(
            self.storage_root, metrics=self.performance_metrics
        )
        self.authority_database.initialize()
        self.session_authority = SQLiteSessionAuthorityRepository(
            self.authority_database
        )
        self.rate_limits = SQLiteRateLimitRepository(self.authority_database)
        self.auth = SQLiteAuthService(
            SQLiteAuthRepository(self.authority_database)
        )
        self.room_session_policy = RoomSessionPolicy.from_environment()
        self.enabled_link_rooms = parse_enabled_room_numbers(
            os.environ.get(
                LINK_ROOMS_ENV,
                f"{DEFAULT_LINK_ROOM_NUMBERS[0]}-{DEFAULT_LINK_ROOM_NUMBERS[-1]}",
            ),
            minimum=LINK_ROOM_FIRST,
            maximum=LINK_ROOM_LAST,
            setting_name=LINK_ROOMS_ENV,
        )
        self.enabled_n64_rooms = parse_enabled_room_numbers(
            os.environ.get(
                N64_ROOMS_ENV,
                f"{DEFAULT_N64_ROOM_NUMBERS[0]}-{DEFAULT_N64_ROOM_NUMBERS[-1]}",
            ),
            minimum=N64_ROOM_FIRST,
            maximum=N64_ROOM_LAST,
            setting_name=N64_ROOMS_ENV,
        )
        self.allow_unlisted_roms = environment_boolean(
            "INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS", True
        )
        self.allow_self_registration = environment_boolean(
            "INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION", False
        )
        self.client_version_check_enabled = environment_boolean(
            "INTEGRAL_EMULATOR_CLIENT_VERSION_CHECK_ENABLED", False
        )
        self.allowed_client_versions = frozenset(
            item.strip() for item in os.environ.get(
                "INTEGRAL_EMULATOR_ALLOWED_CLIENT_VERSIONS", ""
            ).split(",") if item.strip()
        )
        self.allow_user_initial_save_import = environment_boolean(
            "INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT", False
        )
        if not self.allow_unlisted_roms:
            # Preserve the legacy fail-closed startup behavior only when the
            # operator explicitly enables strict catalog enforcement.
            from .allowed_roms import allowed_roms

            allowed_roms()
        managed_package_root = os.environ.get(
            "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT", ""
        ).strip()
        package_roots = (
            [Path(managed_package_root)]
            if managed_package_root
            else [Path(__file__).resolve().parents[2] / "config" / "gb_mobile" / "packages"]
        )
        private_package_root = os.environ.get(
            "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PRIVATE_PACKAGE_ROOT", ""
        ).strip()
        if private_package_root:
            package_roots.append(Path(private_package_root))
        self.mobile_package_catalog = MobilePackageCatalog(package_roots)
        self.environments = self._build_environments(self.storage_root)
        self.default_server_id = DEFAULT_SERVER_ID
        self._request_context: ContextVar[RequestContext | None] = ContextVar(
            "integral_emulator_request_context", default=None
        )
        self.admin_password = os.environ.get("INTEGRAL_EMULATOR_ADMIN_PASSWORD", "")
        self.public_base_path = normalized_public_base_path(
            os.environ.get("INTEGRAL_EMULATOR_PUBLIC_BASE_PATH", "")
        )
        self.admin_base_path = f"{self.public_base_path}/admin"
        self.gb_runtime_fixed_host_runtime_build_id = os.environ.get(
            "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID",
            "integral-gb-runtime-fixed-host-v2",
        ).strip()
        if not self.gb_runtime_fixed_host_runtime_build_id:
            raise ValidationError("invalid fixed Host runtime build id")
        self.n64_runtime_media_relay_host = os.environ.get(
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PUBLIC_HOST",
            "relay.example.invalid",
        )
        self.n64_runtime_media_relay_port = int(
            os.environ.get(
                "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PORT",
                "25164",
            )
        )
        self.network_mode = configured_network_mode()
        self.n64_runtime_media_relay_transport = self.network_mode
        self.admin_cookie_secure = self.network_mode == NETWORK_MODE_TLS
        self.log_file = Path(
            os.environ.get(
                "INTEGRAL_EMULATOR_LOG_FILE",
                str(self.storage_root / "logs" / "server.log"),
            )
        )
        self.admin_sessions: dict[str, datetime] = {}
        self.log_lock = threading.RLock()
        self.lifecycle_metrics = {
            "sweeps": 0,
            "link_terminations": 0,
            "media_terminations": 0,
            "mobile_terminations": 0,
            "room_terminations": 0,
            "last_reconcile_at": None,
            "last_processed_count": 0,
            "last_duration_ms": 0.0,
            "max_duration_ms": 0.0,
            "sweeper_errors": 0,
            "last_sweeper_success_at": None,
            "last_sweeper_error": None,
        }
        self.room_matching_metrics = {
            "create_success": 0,
            "create_pool_full": 0,
            "join_success": 0,
            "join_unavailable": 0,
            "join_rate_limited": 0,
        }
        self.log_file.parent.mkdir(parents=True, exist_ok=True)

    @staticmethod
    def json_body_limit(path: str) -> int:
        if path in {"/auth/register", "/auth/login", "/auth/change-password", "/admin/session"}:
            return AUTH_JSON_BODY_BYTES
        return MAX_JSON_BODY_BYTES

    def public_path(self, path: str) -> str:
        if not path.startswith("/"):
            raise ValueError("public path must start with '/'")
        return f"{self.public_base_path}{path}"

    def admin_path(self, suffix: str = "") -> str:
        if suffix and not suffix.startswith("/"):
            raise ValueError("admin path suffix must start with '/'")
        return f"{self.admin_base_path}{suffix}"

    def enforce_request_rate_limit(
        self,
        client_ip: str,
        path: str,
        body: dict[str, Any],
        *,
        now: float | None = None,
    ) -> None:
        rules: list[tuple[str, int, float]] = []
        if path == "/auth/register":
            rules.append((f"register:ip:{client_ip}", 10, 60.0))
        elif path == "/auth/login":
            rules.append((f"login:ip:{client_ip}", 30, 60.0))
            username = body.get("username")
            if isinstance(username, str) and username.strip():
                rules.append((f"login:account:{username_comparison_key(username)}", 10, 60.0))
        elif path == "/admin/session":
            rules.append((f"admin-login:ip:{client_ip}", 10, 60.0))
        elif path == "/room-matching/create":
            rules.append((f"room-create:ip:{client_ip}", 10, 600.0))
        elif path == "/room-matching/join":
            rules.append((f"room-join:ip:{client_ip}", 30, 600.0))
        elif path.endswith("/preflight") and "/gb-runtime-fixed-host-sessions/" in path:
            rules.append((f"gb-runtime-fixed-host-preflight:ip:{client_ip}", 20, 60.0))
        elif path.endswith("/relay-ticket") and "/gb-runtime-fixed-host-sessions/" in path:
            rules.append((f"gb-runtime-fixed-host-ticket:ip:{client_ip}", 10, 60.0))
        if not rules:
            return
        timestamp_ms = int((time.time() if now is None else now) * 1000)
        if not self.rate_limits.consume_many(
            [(key, limit, int(window * 1000)) for key, limit, window in rules],
            timestamp_ms,
        ):
            raise RateLimitExceeded("too many authentication attempts; try again later")

    def enforce_room_matching_account_rate_limit(
        self, user_id: str, action: str, *, now: float | None = None
    ) -> None:
        limit = 10 if action == "join" else 5
        key = f"room-{action}:account:{self.active_server_id}:{user_id}"
        timestamp_ms = int((time.time() if now is None else now) * 1000)
        if not self.rate_limits.consume_many([(key, limit, 600_000)], timestamp_ms):
            if action == "join":
                self.room_matching_metrics["join_rate_limited"] += 1
                raise RoomJoinRateLimitedError("too many ROOM join attempts")
            raise RateLimitExceeded("too many ROOM create attempts")

    def game_session_lease_expires_at(self) -> str:
        return (
            datetime.now(timezone.utc)
            + timedelta(seconds=self.room_session_policy.lease_seconds)
        ).isoformat()

    def _build_environments(self, storage_root: Path) -> dict[str, ServerEnvironment]:
        return {
            "primary": self._create_environment("primary", "PRIMARY", storage_root),
            "secondary": self._create_environment(
                "secondary", "SECONDARY", storage_root / "servers" / "secondary"
            ),
        }

    def _create_environment(self, server_id: str, name: str, root: Path) -> ServerEnvironment:
        file_storage = LeagueStorage(root, metrics=self.performance_metrics)
        storage = SQLiteRuntimeRepositories(
            file_storage, self.authority_database, server_id
        )
        assets = SQLiteAssetRepository(self.authority_database)
        roms = SQLiteRomRegistry(
            assets,
            server_id,
            allow_unlisted_roms=self.allow_unlisted_roms,
        )
        saves = SQLiteSaveManager(file_storage, assets, server_id)
        rom_slots = RomSlotManager(storage, roms, saves)
        save_uploads = saves.commit_service
        game_authority = SQLiteGameSessionAuthority(
            SQLiteGameSessionRepository(self.authority_database), server_id
        )
        sessions = LinkSessionManager(
            storage,
            saves,
            server_id=server_id,
            policy=self.room_session_policy,
            game_session_authority=game_authority,
        )
        rom_slots.sessions = sessions
        mobile_sessions = MobileSessionManager(
            file_storage,
            saves,
            sessions,
            server_id=server_id,
            session_repository=self.session_authority,
        )
        return ServerEnvironment(
            id=server_id,
            name=name,
            storage=storage,
            roms=roms,
            saves=saves,
            rom_slots=rom_slots,
            sessions=sessions,
            host_processes=GBRuntimeHostProcessManager(storage),
            room_manager=SQLiteRoomManager(
                self.authority_database,
                server_id,
                enabled_link_rooms=self.enabled_link_rooms,
                enabled_n64_rooms=self.enabled_n64_rooms,
            ),
            n64_runtime_media_sessions=N64RuntimeMediaSessionManager(
                storage, policy=self.room_session_policy
            ),
            mobile_sessions=mobile_sessions,
            save_uploads=save_uploads,
            gb_runtime_fixed_host_sessions=GBRuntimeFixedHostSessionManager(storage),
        )

    @property
    def request_context(self) -> RequestContext | None:
        return self._request_context.get()

    @property
    def active_server_id(self) -> str:
        context = self.request_context
        return context.server_id if context is not None else self.default_server_id

    @property
    def active_environment(self) -> ServerEnvironment:
        context = self.request_context
        if context is not None:
            return context.environment
        return self.environments[self.default_server_id]

    @property
    def storage(self) -> LeagueStorage:
        return self.active_environment.storage

    @property
    def roms(self) -> SQLiteRomRegistry:
        return self.active_environment.roms

    @property
    def saves(self) -> SQLiteSaveManager:
        return self.active_environment.saves

    @property
    def save_uploads(self) -> SaveCommitService:
        return self.active_environment.save_uploads

    @property
    def rom_slots(self) -> RomSlotManager:
        return self.active_environment.rom_slots

    @property
    def sessions(self) -> LinkSessionManager:
        return self.active_environment.sessions

    @property
    def gb_runtime_fixed_host_sessions(self) -> GBRuntimeFixedHostSessionManager:
        return self.active_environment.gb_runtime_fixed_host_sessions

    @property
    def host_processes(self) -> GBRuntimeHostProcessManager:
        return self.active_environment.host_processes

    @property
    def room_manager(self) -> SQLiteRoomManager:
        return self.active_environment.room_manager

    @property
    def n64_runtime_media_sessions(self) -> N64RuntimeMediaSessionManager:
        return self.active_environment.n64_runtime_media_sessions

    @property
    def mobile_sessions(self) -> MobileSessionManager:
        return self.active_environment.mobile_sessions

    def handle_request(
        self,
        method: str,
        path: str,
        body: dict[str, Any] | None = None,
        bearer_token: str | None = None,
    ) -> dict[str, Any] | BinaryResponse:
        request_started = time.perf_counter()
        route_name = self.route_metric_name(method, path)
        try:
            auth_started = time.perf_counter()
            context = self.build_request_context(bearer_token)
            self.performance_metrics.record(
                "request_phase",
                f"{route_name}.auth",
                (time.perf_counter() - auth_started) * 1000.0,
            )
            context_token = self._request_context.set(context)
            try:
                domain_started = time.perf_counter()
                response = self._handle_request_unlocked(
                    method, path, body, bearer_token
                )
                self.performance_metrics.record(
                    "request_phase",
                    f"{route_name}.domain",
                    (time.perf_counter() - domain_started) * 1000.0,
                )
                return response
            finally:
                self._request_context.reset(context_token)
        finally:
            self.performance_metrics.record(
                "request_total",
                route_name,
                (time.perf_counter() - request_started) * 1000.0,
            )

    def build_request_context(self, bearer_token: str | None) -> RequestContext:
        session_id = None
        user = None
        server_id = self.default_server_id
        if bearer_token:
            session_token, user = self.auth.require_identity(bearer_token)
            server_id = self.normalize_server_id(session_token.server_id)
            session_id = hashlib.sha256(bearer_token.encode("utf-8")).hexdigest()
        return RequestContext(
            request_id=secrets.token_urlsafe(12),
            server_id=server_id,
            environment=self.environments[server_id],
            auth_session_id_digest=session_id,
            user_id=user.id if user else None,
            username=user.username if user else None,
            issued_at=datetime.now(timezone.utc).isoformat(),
            user=user,
        )

    @contextmanager
    def use_environment(self, environment: ServerEnvironment):
        current = self.request_context
        context = RequestContext(
            request_id=current.request_id if current else secrets.token_urlsafe(12),
            server_id=environment.id,
            environment=environment,
            auth_session_id_digest=current.auth_session_id_digest if current else None,
            user_id=current.user_id if current else None,
            username=current.username if current else None,
            issued_at=current.issued_at if current else datetime.now(timezone.utc).isoformat(),
            user=current.user if current else None,
        )
        token = self._request_context.set(context)
        try:
            yield context
        finally:
            self._request_context.reset(token)

    @staticmethod
    def route_metric_name(method: str, path: str) -> str:
        safe_segments = {
            "admin", "auth", "cancel", "change-password", "chat", "complete", "create",
            "download", "finalize", "gb-runtime-fixed-host-sessions", "game", "health",
            "heartbeat", "join", "leave", "link-sessions", "login",
            "logout", "me", "media", "mobile-scenarios", "mobile-sessions", "n64", "preflight",
            "ready", "register", "relay-ticket", "renew", "room-matching", "rooms", "rom-slots",
            "saves", "start", "stop", "time", "upload", "users",
        }
        segments = [segment for segment in path.split("/") if segment]
        normalized = [segment if segment in safe_segments else "{id}" for segment in segments]
        return f"{method.upper()}/{'/'.join(normalized) if normalized else ''}"

    def _handle_request_unlocked(
        self,
        method: str,
        path: str,
        body: dict[str, Any] | None = None,
        bearer_token: str | None = None,
    ) -> dict[str, Any] | BinaryResponse:
        segments = [segment for segment in path.split("/") if segment]
        body = body or {}
        if method == "GET" and segments == ["health"]:
            return {"ok": True}
        if method == "GET" and segments == ["time"]:
            now = datetime.now(timezone.utc)
            return {"server_time": {"unix_time": int(now.timestamp()), "iso": now.isoformat()}}
        if method == "POST" and segments == ["admin", "session"]:
            return self.create_admin_session(str(body.get("password", "")))
        if method == "POST" and segments == ["admin", "logout"]:
            return {"logged_out": True}
        if method == "POST" and segments == ["admin", "users", "issue"]:
            return self.issue_admin_user(
                str(body.get("username", body.get("prefix", ""))),
                str(body.get("email", "")),
            )
        if method == "POST" and segments == ["admin", "users", "reset-password"]:
            return self.reset_admin_password(str(body.get("username", "")))
        if method == "POST" and len(segments) == 4 and segments[0] == "admin" and segments[1] == "saves" and segments[3] == "replace":
            save_bytes = decode_base64_field(body, "save_data")
            self.sessions.require_save_not_in_active_game(segments[2])
            save = self.saves.admin_replace_save(segments[2], save_bytes)
            return {"save": save.to_dict()}
        if method == "GET" and segments == ["admin", "users"]:
            return {"users": self.admin_user_summaries()}
        if method == "GET" and segments == ["admin", "operations"]:
            return self.admin_operations_summary()
        if method == "GET" and segments == ["admin", "logs"]:
            return self.admin_logs()
        if method == "POST" and segments == ["auth", "register"]:
            if not self.allow_self_registration:
                raise RegistrationDisabledError("self-registration is disabled")
            user = self.auth.register(
                require_json_string(body, "username"),
                require_json_string(body, "password"),
            )
            return {"user": public_user(user.to_dict())}
        if method == "POST" and segments == ["auth", "login"]:
            server_id = self.normalize_server_id(
                require_json_string(body, "server_id", default=self.default_server_id)
            )
            token = self.auth.login(
                require_json_string(body, "username"),
                require_json_string(body, "password"),
                server_id=server_id,
                client_version_allowed=(not self.client_version_check_enabled or (
                    isinstance(body.get("client_version"), str) and
                    body["client_version"] in self.allowed_client_versions
                )),
            )
            user = self.auth.require_user(token.token)
            return {
                "token": token.to_dict(),
                "user": public_user(user.to_dict()),
                "server": self.public_environment(self.environments[server_id]),
            }
        if method == "POST" and segments == ["auth", "change-password"]:
            token = self.require_bearer_token(bearer_token)
            user = self.auth.change_password_for_user(
                self.require_user(token), require_json_string(body, "new_password")
            )
            return {"user": public_user(user.to_dict())}
        if method == "POST" and segments == ["auth", "logout"]:
            token = self.require_bearer_token(bearer_token)
            user = self.require_user(token)
            self.leave_room_for_user(
                user.id, auth_session_id=self.require_auth_session_id_digest()
            )
            self.auth.logout(token)
            return {"logged_out": True}
        if method == "GET" and segments == ["me"]:
            user = self.require_user(bearer_token)
            return {
                "user": public_user(user.to_dict()),
                "server": self.public_environment(self.active_environment),
            }
        if method == "POST" and segments == ["room-matching", "create"]:
            user = self.require_user(bearer_token)
            self.enforce_room_matching_account_rate_limit(user.id, "create")
            if self.sessions.active_game_session_for_user(user.id):
                raise AlreadyInRoomError("already in a ROOM or game session")
            try:
                room = self.room_manager.create_room(
                    require_json_string(body, "mode"), user.id, user.username,
                    self.require_auth_session_id_digest(),
                )
            except RoomPoolFullError:
                self.room_matching_metrics["create_pool_full"] += 1
                raise
            self.room_matching_metrics["create_success"] += 1
            return {"room": self.public_room(room, viewer_user_id=user.id)}
        if method == "POST" and segments == ["room-matching", "join"]:
            user = self.require_user(bearer_token)
            self.enforce_room_matching_account_rate_limit(user.id, "join")
            if self.sessions.active_game_session_for_user(user.id):
                raise AlreadyInRoomError("already in a ROOM or game session")
            try:
                room = self.room_manager.join_room_by_code(
                    require_json_string(body, "room_code"), user.id, user.username,
                    self.require_auth_session_id_digest(),
                )
            except RoomCodeUnavailableError:
                self.room_matching_metrics["join_unavailable"] += 1
                raise
            self.room_matching_metrics["join_success"] += 1
            return {"room": self.public_room(room, viewer_user_id=user.id)}
        if method == "GET" and segments == ["room-matching", "current"]:
            user = self.require_user(bearer_token)
            self.prune_stale_room_users()
            room = self.room_manager.current_room(user.id)
            return {
                "room": (
                    self.public_room(room, viewer_user_id=user.id)
                    if room is not None
                    else None
                )
            }
        if method == "POST" and segments == ["room-matching", "leave"]:
            user = self.require_user(bearer_token)
            left = self.leave_room_for_user(
                user.id, auth_session_id=self.require_auth_session_id_digest()
            )
            return {"left": left}
        if method == "POST" and len(segments) == 3 and segments[0] == "rooms" and segments[2] == "state":
            user = self.require_user(bearer_token)
            room_number = int(segments[1])
            self.require_room_mutable(room_number)
            slot = str(body["slot"]) if "slot" in body else None
            n64_slot = str(body["n64_slot"]) if "n64_slot" in body else None
            ready = bool(body["ready"]) if "ready" in body else None
            requested_link_mode = str(body["link_mode"]) if "link_mode" in body else None
            self.validate_n64_room_state(room_number, user.id, slot, n64_slot, ready)
            if N64_ROOM_FIRST <= room_number <= N64_ROOM_LAST:
                media_session = self.n64_runtime_media_sessions.find_active_for_room(room_number)
                if media_session and (slot is not None or n64_slot is not None or ready is not None):
                    auth_session_id = self.require_auth_session_id_digest()
                    if not self.auth_session_owns_n64_runtime_media_reservation(
                        media_session,
                        user.id,
                        auth_session_id,
                    ):
                        raise ValidationError("N64 Runtime media session belongs to a different auth session")
                    self.cancel_n64_runtime_media_session(media_session)
            current_room = self.room_manager.room(room_number)
            if current_room.link_session_id and (
                ready is False or slot is not None or requested_link_mode is not None
            ):
                try:
                    active_link = self.sessions.get_session(current_room.link_session_id)
                except NotFoundError:
                    active_link = None
                if active_link and active_link.protocol_id == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                    self.gb_runtime_fixed_host_sessions.cancel(
                        active_link.id, "ROOM READY or selection changed"
                    )
                    if active_link.status in ROOM_LEAVE_CANCEL_STATUSES:
                        self.sessions.transition(
                            active_link.id,
                            LinkSessionStatus.CANCELLED,
                            reason="fixed Host ROOM state changed",
                        )
                    self.room_manager.end_game_for_link_session(active_link.id)
            room = self.room_manager.update_user_state(
                room_number,
                user.id,
                slot=slot,
                n64_slot=n64_slot,
                ready=ready,
                link_mode=requested_link_mode,
            )
            return {"room": self.public_room(room, viewer_user_id=user.id)}
        if method == "POST" and len(segments) == 3 and segments[0] == "rooms" and segments[2] == "chat":
            user = self.require_user(bearer_token)
            room = self.room_manager.add_chat(int(segments[1]), user.id, user.username, str(body.get("message", "")))
            return {"room": self.public_room(room, viewer_user_id=user.id)}
        if method == "POST" and len(segments) == 3 and segments[0] == "rooms" and segments[2] == "start":
            user = self.require_user(bearer_token)
            return self.start_room(
                int(segments[1]),
                user.id,
                auth_session_id=self.require_auth_session_id_digest(),
                link_mode=str(body.get("link_mode", "")),
                expected_media_session_id=str(body.get("expected_media_session_id", "")),
            )
        if method == "POST" and segments == ["rooms", "heartbeat"]:
            user = self.require_user(bearer_token)
            auth_session_id = self.require_auth_session_id_digest()
            present = self.room_manager.heartbeat(user.id, auth_session_id)
            renewed = self.renew_room_session_locks_for_user(user.id, auth_session_id)
            renewed.extend(self.renew_n64_runtime_media_locks_for_user(user.id, auth_session_id))
            now = datetime.now(timezone.utc)
            return {
                "present": present,
                "lock_renewed": renewed,
                "save_sync": self.sync_room_saves_for_user(user.id),
                "server_time": {"unix_time": int(now.timestamp()), "iso": now.isoformat()},
                "session_lifecycle": self.session_lifecycle_for_user(user.id),
            }
        if method == "POST" and segments == ["roms"]:
            user = self.require_user(bearer_token)
            if "game_type" in body:
                raise ValidationError("game_type is assigned by the server")
            registration = self.roms.register(
                user.id,
                sha256=require_json_string(body, "sha256"),
                title=require_json_string(body, "title"),
                platform=require_json_string(body, "platform"),
                region=require_json_string(body, "region"),
                sha1=(require_json_string(body, "sha1") if "sha1" in body else None),
                rom_header_title=require_json_string(body, "rom_header_title"),
            )
            return {"rom": registration.to_dict()}
        if method == "GET" and segments == ["roms"]:
            user = self.require_user(bearer_token)
            return {"roms": [registration.to_dict() for registration in self.roms.list_for_user(user.id)]}
        if method == "GET" and len(segments) == 2 and segments[0] == "roms":
            user = self.require_user(bearer_token)
            return {"rom": self.roms.get(segments[1], user.id).to_dict()}
        if method == "GET" and segments == ["rom-slots"]:
            user = self.require_user(bearer_token)
            return {"slots": [slot.to_dict() for slot in self.rom_slots.list_for_user(user.id)]}
        if method == "POST" and segments == ["rom-slots", "apply"]:
            user = self.require_user(bearer_token)
            if self.sessions.active_game_session_for_user(user.id):
                raise ValidationError("ROM slots cannot be changed during an active game session")
            desired_slots = []
            for item in list(body.get("slots", [])):
                desired = dict(item)
                if "game_type" in desired:
                    raise ValidationError("game_type is assigned by the server")
                if "initial_save_data" in desired:
                    if not self.allow_user_initial_save_import:
                        raise ValidationError(
                            "initial_save_data is disabled by server policy"
                        )
                    desired["initial_save_bytes"] = decode_base64_field(desired, "initial_save_data")
                    desired.pop("initial_save_data", None)
                desired_slots.append(desired)
            return self.rom_slots.apply_slots(
                user.id,
                desired_slots,
                confirm_delete_saves=bool(body.get("confirm_delete_saves", False)),
            )
        if method == "GET" and segments == ["saves"]:
            user = self.require_user(bearer_token)
            saves = self.saves.list_for_user(user.id)
            self.sessions.require_saves_not_in_partial_pair_commit(
                [save.id for save in saves]
            )
            return {"saves": [save.to_dict() for save in saves]}
        if method == "GET" and len(segments) == 2 and segments[0] == "saves":
            user = self.require_user(bearer_token)
            self.sessions.require_saves_not_in_partial_pair_commit([segments[1]])
            save, save_bytes = self.saves.download(segments[1], user.id)
            return {"save": save.to_dict(), "save_data": encode_bytes(save_bytes)}
        if method == "PUT" and len(segments) == 2 and segments[0] == "saves":
            user = self.require_user(bearer_token)
            request_id = str(body.get("request_id", "")).strip()
            save_bytes = decode_base64_field(body, "save_data")
            expected_revision = int(body.get("expected_revision", 0))
            auth_session_id = self.require_auth_session_id_digest()
            game_session_id = str(body.get("game_session_id", ""))
            fencing_token = int(body.get("fencing_token", 0))
            lock = self.sessions.authorize_normal_save_upload(
                user.id, segments[1], auth_session_id, game_session_id, fencing_token
            )
            authority = SaveUploadAuthority.unlocked(
                lock.execution_mode if lock else "UNLOCKED",
                game_session_id=lock.game_session_id if lock else "",
                game_run_id=lock.game_run_id if lock else "",
                fencing_token=lock.fencing_token if lock else 0,
                auth_session_id=auth_session_id,
            )
            if not request_id:
                save = self.saves.upload(
                    segments[1], user.id, expected_revision, save_bytes
                )
                return {"save": save.to_dict(), "idempotent_replay": False}
            save, replay = self.save_uploads.commit(
                user_id=user.id,
                save_id=segments[1],
                request_id=request_id,
                expected_revision=expected_revision,
                save_bytes=save_bytes,
                authority=authority,
            )
            return {"save": save.to_dict(), "idempotent_replay": replay}
        if method == "POST" and segments == ["link-sessions"]:
            raise NotFoundError("route not found")
        if method == "POST" and segments == ["link-sessions", "self"]:
            raise NotFoundError("route not found")
        if method == "POST" and segments == ["mobile-scenarios"]:
            user = self.require_user(bearer_token)
            save_id = require_json_string(body, "save_id")
            rom_id = require_json_string(body, "rom_id")
            matching_slots = [
                slot
                for slot in self.rom_slots.list_for_user(user.id)
                if slot.save_id == save_id and slot.rom_id == rom_id
            ]
            if len(matching_slots) != 1:
                raise ValidationError("Mobile ROM/SAV binding is not registered in a slot")
            slot, registration = self.rom_slots.ensure_trusted_header_title(matching_slots[0])
            if not registration.rom_header_title or registration.rom_header_title != slot.rom_header_title:
                raise ValidationError("registered ROM header title is unavailable or inconsistent")
            scenarios = self.mobile_package_catalog.scenarios(registration.rom_header_title)
            return {
                "rom_header_title": registration.rom_header_title,
                "scenarios": list(scenarios),
            }
        if method == "POST" and segments == ["mobile-sessions"]:
            user = self.require_user(bearer_token)
            auth_session_id = self.require_auth_session_id_digest()
            save_id = require_json_string(body, "save_id")
            rom_id = require_json_string(body, "rom_id")
            create_request_id = require_json_string(body, "request_id")
            scenario_id = str(body.get("scenario_id", "")).strip() or None
            matching_slots = [
                slot
                for slot in self.rom_slots.list_for_user(user.id)
                if slot.save_id == save_id and slot.rom_id == rom_id
            ]
            if len(matching_slots) != 1:
                raise ValidationError("Mobile ROM/SAV binding is not registered in a slot")
            slot, registration = self.rom_slots.ensure_trusted_header_title(matching_slots[0])
            if not registration.rom_header_title or registration.rom_header_title != slot.rom_header_title:
                raise ValidationError("registered ROM header title is unavailable or inconsistent")
            session, runtime_contract, replay = self.mobile_sessions.create(
                user_id=user.id,
                auth_session_id_digest=auth_session_id,
                save_id=save_id,
                rom_id=rom_id,
                rom_header_title=registration.rom_header_title,
                requested_scenario_id=scenario_id,
                package_resolver=lambda: self.mobile_package_catalog.select(
                    registration.rom_header_title, scenario_id
                ),
                lease_expires_at=self.game_session_lease_expires_at(),
                create_request_id=create_request_id,
            )
            response = {
                "mobile_session": session.to_public_dict(),
                "idempotent_replay": replay,
            }
            if runtime_contract is not None:
                response["runtime_contract"] = runtime_contract
            return response
        if method == "GET" and len(segments) == 2 and segments[0] == "mobile-sessions":
            user = self.require_user(bearer_token)
            session = self.mobile_sessions.get(segments[1], user.id)
            return {"mobile_session": session.to_public_dict()}
        if (
            method == "POST"
            and len(segments) == 3
            and segments[0] == "mobile-sessions"
            and segments[2] == "heartbeat"
        ):
            user = self.require_user(bearer_token)
            session = self.mobile_sessions.heartbeat(
                segments[1],
                user_id=user.id,
                auth_session_id_digest=self.require_auth_session_id_digest(),
                game_session_id=require_json_string(body, "game_session_id"),
                fencing_token=int(body.get("fencing_token", 0)),
                lease_expires_at=self.game_session_lease_expires_at(),
            )
            return {"mobile_session": session.to_public_dict()}
        if (
            method == "POST"
            and len(segments) == 3
            and segments[0] == "mobile-sessions"
            and segments[2] == "complete"
        ):
            user = self.require_user(bearer_token)
            session = self.mobile_sessions.complete(
                segments[1],
                user_id=user.id,
                auth_session_id_digest=self.require_auth_session_id_digest(),
                game_session_id=require_json_string(body, "game_session_id"),
                fencing_token=int(body.get("fencing_token", 0)),
            )
            return {"mobile_session": session.to_public_dict()}
        if (
            method == "POST"
            and len(segments) == 3
            and segments[0] == "mobile-sessions"
            and segments[2] == "cancel"
        ):
            user = self.require_user(bearer_token)
            session = self.mobile_sessions.cancel(
                segments[1],
                user_id=user.id,
                auth_session_id_digest=self.require_auth_session_id_digest(),
                game_session_id=require_json_string(body, "game_session_id"),
                fencing_token=int(body.get("fencing_token", 0)),
                reason=str(body.get("reason", "client canceled")),
            )
            return {"mobile_session": session.to_public_dict()}
        if method == "GET" and segments == ["game", "status"]:
            user = self.require_user(bearer_token)
            return self.game_status_for_user(user.id, self.require_auth_session_id_digest())
        if method == "POST" and segments == ["game", "start"]:
            user = self.require_user(bearer_token)
            execution_mode = str(body.get("execution_mode", "")).strip().upper()
            if execution_mode not in {"LOCAL_CLIENT", "N64_RUNTIME_CLIENT"}:
                raise ValidationError("execution_mode is not supported")
            save_ids = body.get("save_ids", [])
            if not isinstance(save_ids, list) or not save_ids:
                raise ValidationError("save_ids is required")
            self.validate_unique_save_ids([str(save_id) for save_id in save_ids])
            saves = [self.saves.get_save(str(save_id), user.id) for save_id in save_ids]
            if execution_mode == "LOCAL_CLIENT":
                for save in saves:
                    self.require_gb_gbc_save(save, "local play")
            else:
                self.validate_n64_runtime_saves(saves)
            lock = self.sessions.acquire_local_game_session_lock(
                user.id,
                self.game_session_lease_expires_at(),
                self.require_auth_session_id_digest(),
                execution_mode=execution_mode,
                saves=saves,
            )
            return {"game_session": self.public_game_session_lock(lock, owner=True), "link_session": None}
        if method == "POST" and segments == ["game", "heartbeat"]:
            user = self.require_user(bearer_token)
            lock = self.sessions.renew_local_game_session_lock(
                user.id,
                self.game_session_lease_expires_at(),
                self.require_auth_session_id_digest(),
                str(body.get("game_session_id", "")),
                int(body.get("fencing_token", 0)),
            )
            return {"game_session": self.public_game_session_lock(lock, owner=True), "link_session": None}
        if method == "POST" and segments == ["game", "stop"]:
            user = self.require_user(bearer_token)
            auth_session_id = self.require_auth_session_id_digest()
            active = self.sessions.require_game_session_owner(user.id, auth_session_id)
            self.sessions.require_matching_game_session_fence(
                active["lock"],
                str(body.get("game_session_id", "")),
                int(body.get("fencing_token", 0)),
            )
            session = active["link_session"]
            if session is None:
                lock = self.sessions.release_user_game_session_lock(
                    user.id,
                    auth_session_id,
                    str(body.get("game_session_id", "")),
                    int(body.get("fencing_token", 0)),
                )
                return {"game_session": self.public_game_session_lock(lock, owner=True), "link_session": None, "room": None}
            if session.status in {LinkSessionStatus.FINALIZING.value, LinkSessionStatus.RECOVERING.value}:
                session = self.sessions.reconcile_finalization(session.id)
                room = None
                if self.sessions.client_saves_are_final(session.id):
                    session = self.sessions.transition(session.id, LinkSessionStatus.COMPLETED)
                    room = self.room_manager.end_game_for_link_session(session.id)
                return {
                    "link_session": session.to_dict(),
                    "room": self.public_room(room, viewer_user_id=user.id) if room else None,
                }
            room = self.end_room_game_for_session(session.id, reason="game stopped")
            stopped = self.sessions.get_session(session.id)
            return {
                "link_session": stopped.to_dict(),
                "room": self.public_room(room, viewer_user_id=user.id) if room else None,
            }
        if method == "GET" and segments == ["link-sessions"]:
            user = self.require_user(bearer_token)
            return {"link_sessions": [session.to_dict() for session in self.sessions.list_for_user(user.id)]}
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "manifest"
            and method == "GET"
        ):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            control = self.reconcile_gb_runtime_fixed_host_trade(segments[1])
            return {"gb_runtime_fixed_host_session": control.to_public_dict(user.id)}
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "host-finish"
            and method == "POST"
        ):
            user = self.require_user(bearer_token)
            auth_session_id = self.require_auth_session_id_digest()
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            control = self.gb_runtime_fixed_host_sessions.get(session.id)
            manifest = control.manifest_object()
            if manifest.role_for(user.id) != "host":
                raise ValidationError("fixed Host finish requires host role")
            host_encoded = require_json_string(body, "host_candidate")
            remote_encoded = require_json_string(body, "remote_candidate")
            try:
                host_candidate = base64.b64decode(host_encoded, validate=True)
                remote_candidate = base64.b64decode(remote_encoded, validate=True)
            except (ValueError, base64.binascii.Error) as exc:
                raise ValidationError("fixed Host save candidate encoding is invalid") from exc
            if not host_candidate or not remote_candidate:
                raise ValidationError("both fixed Host save candidates are required")
            host_sha256 = hashlib.sha256(host_candidate).hexdigest()
            remote_sha256 = hashlib.sha256(remote_candidate).hexdigest()
            if control.host_finish and (
                control.host_finish.get("host_candidate_sha256") != host_sha256
                or control.host_finish.get("remote_candidate_sha256") != remote_sha256
            ):
                raise ValidationError("fixed Host finish is immutable")
            if control.state == "FINISHED":
                control = self.gb_runtime_fixed_host_sessions.submit_host_finish(
                    session.id,
                    user.id,
                    terminal_digest=require_json_string(body, "terminal_digest"),
                    final_frame=int(body.get("final_frame", -1)),
                    host_candidate_sha256=host_sha256,
                    remote_candidate_sha256=remote_sha256,
                )
                return {"gb_runtime_fixed_host_session": control.to_public_dict(user.id)}
            staged = self.sessions.stage_gb_runtime_fixed_host_candidates(
                session.id,
                user.id,
                auth_session_id,
                require_json_string(body, "game_session_id"),
                int(body.get("fencing_token", 0)),
                host_candidate,
                remote_candidate,
            )
            control = self.gb_runtime_fixed_host_sessions.submit_host_finish(
                session.id,
                user.id,
                terminal_digest=require_json_string(body, "terminal_digest"),
                final_frame=int(body.get("final_frame", -1)),
                host_candidate_sha256=staged["host_sha256"],
                remote_candidate_sha256=staged["remote_sha256"],
            )
            control = self.reconcile_gb_runtime_fixed_host_trade(session.id)
            return {"gb_runtime_fixed_host_session": control.to_public_dict(user.id)}
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "terminal-receipt"
            and method == "POST"
        ):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            control = self.gb_runtime_fixed_host_sessions.submit_remote_receipt(
                session.id,
                user.id,
                terminal_digest=require_json_string(body, "terminal_digest"),
                final_frame=int(body.get("final_frame", -1)),
            )
            control = self.reconcile_gb_runtime_fixed_host_trade(session.id)
            return {"gb_runtime_fixed_host_session": control.to_public_dict(user.id)}
        if (method == "POST" and len(segments) == 3
                and segments[0] == "gb-runtime-fixed-host-sessions" and segments[2] == "blocked"):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            self.sessions.bind_game_session_lock(session.id, user.id, self.require_auth_session_id_digest())
            control = self.gb_runtime_fixed_host_sessions.block_preflight(session.id, user.id, require_json_string(body, "reason"))
            return {"gb_runtime_fixed_host_session": control.to_public_dict(user.id)}
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "preflight"
            and method == "POST"
        ):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            self.sessions.bind_game_session_lock(
                session.id, user.id, self.require_auth_session_id_digest()
            )
            available = body.get("available_roms")
            if not isinstance(available, list) or not all(
                isinstance(item, dict) for item in available
            ):
                raise ValidationError("fixed Host available ROM metadata is invalid")
            control = self.gb_runtime_fixed_host_sessions.submit_preflight(
                segments[1],
                user.id,
                manifest_digest=require_json_string(body, "manifest_digest"),
                protocol_id=require_json_string(body, "protocol_id"),
                runtime_build_id=require_json_string(body, "runtime_build_id"),
                available_roms=available,
            )
            return {"gb_runtime_fixed_host_session": control.to_public_dict(user.id)}
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "relay-ticket"
            and method == "POST"
        ):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            control, role, ticket = self.gb_runtime_fixed_host_sessions.issue_ticket(
                segments[1], user.id, resume=body.get("resume") is True
            )
            if control.state == "WAITING_PEER":
                current_link = self.sessions.get_session(session.id)
                if current_link.status in {
                    LinkSessionStatus.CREATED.value,
                    LinkSessionStatus.WAITING_PLAYER_A.value,
                    LinkSessionStatus.WAITING_PLAYER_B.value,
                    LinkSessionStatus.PREPARING.value,
                }:
                    self.sessions.transition(session.id, LinkSessionStatus.RUNNING)
            manifest = control.manifest_object()
            return {
                "gb_runtime_fixed_host_session": control.to_public_dict(user.id),
                "connection": {
                    "relay_host": self.n64_runtime_media_relay_host,
                    "relay_port": self.n64_runtime_media_relay_port,
                    "relay_transport": self.n64_runtime_media_relay_transport,
                    "role": role,
                    "scope": GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
                    "ticket": ticket,
                    "runtime_config": {
                        "save_policy": manifest.save_policy,
                        "requested_mode": manifest.requested_mode,
                    },
                },
            }
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "runtime-snapshots"
            and method == "GET"
        ):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            control = self.gb_runtime_fixed_host_sessions.get(segments[1])
            manifest = control.manifest_object()
            if manifest.role_for(user.id) != "host":
                raise ValidationError("fixed Host snapshots require host role")
            if control.state not in {"READY", "WAITING_PEER"}:
                raise ValidationError("fixed Host session is not ready")
            self.saves.verify_locked_revision(
                manifest.host_save_id,
                manifest.host_user_id,
                session.id,
                manifest.host_base_revision,
            )
            self.saves.verify_locked_revision(
                manifest.remote_save_id,
                manifest.remote_user_id,
                session.id,
                manifest.remote_base_revision,
            )
            host_save, host_bytes = self.saves.download(
                manifest.host_save_id, manifest.host_user_id
            )
            remote_save, remote_bytes = self.saves.download(
                manifest.remote_save_id, manifest.remote_user_id
            )
            return {
                "runtime_snapshots": {
                    "manifest_digest": control.manifest_digest,
                    "save_policy": manifest.save_policy,
                    "host": {
                        "game_type": host_save.game_type,
                        "revision": host_save.revision,
                        "sha256": host_save.sha256,
                        "save_data": encode_bytes(host_bytes),
                    },
                    "remote": {
                        "game_type": remote_save.game_type,
                        "revision": remote_save.revision,
                        "sha256": remote_save.sha256,
                        "save_data": encode_bytes(remote_bytes),
                    },
                }
            }
        if (
            len(segments) == 3
            and segments[0] == "gb-runtime-fixed-host-sessions"
            and segments[2] == "cancel"
            and method == "POST"
        ):
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                raise NotFoundError("fixed Host session not found")
            control = self.gb_runtime_fixed_host_sessions.cancel(segments[1], "session cancelled")
            if session.status in ROOM_LEAVE_CANCEL_STATUSES:
                session = self.sessions.transition(
                    session.id, LinkSessionStatus.CANCELLED, reason="fixed Host cancelled"
                )
            room = self.room_manager.end_game_for_link_session(session.id)
            return {
                "gb_runtime_fixed_host_session": control.to_public_dict(user.id),
                "link_session": session.to_dict(),
                "room": self.public_room(room, viewer_user_id=user.id) if room else None,
            }
        if method == "GET" and len(segments) == 2 and segments[0] == "n64-runtime-media-sessions":
            user = self.require_user(bearer_token)
            media_session = self.n64_runtime_media_sessions.get(segments[1])
            role = self.n64_runtime_media_sessions.role_for_user(media_session, user.id)
            return {"media_session": media_session.to_public_dict(role)}
        if (method == "POST" and len(segments) == 3
                and segments[0] == "n64-runtime-media-sessions"
                and segments[2] == "terminate"):
            user = self.require_user(bearer_token)
            return self.terminate_n64_room(segments[1], user.id,
                self.require_auth_session_id_digest(), body.get("room_code"),
                preflight_failed=body.get("reason") == "preflight_failed")
        if (method == "POST" and len(segments) == 3
                and segments[0] == "n64-runtime-media-sessions"
                and segments[2] == "finish"):
            user = self.require_user(bearer_token)
            return self.finish_n64_room(segments[1], user.id, self.require_auth_session_id_digest())
        if (method == "POST" and len(segments) == 3
                and segments[0] == "n64-runtime-media-sessions"
                and segments[2] == "recover"):
            user = self.require_user(bearer_token)
            media = self.n64_runtime_media_sessions.get(segments[1])
            role = self.n64_runtime_media_sessions.role_for_user(media, user.id)
            if media.status in ACTIVE_MEDIA_STATUSES:
                if not self.auth_session_owns_n64_runtime_media_reservation(
                        media, user.id, self.require_auth_session_id_digest()):
                    raise ValidationError("N64 recovery requires the active participant reservation")
                media = self.n64_runtime_media_sessions.recover(media.id)
            return {"media_session": media.to_public_dict(role)}
        if (
            method == "GET"
            and len(segments) == 4
            and segments[0] == "n64-runtime-media-sessions"
            and segments[2] == "runtime-saves"
        ):
            user = self.require_user(bearer_token)
            media_session, save_id, owner_user_id = self.n64_runtime_media_sessions.runtime_save_binding(
                segments[1], user.id, segments[3]
            )
            room = self.room_manager.room(media_session.room_number)
            if len(room.users) != 2 or not all(item.get("ready") for item in room.users):
                raise ValidationError("N64 room is no longer ready")
            host_n64, host_gb, remote_gb = self.validate_n64_room_selection(room)
            current_ids = (host_n64.save_id, host_gb.save_id, remote_gb.save_id)
            expected_ids = (
                media_session.host_n64_save_id,
                media_session.host_save_id,
                media_session.remote_save_id,
            )
            if current_ids != expected_ids:
                raise ValidationError("N64 room selection changed")
            self.require_n64_runtime_media_participant_locks(
                media_session,
                requester_auth_session_id=self.require_auth_session_id_digest(),
            )
            save, save_bytes = self.saves.download(save_id, owner_user_id)
            return {
                "runtime_save": {
                    "kind": segments[3],
                    "game_type": save.game_type,
                    "revision": save.revision,
                    "sha256": save.sha256,
                    "no_save": True,
                },
                "save_data": encode_bytes(save_bytes),
            }
        if method == "GET" and len(segments) == 2 and segments[0] == "link-sessions":
            user = self.require_user(bearer_token)
            session = self.require_session_member(segments[1], user.id)
            session = self.sessions.reconcile_finalization(session.id)
            return {"link_session": session.to_dict()}
        if method == "GET" and len(segments) == 3 and segments[0] == "link-sessions" and segments[2] == "events":
            user = self.require_user(bearer_token)
            self.require_session_member(segments[1], user.id)
            return {"events": self.sessions.list_events(segments[1])}
        raise NotFoundError("route not found")

    def create_admin_session(self, password: str) -> dict[str, Any]:
        if not self.admin_password:
            raise AuthenticationError("admin password is not configured")
        if not hmac.compare_digest(password, self.admin_password):
            raise AuthenticationError("invalid admin password")
        token = secrets.token_urlsafe(32)
        expires_at = datetime.now(timezone.utc) + timedelta(hours=12)
        self.admin_sessions[token] = expires_at
        return {"admin_session": {"token": token, "expires_at": expires_at.isoformat()}}

    def normalize_server_id(self, server_id: str) -> str:
        normalized = server_id.strip().lower()
        if not normalized:
            normalized = self.default_server_id
        if normalized not in self.environments:
            raise ValidationError("server_id is not supported")
        return normalized

    def public_environment(self, environment: ServerEnvironment) -> dict[str, Any]:
        return {
            "id": environment.id,
            "name": environment.name,
            "allow_user_initial_save_import": self.allow_user_initial_save_import,
        }

    def require_admin_session(self, token: str | None) -> None:
        if not token:
            raise AuthenticationError("admin login required")
        expires_at = self.admin_sessions.get(token)
        if not expires_at or expires_at <= datetime.now(timezone.utc):
            self.admin_sessions.pop(token, None)
            raise AuthenticationError("admin login required")

    def destroy_admin_session(self, token: str | None) -> None:
        if token:
            self.admin_sessions.pop(token, None)

    def auth_session_owns_n64_runtime_media_reservation(
        self,
        media_session,
        user_id: str,
        auth_session_id: str,
    ) -> bool:
        if not auth_session_id:
            return False
        active = self.sessions.active_game_session_for_user(user_id)
        if not active:
            return False
        lock = active["lock"]
        return bool(
            lock.execution_mode == "N64_RUNTIME_NOSAVE"
            and lock.game_run_id == media_session.id
            and lock.auth_session_id == auth_session_id
        )

    def finish_n64_room(self, session_id: str, user_id: str, auth_session_id: str) -> dict[str, Any]:
        manager = self.n64_runtime_media_sessions
        # Match the manager's lock order: filesystem lock before SQLite write.
        # All three terminal records become visible to Relay together on commit.
        with manager.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            with self.authority_database.write_unit():
                records = manager.storage.load_n64_runtime_media_sessions()
                media = manager._get_locked(records, session_id)
                role = manager.role_for_user(media, user_id)
                if role != "host":
                    raise ValidationError("N64 finish requires host role")
                if media.status not in ACTIVE_MEDIA_STATUSES:
                    return {"media_session": media.to_public_dict(role)}
                if not self.auth_session_owns_n64_runtime_media_reservation(media, user_id, auth_session_id):
                    raise ValidationError("N64 finish requires the active host reservation")
                room = self.room_manager.current_room(user_id)
                if room is None or room.room_number != media.room_number:
                    raise ValidationError("N64 finish room does not match session")
                media = replace(media, status="COMPLETED", termination_reason="host_finished",
                                recovery_deadline=None, updated_at=datetime.now(timezone.utc).isoformat(timespec="milliseconds"))
                manager._save_locked(records, media)
                media = manager._get_locked(manager.storage.load_n64_runtime_media_sessions(), session_id)
                return {"media_session": media.to_public_dict(role)}

    def terminate_n64_room(self, session_id: str, user_id: str,
                           auth_session_id: str, room_code: str, *, preflight_failed: bool = False) -> dict[str, Any]:
        manager = self.n64_runtime_media_sessions
        with manager.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            with self.authority_database.write_unit():
                records = manager.storage.load_n64_runtime_media_sessions()
                media = manager._get_locked(records, session_id)
                role = manager.role_for_user(media, user_id)
                if not room_code or room_code != media.room_code or media.room_created_at_ms is None:
                    raise ValidationError("N64 termination ROOM binding mismatch")
                if media.status not in ACTIVE_MEDIA_STATUSES:
                    return {"media_session": media.to_public_dict(role)}
                if preflight_failed and (role != "host" or not
                        self.auth_session_owns_n64_runtime_media_reservation(media, user_id, auth_session_id)):
                    raise ValidationError("N64 preflight failure requires the active host reservation")
                active = self.sessions.active_game_session_for_user(user_id)
                if (active and active["lock"].game_run_id == media.id and
                        not self.auth_session_owns_n64_runtime_media_reservation(media, user_id, auth_session_id)):
                    raise ValidationError("N64 termination requires the participant reservation")
                # Repository compares this session's immutable ROOM code AND
                # creation generation before removing membership, in this unit.
                expired = manager._expired(media)
                reason = ("recovery_timeout" if media.recovery_deadline else "session_expired") if expired else "participant_left"
                if preflight_failed and not expired:
                    reason = "preflight_failed"
                media = replace(media, status="EXPIRED" if expired else "CANCELLED", termination_reason=reason,
                    recovery_deadline=None, updated_at=datetime.now(timezone.utc).isoformat(timespec="milliseconds"))
                manager._save_locked(records, media)
                media = manager._get_locked(manager.storage.load_n64_runtime_media_sessions(), session_id)
                return {"media_session": media.to_public_dict(role)}

    def cancel_n64_runtime_media_session(self, media_session) -> None:
        self.n64_runtime_media_sessions.cancel(media_session.id)

    def cancel_n64_runtime_media_sessions_for_user(
        self,
        user_id: str,
        auth_session_id: str,
        *,
        reject_non_owner: bool = False,
    ) -> bool:
        media_sessions = self.n64_runtime_media_sessions.active_for_user(user_id)
        for media_session in media_sessions:
            if self.auth_session_owns_n64_runtime_media_reservation(
                media_session,
                user_id,
                auth_session_id,
            ):
                continue
            active = self.sessions.active_game_session_for_user(user_id)
            lock = active["lock"] if active else None
            if (
                lock
                and lock.execution_mode == "N64_RUNTIME_NOSAVE"
                and lock.game_run_id == media_session.id
            ):
                if reject_non_owner:
                    raise ValidationError("N64 Runtime media session belongs to a different auth session")
                return False
            # A media record can outlive its lease-backed game lock after an
            # abandoned client. It is no longer runnable, so a fresh Auth
            # Session for the same participant may clear the orphan.
            self.cancel_n64_runtime_media_session(media_session)
        for media_session in media_sessions:
            self.cancel_n64_runtime_media_session(media_session)
        return True

    def leave_room_for_user(self, user_id: str, auth_session_id: str = "") -> bool:
        leaving_room = self.room_manager.current_room(user_id)
        if not self.cancel_n64_runtime_media_sessions_for_user(user_id, auth_session_id):
            return False
        session_ids = self.room_manager.link_session_ids_for_user(user_id)
        for session in self.sessions.list_for_user(user_id):
            if LinkSessionStatus(session.status) in ACTIVE_STATUSES and session.id not in session_ids:
                session_ids.append(session.id)
        for session_id in session_ids:
            try:
                fixed_link = self.sessions.get_session(session_id)
            except NotFoundError:
                fixed_link = None
            if fixed_link and fixed_link.protocol_id == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                try:
                    self.gb_runtime_fixed_host_sessions.cancel(session_id, "ROOM participant left")
                except NotFoundError:
                    pass
            try:
                self.sessions.mark_participant_disconnected(session_id, user_id)
            except LeagueError:
                pass
        if leaving_room is not None:
            self.room_manager.leave_room(user_id, expected_room_code=leaving_room.room_code,
                close_link_game=bool(leaving_room.link_session_id or leaving_room.post_game_at))
        for session_id in session_ids:
            if not self.auth_session_can_cancel_room_game(session_id, user_id, auth_session_id):
                continue
            try:
                session = self.sessions.get_session(session_id)
            except NotFoundError:
                continue
            if session.status not in ROOM_LEAVE_CANCEL_STATUSES:
                continue
            if session.host_process_id:
                try:
                    self.host_processes.terminate(session.host_process_id)
                except LeagueError:
                    pass
            try:
                self.sync_link_session_saves(session_id, suppress_errors=False)
            except LeagueError:
                if session.host_process_id:
                    continue
            current = self.sessions.get_session(session_id)
            if current.status in ROOM_LEAVE_CANCEL_STATUSES:
                self.sessions.transition(session_id, LinkSessionStatus.CANCELLED, reason="room left")
        return True

    def auth_session_can_cancel_room_game(self, session_id: str, user_id: str, auth_session_id: str) -> bool:
        if not auth_session_id:
            return True
        try:
            active = self.sessions.active_game_session_for_user(user_id)
        except NotFoundError:
            return True
        if not active:
            return True
        if active["link_session"].id != session_id:
            return True
        lock = active["lock"]
        return bool(lock.auth_session_id and lock.auth_session_id == auth_session_id)

    def prune_stale_room_users(self) -> list[str]:
        active_media = self.n64_runtime_media_sessions.active_sessions()
        pruned_session_ids = self.room_manager.prune_stale_users()
        for session_id in pruned_session_ids:
            self.cancel_room_game_for_session(session_id, reason="room presence stale")
        for media_session in active_media:
            room = self.room_manager.room(media_session.room_number)
            actual_users = {str(user.get("user_id", "")) for user in room.users}
            expected_users = {media_session.host_user_id, media_session.remote_user_id}
            if actual_users == expected_users:
                continue
            self.n64_runtime_media_sessions.cancel(media_session.id, reason="room presence stale")
        return pruned_session_ids

    def require_room_mutable(self, room_number: int) -> None:
        room = self.room_manager.room(room_number)
        if not room.link_session_id:
            return
        try:
            session = self.sessions.get_session(room.link_session_id)
        except NotFoundError:
            self.room_manager.set_link_session(room_number, None)
            return
        if (
            LinkSessionStatus(session.status) in MATCHABLE_STATUSES
            and session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID
        ):
            raise ValidationError("room game is active")
        if session.protocol_id == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            return
        self.room_manager.set_link_session(room_number, None)

    def public_room(self, room: Any, viewer_user_id: str = "") -> dict[str, Any]:
        result = room.to_dict()
        result["users"] = [dict(member) for member in result.get("users", [])]
        for member in result.get("users", []):
            member.pop("auth_session_id_digest", None)
        member_ids = {
            str(user.get("user_id", "")) for user in result.get("users", [])
        }
        is_member = viewer_user_id in member_ids
        if is_member:
            result["creator"] = bool(
                result.get("creator_user_id") == viewer_user_id
            )
        else:
            result.pop("room_code", None)
            result.pop("room_code_created_at", None)
            result.pop("creator_user_id", None)
            result.pop("creator_username", None)
            result.pop("creator", None)
        if not (N64_ROOM_FIRST <= room.room_number <= N64_ROOM_LAST):
            return result
        if not is_member:
            return result
        for user_index, user in enumerate(result.get("users", [])):
            bindings = [("slot", False)]
            if user_index == 0:
                bindings.append(("n64_slot", True))
            for field_name, require_n64 in bindings:
                filename_field = f"{field_name}_filename"
                game_type_field = f"{field_name}_game_type"
                platform_field = f"{field_name}_platform"
                header_field = f"{field_name}_rom_header_title"
                user[filename_field] = ""
                user[game_type_field] = ""
                user[platform_field] = ""
                user[header_field] = ""
                slot_name = str(user.get(field_name, ""))
                if not slot_name:
                    continue
                try:
                    slot = self.require_room_rom_slot(
                        str(user.get("user_id", "")),
                        slot_name,
                        require_n64=require_n64,
                    )
                except LeagueError:
                    continue
                user[filename_field] = str(slot.filename or "")
                user[game_type_field] = str(slot.game_type or "")
                user[platform_field] = str(rom_platform_from_filename(slot.filename) or "")
                user[header_field] = str(slot.rom_header_title or "")
        return result

    def validate_n64_room_state(
        self,
        room_number: int,
        user_id: str,
        slot_name: str | None,
        n64_slot_name: str | None,
        ready: bool | None,
    ) -> None:
        if room_number < N64_ROOM_FIRST or room_number > N64_ROOM_LAST:
            if n64_slot_name is not None:
                raise ValidationError("n64_slot is only valid in N64 rooms")
            return
        room = self.room_manager.room(room_number)
        users = [dict(item) for item in room.users]
        user_index = next((index for index, item in enumerate(users) if item.get("user_id") == user_id), None)
        if user_index is None:
            raise ValidationError("room not joined")
        if slot_name is not None:
            self.require_room_rom_slot(user_id, slot_name, require_n64=False)
            users[user_index]["slot"] = slot_name
        if n64_slot_name is not None:
            if user_index != 0 and n64_slot_name.strip():
                raise ValidationError("USER2 cannot select an N64 ROM")
            if n64_slot_name.strip():
                self.require_room_rom_slot(user_id, n64_slot_name, require_n64=True)
            users[user_index]["n64_slot"] = n64_slot_name
        if ready:
            selected = users[user_index]
            if not selected.get("slot"):
                raise ValidationError("GB ROM slot is required")
            self.require_room_rom_slot(user_id, str(selected["slot"]), require_n64=False)
            if user_index == 0:
                if not selected.get("n64_slot"):
                    raise ValidationError("USER1 N64 ROM slot is required")
                self.require_room_rom_slot(user_id, str(selected["n64_slot"]), require_n64=True)
            elif selected.get("n64_slot"):
                raise ValidationError("USER2 cannot select an N64 ROM")
        if len(users) == 2 and users[0].get("slot") and users[1].get("slot"):
            self.require_room_rom_slot(str(users[0]["user_id"]), str(users[0]["slot"]), require_n64=False)
            remote_gb = self.require_room_rom_slot(
                str(users[1]["user_id"]), str(users[1]["slot"]), require_n64=False
            )
            self.require_n64_host_remote_rom(str(users[0]["user_id"]), remote_gb)

    def validate_n64_room_selection(self, room: Any):
        if len(room.users) != 2:
            raise ValidationError("N64 room needs two users")
        host, remote = room.users
        if not host.get("slot") or not host.get("n64_slot") or not remote.get("slot"):
            raise ValidationError("N64 room ROM selections are incomplete")
        if remote.get("n64_slot"):
            raise ValidationError("USER2 cannot select an N64 ROM")
        host_n64 = self.require_room_rom_slot(str(host["user_id"]), str(host["n64_slot"]), require_n64=True)
        host_gb = self.require_room_rom_slot(str(host["user_id"]), str(host["slot"]), require_n64=False)
        remote_gb = self.require_room_rom_slot(str(remote["user_id"]), str(remote["slot"]), require_n64=False)
        self.require_n64_host_remote_rom(str(host["user_id"]), remote_gb)
        return host_n64, host_gb, remote_gb

    def require_n64_host_remote_rom(self, host_user_id: str, remote_slot):
        for slot in sorted(self.rom_slots.list_for_user(host_user_id), key=lambda item: item.slot):
            if (
                slot.rom_id
                and slot.save_id
                and slot.game_type == remote_slot.game_type
                and slot.rom_header_title == remote_slot.rom_header_title
                and rom_platform_from_filename(slot.filename)
                == rom_platform_from_filename(remote_slot.filename)
            ):
                return slot
        raise ValidationError(
            "USER1 must register a matching USER2 ROM in ROM1-ROM8"
        )

    def require_room_rom_slot(self, user_id: str, slot_name: str, require_n64: bool):
        slot_number = self._parse_room_slot(slot_name)
        slot = next((item for item in self.rom_slots.list_for_user(user_id) if item.slot == slot_number), None)
        if not slot or not slot.rom_id or not slot.save_id:
            raise ValidationError("selected ROM slot is not registered")
        platform = rom_platform_from_filename(slot.filename)
        if require_n64 and platform != "n64":
            raise ValidationError("selected ROM slot must be an N64 ROM")
        if not require_n64 and platform != "gb":
            raise ValidationError("selected ROM slot must be a GB/GBC ROM")
        save = self.saves.get_save(slot.save_id, user_id)
        if save.game_type != slot.game_type:
            raise ValidationError("selected ROM and SAV game_type do not match")
        return slot

    def end_room_game_for_session(self, session_id: str, reason: str) -> Any:
        room = self.room_manager.end_game_for_link_session(session_id)
        self.cancel_room_game_for_session(session_id, reason)
        return room

    def cancel_room_game_for_session(self, session_id: str, reason: str) -> None:
        try:
            session = self.sessions.get_session(session_id)
        except NotFoundError:
            return
        if session.status not in ROOM_LEAVE_CANCEL_STATUSES:
            return
        if session.host_process_id:
            try:
                self.host_processes.terminate(session.host_process_id)
            except LeagueError:
                pass
        try:
            self.sync_link_session_saves(session_id, suppress_errors=False)
        except LeagueError:
            if session.host_process_id:
                return
        current = self.sessions.get_session(session_id)
        if current.status in ROOM_LEAVE_CANCEL_STATUSES:
            self.abort_gb_runtime_fixed_host_control_for_session(current, reason)
            self.sessions.transition(session_id, LinkSessionStatus.CANCELLED, reason=reason)

    def abort_gb_runtime_fixed_host_control_for_session(
        self,
        session,
        reason: str,
        *,
        environment: ServerEnvironment | None = None,
    ) -> None:
        if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            return
        authority = (environment or self.active_environment).gb_runtime_fixed_host_sessions
        try:
            authority.cancel(session.id, reason)
        except NotFoundError:
            return

    def cancel_room_game_for_session_if_owned(self, session_id: str, user_id: str, auth_session_id: str, reason: str) -> None:
        if self.auth_session_can_cancel_room_game(session_id, user_id, auth_session_id):
            self.cancel_room_game_for_session(session_id, reason)

    def sync_room_saves_for_user(self, user_id: str) -> list[dict[str, Any]]:
        results = []
        for session_id in self.room_manager.link_session_ids_for_user(user_id):
            result = self.sync_link_session_saves(session_id)
            if result:
                results.append(result)
        return results

    def renew_room_session_locks_for_user(self, user_id: str, auth_session_id: str) -> list[dict[str, Any]]:
        renewed = []
        expires_at = self.game_session_lease_expires_at()
        active_statuses = {status.value for status in LEASE_RENEWABLE_STATUSES}
        for session_id in self.room_manager.link_session_ids_for_user(user_id):
            try:
                session = self.sessions.get_session(session_id)
                if user_id not in {session.player_a_user_id, session.player_b_user_id}:
                    continue
                if self.reconcile_aborted_gb_runtime_fixed_host_session(session):
                    continue
                if session.status not in active_statuses:
                    continue
                self.sessions.renew_game_session_lock(session_id, user_id, expires_at, auth_session_id=auth_session_id)
                session = self.sessions.renew_player_save_locks(session_id, user_id, expires_at)
            except LeagueError:
                continue
            renewed.append({"link_session_id": session_id, "lock_expires_at": expires_at})
        return renewed

    def reconcile_aborted_gb_runtime_fixed_host_session(self, session) -> bool:
        """Release a ROOM whose fixed-Host media/control plane already aborted."""
        if session.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            return False
        try:
            control = self.gb_runtime_fixed_host_sessions.get(session.id)
        except NotFoundError:
            return False
        if control.state != "ABORTED":
            return False
        reason = control.termination_reason or "fixed Host transport ended"
        current = self.sessions.get_session(session.id)
        if current.status in ROOM_LEAVE_CANCEL_STATUSES:
            self.sessions.transition(
                session.id, LinkSessionStatus.CANCELLED, reason=reason
            )
        self.room_manager.end_game_for_link_session(session.id)
        return True

    def renew_n64_runtime_media_locks_for_user(self, user_id: str, auth_session_id: str) -> list[dict[str, Any]]:
        renewed = []
        expires_at = self.game_session_lease_expires_at()
        for media_session in self.n64_runtime_media_sessions.active_for_user(user_id):
            room = self.room_manager.room(media_session.room_number)
            if len(room.users) != 2 or not all(item.get("ready") for item in room.users):
                continue
            active = self.sessions.active_game_session_for_user(user_id)
            if not active:
                continue
            lock = active["lock"]
            if lock.execution_mode != "N64_RUNTIME_NOSAVE" or lock.game_run_id != media_session.id:
                continue
            renewed_lock = self.sessions.renew_local_game_session_lock(
                user_id,
                expires_at,
                auth_session_id,
                lock.game_session_id,
                lock.fencing_token,
            )
            media_session = self.n64_runtime_media_sessions.renew(media_session.id)
            renewed.append(
                {
                    "media_session_id": media_session.id,
                    "game_session_id": renewed_lock.game_session_id,
                    "lock_expires_at": expires_at,
                    "media_expires_at": media_session.expires_at,
                    "no_save": True,
                }
            )
        return renewed

    def session_lifecycle_for_user(self, user_id: str) -> dict[str, Any] | None:
        active = self.sessions.active_game_session_for_user(user_id)
        if not active:
            room_notice = self.room_manager.recent_termination_notice(user_id)
            if room_notice:
                return room_notice
            cutoff = datetime.now(timezone.utc) - timedelta(minutes=2)
            link_sessions = sorted(
                self.sessions.list_for_user(user_id),
                key=lambda item: item.updated_at,
                reverse=True,
            )
            for session in link_sessions:
                updated = self._parse_server_time(session.updated_at)
                if updated and updated >= cutoff and session.status in {
                    LinkSessionStatus.FINALIZING.value,
                    LinkSessionStatus.RECOVERING.value,
                    LinkSessionStatus.COMPLETED.value,
                    LinkSessionStatus.FAILED.value,
                    LinkSessionStatus.CANCELLED.value,
                    LinkSessionStatus.EXPIRED.value,
                }:
                    return {
                        "kind": "link",
                        "session_id": session.id,
                        "status": session.status,
                        "expires_at": session.expires_at,
                        "termination_reason": session.termination_reason,
                        "lease_expires_at": None,
                    }
            for media_session in sorted(
                self.n64_runtime_media_sessions.all_sessions(),
                key=lambda item: item.updated_at,
                reverse=True,
            ):
                if user_id not in {media_session.host_user_id, media_session.remote_user_id}:
                    continue
                updated = self._parse_server_time(media_session.updated_at)
                if updated and updated >= cutoff and media_session.status in {"COMPLETED", "CANCELLED", "EXPIRED"}:
                    return {
                        "kind": "media",
                        "session_id": media_session.id,
                        "status": media_session.status,
                        "expires_at": media_session.expires_at,
                        "termination_reason": media_session.termination_reason,
                        "lease_expires_at": None,
                    }
            return None
        lock = active["lock"]
        session = active["link_session"]
        if session:
            return {
                "kind": "link",
                "session_id": session.id,
                "status": session.status,
                "expires_at": session.expires_at,
                "termination_reason": session.termination_reason,
                "lease_expires_at": lock.lease_expires_at,
            }
        for media_session in self.n64_runtime_media_sessions.active_for_user(user_id):
            if media_session.id != lock.game_run_id:
                continue
            return {
                "kind": "media",
                "session_id": media_session.id,
                "status": media_session.status,
                "expires_at": media_session.expires_at,
                "termination_reason": media_session.termination_reason,
                "lease_expires_at": lock.lease_expires_at,
            }
        return None

    @staticmethod
    def _parse_server_time(value: str | None) -> datetime | None:
        if not value:
            return None
        try:
            parsed = datetime.fromisoformat(value)
        except ValueError:
            return None
        return parsed.replace(tzinfo=parsed.tzinfo or timezone.utc)

    def require_n64_runtime_media_participant_locks(
        self,
        media_session,
        requester_auth_session_id: str,
    ) -> None:
        expected = {
            media_session.host_user_id: {
                media_session.host_n64_save_id,
                media_session.host_save_id,
            },
            media_session.remote_user_id: {media_session.remote_save_id},
        }
        for user_id, expected_save_ids in expected.items():
            active = self.sessions.active_game_session_for_user(user_id)
            if not active:
                raise ValidationError("N64 Runtime participants are not reserved")
            lock = active["lock"]
            bound_save_ids = {
                str(binding.get("save_id", ""))
                for binding in (lock.save_bindings or [])
            }
            if (
                lock.execution_mode != "N64_RUNTIME_NOSAVE"
                or lock.game_run_id != media_session.id
                or bound_save_ids != expected_save_ids
                or (
                    user_id == media_session.host_user_id
                    and lock.auth_session_id != requester_auth_session_id
                )
            ):
                raise ValidationError("N64 Runtime participants are not reserved")

    def sync_link_session_saves(self, session_id: str, suppress_errors: bool = True) -> dict[str, Any] | None:
        try:
            session = self.sessions.get_session(session_id)
            if session.host_process_id:
                self.host_processes.poll(session.host_process_id)
            if session.protocol_id == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
                control = self.reconcile_gb_runtime_fixed_host_trade(session_id)
                if control.state != "FINISHED" or control.commit_result is None:
                    return None
                result = dict(control.commit_result)
            else:
                result = self.sessions.commit_changed_staged_saves(session_id)
        except LeagueError:
            if not suppress_errors:
                raise
            return None
        if not result.get("changed"):
            return None
        return {"link_session_id": session_id, **result}

    def recover_startup_state(self) -> list[dict[str, Any]]:
        recovered: list[dict[str, Any]] = []
        for env in self.environments.values():
            for session in env.sessions.recover_orphaned_sessions(reason="startup recovery"):
                env.room_manager.end_game_for_link_session(session.id)
                recovered.append({"server_id": env.id, "link_session_id": session.id, "status": session.status})
            for media_session in env.n64_runtime_media_sessions.active_sessions():
                room = env.room_manager.room(media_session.room_number)
                expected_users = [media_session.host_user_id, media_session.remote_user_id]
                actual_users = [str(user.get("user_id", "")) for user in room.users]
                if (
                    actual_users == expected_users
                    and len(room.users) == 2
                    and all(bool(user.get("ready")) for user in room.users)
                ):
                    continue
                env.n64_runtime_media_sessions.cancel(media_session.id)
                env.sessions.release_media_game_session_locks(
                    media_session.id,
                    {media_session.host_user_id, media_session.remote_user_id},
                )
                recovered.append(
                    {
                        "server_id": env.id,
                        "media_session_id": media_session.id,
                        "status": "CANCELLED",
                    }
                )
        if recovered:
            self.write_server_log("WARN", f"recovered {len(recovered)} orphaned session(s) on startup")
        return recovered

    def reconcile_session_lifecycle(self, now: datetime | None = None) -> list[dict[str, Any]]:
        """Apply all authoritative room deadlines idempotently."""
        started = time.perf_counter()
        current_time = now or datetime.now(timezone.utc)
        actions: list[dict[str, Any]] = []
        self.lifecycle_metrics["sweeps"] += 1
        pre_running = {
            LinkSessionStatus.CREATED.value,
            LinkSessionStatus.WAITING_PLAYER_A.value,
            LinkSessionStatus.WAITING_PLAYER_B.value,
            LinkSessionStatus.PREPARING.value,
        }
        primary_environment = self.environments[self.default_server_id]
        for expired_lock in primary_environment.sessions.prune_expired_game_session_locks(
            current_time
        ):
            actions.append(
                {
                    "server_id": expired_lock.server_id or self.default_server_id,
                    "game_session_status": "EXPIRED",
                    "reason": "participant_lease_expired",
                }
            )
        for env in self.environments.values():
            for mobile_session in env.mobile_sessions.expire_due(current_time):
                self.lifecycle_metrics["mobile_terminations"] += 1
                action = {
                    "server_id": env.id,
                    "mobile_session_id": mobile_session.id,
                    "status": mobile_session.status,
                }
                if mobile_session.status == "COMPLETED":
                    action["completion_source"] = mobile_session.completion_source
                elif mobile_session.status == "EXPIRED":
                    action["reason"] = "lease_expired"
                actions.append(action)
            idle_actions = env.room_manager.prune_idle_rooms(self.room_session_policy, current_time)
            for idle_action in idle_actions:
                actions.append({"server_id": env.id, **idle_action})
                self.lifecycle_metrics["room_terminations"] += 1
            now_ms = int(current_time.timestamp() * 1000)
            link_rows = self.session_authority.list_due(
                "link", server_id=env.id, active_states=pre_running | {
                    LinkSessionStatus.RUNNING.value,
                    LinkSessionStatus.FINALIZING.value,
                    LinkSessionStatus.RECOVERING.value,
                }, now_ms=now_ms,
                include_missing_game_locks=True, limit=64,
            )
            for link_row in link_rows:
                session = env.sessions.get_session(str(link_row["session_id"]))
                status = session.status
                if status == LinkSessionStatus.FINALIZING.value:
                    reconciled = env.sessions.reconcile_finalization(session.id)
                    if reconciled.status == LinkSessionStatus.EXPIRED.value:
                        self.abort_gb_runtime_fixed_host_control_for_session(
                            reconciled,
                            "trade result deadline expired",
                            environment=env,
                        )
                        env.room_manager.end_game_for_link_session(session.id)
                        actions.append({"server_id": env.id, "link_session_id": session.id, "reason": "trade_result_deadline"})
                    continue
                if status == LinkSessionStatus.RECOVERING.value:
                    self.abort_gb_runtime_fixed_host_control_for_session(
                        session,
                        session.termination_reason or "link session recovering",
                        environment=env,
                    )
                    env.room_manager.end_game_for_link_session(session.id)
                    continue
                if status not in pre_running | {LinkSessionStatus.RUNNING.value}:
                    continue

                reason: str | None = None
                if status == LinkSessionStatus.RUNNING.value and env.sessions.deadline_reached(
                    session.expires_at, current_time
                ):
                    reason = "session_expired"
                if reason is None and env.sessions.participant_leases_expired(session, current_time):
                    reason = "participant_lease_expired"
                if reason is None:
                    continue

                if status == LinkSessionStatus.RUNNING.value and session.requested_link_mode == "trade":
                    ended = env.sessions.transition(
                        session.id, LinkSessionStatus.FINALIZING, reason=reason
                    )
                else:
                    terminal = (
                        LinkSessionStatus.CANCELLED
                        if status in pre_running
                        else LinkSessionStatus.EXPIRED
                    )
                    ended = env.sessions.transition(session.id, terminal, reason=reason)
                    self.abort_gb_runtime_fixed_host_control_for_session(
                        ended,
                        reason,
                        environment=env,
                    )
                env.room_manager.end_game_for_link_session(session.id)
                self.lifecycle_metrics["link_terminations"] += 1
                actions.append(
                    {
                        "server_id": env.id,
                        "link_session_id": session.id,
                        "status": ended.status,
                        "reason": reason,
                    }
                )
            media_rows = self.session_authority.list_due(
                "n64", server_id=env.id,
                active_states=ACTIVE_MEDIA_STATUSES,
                now_ms=now_ms,
                include_missing_game_locks=True, limit=64,
            )
            due_ids = {str(row["session_id"]) for row in media_rows}
            for media in env.n64_runtime_media_sessions.all_sessions():
                if (media.id not in due_ids and media.recovery_deadline
                        and self._deadline_reached(media.recovery_deadline, current_time)
                        and (media.status in ACTIVE_MEDIA_STATUSES or media.status == "EXPIRED")):
                    media_rows.append({"session_id": media.id})
            for media_row in media_rows:
                media_session = env.n64_runtime_media_sessions.get(str(media_row["session_id"]))
                if media_session.status not in ACTIVE_MEDIA_STATUSES and media_session.status != "EXPIRED":
                    continue
                reason = None
                both_tickets_used = bool(
                    media_session.host_ticket_used and media_session.remote_ticket_used
                )
                if self._deadline_reached(media_session.recovery_deadline, current_time):
                    reason = "recovery_timeout"
                elif self._deadline_reached(media_session.expires_at, current_time):
                    reason = "session_expired"
                elif both_tickets_used and env.sessions.media_participant_leases_expired(
                    media_session.id,
                    {media_session.host_user_id, media_session.remote_user_id},
                    current_time,
                ):
                    reason = "participant_lease_expired"
                if reason is None:
                    continue
                env.n64_runtime_media_sessions.expire(media_session.id, reason)
                self.lifecycle_metrics["media_terminations"] += 1
                actions.append(
                    {
                        "server_id": env.id,
                        "media_session_id": media_session.id,
                        "status": "EXPIRED",
                        "reason": reason,
                    }
                )
            with self.use_environment(env):
                self.prune_stale_room_users()
        duration_ms = (time.perf_counter() - started) * 1000.0
        self.lifecycle_metrics["last_reconcile_at"] = datetime.now(timezone.utc).isoformat()
        self.lifecycle_metrics["last_processed_count"] = len(actions)
        self.lifecycle_metrics["last_duration_ms"] = round(duration_ms, 3)
        self.lifecycle_metrics["max_duration_ms"] = round(
            max(float(self.lifecycle_metrics["max_duration_ms"]), duration_ms), 3
        )
        return actions

    @staticmethod
    def _deadline_reached(value: str | None, now: datetime) -> bool:
        if not value:
            return False
        try:
            deadline = datetime.fromisoformat(value)
        except ValueError:
            return True
        if deadline.tzinfo is None:
            deadline = deadline.replace(tzinfo=timezone.utc)
        return deadline <= now

    def reconcile_gb_runtime_fixed_host_trade(self, session_id: str):
        """Finish an acknowledged Trade pair, including restart recovery."""
        control = self.gb_runtime_fixed_host_sessions.get(session_id)
        manifest = control.manifest_object()
        if manifest.save_policy != "commit_pair" or not self.gb_runtime_fixed_host_sessions.ready_to_commit(session_id):
            return control
        session = self.sessions.get_session(session_id)
        if session.status == LinkSessionStatus.RUNNING.value:
            session = self.sessions.transition(
                session_id, LinkSessionStatus.FINALIZING, reason="fixed Host terminal receipts matched"
            )
        if session.status == LinkSessionStatus.COMPLETED.value:
            result = self.sessions.completed_staged_save_commit_result(session_id)
            if result is None:
                raise ValidationError("fixed Host completed session has no pair commit journal")
        elif session.status == LinkSessionStatus.FINALIZING.value:
            result = self.sessions.commit_changed_staged_saves(
                session_id, durable_final=True
            )
        else:
            raise ValidationError("fixed Host Trade cannot be reconciled in this state")
        control = self.gb_runtime_fixed_host_sessions.complete(session_id, result)
        if session.status != LinkSessionStatus.COMPLETED.value:
            self.sessions.transition(session_id, LinkSessionStatus.COMPLETED)
            self.room_manager.end_game_for_link_session(session_id)
        return control

    def start_room(
        self,
        room_number: int,
        requester_user_id: str,
        auth_session_id: str = "",
        link_mode: str | None = None,
        expected_media_session_id: str = "",
    ) -> dict[str, Any]:
        if expected_media_session_id:
            previous = self.n64_runtime_media_sessions.get(expected_media_session_id)
            role = self.n64_runtime_media_sessions.role_for_user(previous, requester_user_id)
            if previous.room_number != room_number:
                raise ValidationError("N64 media session room mismatch")
            if previous.status not in ACTIVE_MEDIA_STATUSES:
                return {"media_session": previous.to_public_dict(role), "room": None}
        room = self.room_manager.room(room_number)
        if N64_ROOM_FIRST <= room_number <= N64_ROOM_LAST:
            if len(room.users) != 2 or not all(user.get("ready") for user in room.users):
                raise ValidationError("N64 room is not ready")
            host_n64, host_gb, remote_gb = self.validate_n64_room_selection(room)
            media_session = self.n64_runtime_media_sessions.create_or_get(
                expected_session_id=expected_media_session_id,
                room_number=room_number,
                host_user_id=str(room.users[0]["user_id"]),
                remote_user_id=str(room.users[1]["user_id"]),
                host_n64_slot=str(room.users[0]["n64_slot"]),
                host_gb_slot=str(room.users[0]["slot"]),
                remote_gb_slot=str(room.users[1]["slot"]),
                host_n64_rom_id=str(host_n64.rom_id),
                host_gb_rom_id=str(host_gb.rom_id),
                remote_gb_rom_id=str(remote_gb.rom_id),
                host_n64_save_id=str(host_n64.save_id),
                host_save_id=str(host_gb.save_id),
                remote_save_id=str(remote_gb.save_id),
                game_type=str(host_gb.game_type),
            )
            role = self.n64_runtime_media_sessions.role_for_user(media_session, requester_user_id)
            participant_saves = (
                [
                    self.saves.get_save(media_session.host_n64_save_id, media_session.host_user_id),
                    self.saves.get_save(media_session.host_save_id, media_session.host_user_id),
                ]
                if role == "host"
                else [self.saves.get_save(media_session.remote_save_id, media_session.remote_user_id)]
            )
            game_lock = self.sessions.acquire_media_game_session_lock(
                requester_user_id,
                media_session.id,
                self.game_session_lease_expires_at(),
                auth_session_id,
                participant_saves,
                expires_at=media_session.expires_at,
            )
            media_session, role, ticket = self.n64_runtime_media_sessions.issue_ticket(media_session.id, requester_user_id)
            room = self.storage.mark_n64_room_started(media_session.id)
            return {
                "room": self.public_room(room, viewer_user_id=requester_user_id),
                "media_session": media_session.to_public_dict(role),
                "game_session": self.public_game_session_lock(game_lock, owner=True),
                "connection": {
                    "relay_host": self.n64_runtime_media_relay_host,
                    "relay_port": self.n64_runtime_media_relay_port,
                    "relay_transport": self.n64_runtime_media_relay_transport,
                    "role": role,
                    "scope": media_session.ticket_scope,
                    "ticket": ticket,
                },
            }
        if room.link_session_id:
            try:
                existing_room_session = self.sessions.get_session(room.link_session_id)
            except NotFoundError:
                room = self.room_manager.set_link_session(room_number, None)
            else:
                if existing_room_session.status in {
                    LinkSessionStatus.CREATED.value,
                    LinkSessionStatus.WAITING_PLAYER_A.value,
                    LinkSessionStatus.WAITING_PLAYER_B.value,
                    LinkSessionStatus.PREPARING.value,
                    LinkSessionStatus.RUNNING.value,
                }:
                    return {
                        "room": self.public_room(
                            room, viewer_user_id=requester_user_id
                        ),
                        "link_session": existing_room_session.to_dict(),
                    }
                room = self.room_manager.set_link_session(room_number, None)
        if len(room.users) != 2:
            raise ValidationError("room needs two users")
        if not any(user["user_id"] == requester_user_id for user in room.users):
            raise ValidationError("room not joined")
        if not all(user.get("ready") and user.get("slot") for user in room.users):
            raise ValidationError("room is not ready")
        selected_link_mode = normalize_link_mode(
            link_mode if link_mode is not None else room.link_mode
        )
        if selected_link_mode != normalize_link_mode(room.link_mode):
            raise ValidationError("room link mode changed; refresh the room")
        saves = []
        selected_slots = []
        for user in room.users:
            slot_number = self._parse_room_slot(str(user.get("slot", "")))
            slots = self.rom_slots.list_for_user(str(user["user_id"]))
            slot = next((item for item in slots if item.slot == slot_number), None)
            if not slot or not slot.save_id:
                raise ValidationError("selected ROM slot is not registered")
            if rom_platform_from_filename(slot.filename) != "gb":
                raise ValidationError("ROOM play requires a GB/GBC ROM slot")
            save = self.saves.get_save(slot.save_id, str(user["user_id"]))
            if save.game_type != slot.game_type:
                raise ValidationError("selected ROM and SAV game_type do not match")
            selected_slots.append(slot)
            saves.append(slot.save_id)
        self.validate_unique_save_ids(saves)
        participant_user_ids = [str(user["user_id"]) for user in room.users]
        selected_protocol = GB_RUNTIME_FIXED_HOST_PROTOCOL_ID
        if selected_protocol == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            self.require_gb_runtime_fixed_host_rom_availability(
                participant_user_ids[0],
                selected_slots,
            )

        lock_expires_at = self.game_session_lease_expires_at()
        existing_session = self.sessions.find_active_for_match(
            str(room.users[0]["user_id"]),
            str(room.users[1]["user_id"]),
            saves[0],
            saves[1],
            selected_link_mode,
            selected_protocol,
        )
        if existing_session:
            room = self.room_manager.set_link_session(room_number, existing_session.id)
            return {
                "room": self.public_room(room, viewer_user_id=requester_user_id),
                "link_session": existing_session.to_dict(),
            }
        participant_auth_sessions = {
            str(member.get("user_id")): str(member.get("auth_session_id_digest") or "")
            for member in room.users
            if member.get("auth_session_id_digest")
        }
        if auth_session_id:
            participant_auth_sessions[requester_user_id] = auth_session_id
        if set(participant_auth_sessions) != set(participant_user_ids):
            raise ValidationError("all ROOM members must refresh before game start")
        session = self.sessions.create_session(
            str(room.users[0]["user_id"]),
            str(room.users[1]["user_id"]),
            saves[0],
            saves[1],
            lock_expires_at,
            room_number=room_number,
            base_port=self.room_base_port(room_number),
            link_mode=selected_link_mode,
            protocol_id=selected_protocol,
            auth_session_ids=participant_auth_sessions,
        )
        if selected_protocol == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            save_policy = "discard" if selected_link_mode == "battle" else "commit_pair"
            try:
                gb_runtime_fixed_host_session = self.gb_runtime_fixed_host_sessions.create(
                    room_number,
                    GBRuntimeFixedHostManifest(
                        session_id=session.id,
                        session_epoch=1,
                        host_user_id=session.player_a_user_id,
                        remote_user_id=session.player_b_user_id,
                        host_save_id=session.save_a_id,
                        remote_save_id=session.save_b_id,
                        host_game_type=str(selected_slots[0].game_type),
                        remote_game_type=str(selected_slots[1].game_type),
                        host_platform=str(rom_platform_from_filename(selected_slots[0].filename)),
                        remote_platform=str(rom_platform_from_filename(selected_slots[1].filename)),
                        host_rom_header_title=str(selected_slots[0].rom_header_title),
                        remote_rom_header_title=str(selected_slots[1].rom_header_title),
                        host_base_revision=int(session.player_a_base_revision),
                        remote_base_revision=int(session.player_b_base_revision),
                        requested_mode=selected_link_mode,
                        save_policy=save_policy,
                        runtime_build_id=self.gb_runtime_fixed_host_runtime_build_id,
                    ),
                )
            except Exception:
                self.sessions.transition(
                    session.id,
                    LinkSessionStatus.CANCELLED,
                    reason="fixed Host manifest bootstrap failed",
                )
                raise
        room = self.room_manager.set_link_session(room_number, session.id)
        response = {
            "room": self.public_room(room, viewer_user_id=requester_user_id),
            "link_session": session.to_dict(),
        }
        if selected_protocol == GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            response["gb_runtime_fixed_host_session"] = gb_runtime_fixed_host_session.to_public_dict(
                requester_user_id
            )
        return response

    def public_game_session_lock(self, lock, owner: bool) -> dict[str, Any]:
        game_session = {
            "game_run_id": lock.game_run_id,
            "link_session_id": lock.link_session_id,
            "server_id": lock.server_id,
            "execution_mode": lock.execution_mode,
            "fencing_token": lock.fencing_token if owner else None,
            "started_at": lock.started_at,
            "last_heartbeat": lock.last_heartbeat,
            "lease_expires_at": lock.lease_expires_at,
            "expires_at": lock.expires_at,
            "is_owner_auth_session": owner,
            "save_bindings": lock.save_bindings if owner else [],
        }
        if owner:
            game_session["game_session_id"] = lock.game_session_id
        return game_session

    def validate_unique_save_ids(self, save_ids: list[str]) -> None:
        normalized = [save_id.strip() for save_id in save_ids]
        if any(not save_id for save_id in normalized):
            raise ValidationError("save_id is required")
        if len(set(normalized)) != len(normalized):
            raise ValidationError("save_ids must be unique")

    def require_gb_gbc_save(self, save: SaveRecord, context: str) -> None:
        slot = self._slot_for_save(save)
        if slot is not None and rom_platform_from_filename(slot.filename) != "gb":
            raise ValidationError(f"{context} requires GB/GBC saves")

    def require_n64_save(self, save: SaveRecord, context: str) -> None:
        slot = self._slot_for_save(save)
        if slot is None or rom_platform_from_filename(slot.filename) != "n64":
            raise ValidationError(f"{context} requires an N64 ROM save")

    def _slot_for_save(self, save: SaveRecord):
        for slot in self.rom_slots.list_for_user(save.user_id):
            if slot.save_id == save.id:
                if slot.game_type != save.game_type:
                    raise ValidationError("ROM and SAV game_type do not match")
                return slot
        return None

    def validate_n64_runtime_saves(self, saves: list[SaveRecord]) -> None:
        if not 1 <= len(saves) <= 5:
            raise ValidationError("N64 Runtime requires one N64 save and up to four Transfer Pak saves")
        self.require_n64_save(saves[0], "N64 Runtime")
        for save in saves[1:]:
            self.require_gb_gbc_save(save, "N64 Runtime Transfer Pak")

    def game_status_for_user(self, user_id: str, auth_session_id: str) -> dict[str, Any]:
        active = self.sessions.active_game_session_for_user(user_id)
        if not active:
            return {"active": False, "game_session": None, "link_session": None}
        lock = active["lock"]
        session = active["link_session"]
        if session:
            session = self.sessions.reconcile_finalization(session.id)
        owner = bool(lock.auth_session_id and lock.auth_session_id == auth_session_id)
        game_session = self.public_game_session_lock(lock, owner)
        return {"active": True, "game_session": game_session, "link_session": session.to_dict() if session else None}

    def room_base_port(self, room_number: int) -> int:
        if room_number < LINK_ROOM_FIRST or room_number > LINK_ROOM_LAST:
            raise ValidationError("Link Cable room must be 1 to 64")
        return GSC_ROOM_BASE_PORT + (room_number - 1) * GSC_ROOM_PORTS_PER_ROOM

        return int(body.get("port", GSC_ROOM_BASE_PORT))

    def require_session_member(self, session_id: str, user_id: str):
        session = self.sessions.get_session(session_id)
        if user_id not in {session.player_a_user_id, session.player_b_user_id}:
            raise ValidationError("link session access denied")
        return session

    def require_gb_runtime_fixed_host_rom_availability(
        self, host_user_id: str, required_slots: list
    ) -> None:
        required = {
            (
                str(slot.game_type),
                str(rom_platform_from_filename(slot.filename)),
                str(slot.rom_header_title),
            )
            for slot in required_slots
        }
        available = {
            (
                str(slot.game_type),
                str(rom_platform_from_filename(slot.filename)),
                str(slot.rom_header_title),
            )
            for slot in self.rom_slots.list_for_user(host_user_id)
            if slot.rom_id and slot.game_type and slot.rom_header_title
        }
        missing = sorted(required - available)
        if missing:
            raise ValidationError(
                "fixed Host User1 is missing required ROM metadata"
            )

    def _parse_room_slot(self, value: str) -> int:
        normalized = value.strip().upper()
        if not normalized.startswith("ROM"):
            raise ValidationError("selected ROM slot is invalid")
        try:
            slot_number = int(normalized[3:])
        except ValueError as error:
            raise ValidationError("selected ROM slot is invalid") from error
        if slot_number < 1 or slot_number > 8:
            raise ValidationError("selected ROM slot is invalid")
        return slot_number

    def issue_admin_user(self, username: str, email: str) -> dict[str, Any]:
        issued = issue_user(self.auth, username, email=email)
        return {
            "user": public_user(issued.user.to_dict()),
            "initial_password": issued.initial_password,
        }

    def reset_admin_password(self, username: str) -> dict[str, Any]:
        reset = reset_user_password(self.auth, username)
        return {
            "user": public_user(reset.user.to_dict()),
            "initial_password": reset.temporary_password,
        }

    def admin_user_summaries(self) -> list[dict[str, Any]]:
        users = [public_user(user.to_dict()) for user in self.auth.list_users()]

        summaries = []
        for user in sorted(users, key=lambda item: item["username"]):
            user_id = user["id"]
            environments = []
            for env in self.environments.values():
                user_roms = [item.to_dict() for item in env.roms.list_for_user(user_id)]
                user_saves = [item.to_dict() for item in env.saves.list_for_user(user_id)]
                user_slots = [
                    item.to_dict() for item in env.rom_slots.list_for_user(user_id)
                    if item.rom_id
                ]
                user_sessions = [
                    item.to_dict() for item in env.sessions.list_for_user(user_id)
                ]
                environments.append(
                    {
                        "server": self.public_environment(env),
                        "counts": {
                            "rom_registrations": len(user_roms),
                            "saves": len(user_saves),
                            "rom_slots": len([slot for slot in user_slots if slot.get("rom_id")]),
                            "link_sessions": len(user_sessions),
                        },
                        "rom_slots": sorted(user_slots, key=lambda item: int(item.get("slot", 0))),
                        "saves": sorted(user_saves, key=lambda item: (item.get("game_type", ""), item.get("created_at", ""))),
                        "rom_registrations": sorted(user_roms, key=lambda item: item.get("created_at", "")),
                        "link_sessions": sorted(user_sessions, key=lambda item: item.get("created_at", "")),
                    }
                )
            default_summary = next(item for item in environments if item["server"]["id"] == self.default_server_id)
            summaries.append(
                {
                    "user": user,
                    "server": default_summary["server"],
                    "counts": default_summary["counts"],
                    "rom_slots": default_summary["rom_slots"],
                    "saves": default_summary["saves"],
                    "rom_registrations": default_summary["rom_registrations"],
                    "link_sessions": default_summary["link_sessions"],
                    "environments": environments,
                }
            )
        return summaries

    def admin_operations_summary(self) -> dict[str, Any]:
        rooms = [self.admin_room_summary(room.to_dict()) for room in self.room_manager.list_rooms()]
        host_records = [
            self.host_processes.poll(str(record["id"])).to_public_dict()
            for record in self.storage.load_host_processes().values()
            if record.get("id")
        ]
        host_records = sorted(host_records, key=lambda item: item.get("updated_at", ""), reverse=True)
        sessions = [
            self.sessions.get_session(str(session["id"])).to_dict()
            for session in self.storage.load_link_sessions().values()
            if session.get("id")
        ]
        sessions = sorted(sessions, key=lambda item: item.get("updated_at", ""), reverse=True)
        events = sorted(
            self.storage.load_session_events().values(),
            key=lambda item: item.get("created_at", ""),
            reverse=True,
        )[:80]
        return {
            "server": self.public_environment(self.active_environment),
            "lifecycle": self.admin_lifecycle_summary(),
            "performance": self.performance_metrics.snapshot(),
            "rooms": rooms,
            "host_processes": host_records[:40],
            "link_sessions": sessions[:40],
            "events": events,
            "environments": [self.admin_operations_summary_for_environment(env) for env in self.environments.values()],
        }

    def admin_lifecycle_summary(self) -> dict[str, Any]:
        now = datetime.now(timezone.utc)
        now_ms = int(now.timestamp() * 1000)
        with self.authority_database.transaction() as connection:
            active_locks = int(connection.execute(
                "SELECT COUNT(*) FROM game_session_locks WHERE lease_expires_at_ms > ?",
                (now_ms,),
            ).fetchone()[0])
            stale_locks = int(connection.execute(
                "SELECT COUNT(*) FROM game_session_locks WHERE lease_expires_at_ms <= ?",
                (now_ms,),
            ).fetchone()[0])
        link_sessions = [
            session
            for env in self.environments.values()
            for session in env.sessions.all_sessions()
        ]
        media_sessions = [
            session
            for env in self.environments.values()
            for session in env.n64_runtime_media_sessions.all_sessions()
        ]
        gb_runtime_fixed_host_records = [
            record
            for env in self.environments.values()
            for record in env.storage.load_gb_runtime_fixed_host_sessions().values()
        ]
        gb_runtime_fixed_host_states: dict[str, int] = {}
        gb_runtime_fixed_host_remote_disconnects = 0
        for record in gb_runtime_fixed_host_records:
            state = str(record.get("state") or "UNKNOWN")
            gb_runtime_fixed_host_states[state] = gb_runtime_fixed_host_states.get(state, 0) + 1
            gb_runtime_fixed_host_remote_disconnects += int(record.get("remote_disconnect_count") or 0)
        last_sweeper = self._parse_server_time(
            str(self.lifecycle_metrics.get("last_sweeper_success_at") or "")
        )
        return {
            "metrics": dict(self.lifecycle_metrics),
            "sweeper_lag_seconds": (
                max(0, int((now - last_sweeper).total_seconds())) if last_sweeper else None
            ),
            "active_game_locks": active_locks,
            "stale_game_locks": stale_locks,
            "active_link_sessions": sum(
                session.status in ACTIVE_STATUSES for session in link_sessions
            ),
            "recovering_link_sessions": sum(
                session.status == LinkSessionStatus.RECOVERING.value for session in link_sessions
            ),
            "active_media_sessions": sum(
                session.status in {"CREATED", "WAITING_PEER", "READY", "RUNNING"}
                for session in media_sessions
            ),
            "gb_runtime_fixed_host_rollout": {
                "stage": "default",
                "runtime_build_id": self.gb_runtime_fixed_host_runtime_build_id,
                "canary_user_count": 0,
                "room_allowlist_count": 16,
                "opt_in_user_count": 0,
            },
            "gb_runtime_fixed_host_sessions": {
                "total": len(gb_runtime_fixed_host_records),
                "states": gb_runtime_fixed_host_states,
                "active": sum(
                    str(record.get("state") or "") in {
                        "PREFLIGHT", "READY", "WAITING_PEER", "RUNNING",
                        "PAUSED_REMOTE", "FINALIZING",
                    }
                    for record in gb_runtime_fixed_host_records
                ),
                "remote_disconnects": gb_runtime_fixed_host_remote_disconnects,
                "commit_aborts": sum(
                    str(record.get("state") or "") == "ABORTED"
                    and str(record.get("manifest", {}).get("save_policy") or "") == "commit_pair"
                    for record in gb_runtime_fixed_host_records
                ),
            },
            "termination_reasons": {
                session.id: session.termination_reason
                for session in [*link_sessions, *media_sessions]
                if session.termination_reason
            },
        }

    def admin_operations_summary_for_environment(self, env: ServerEnvironment) -> dict[str, Any]:
        with self.use_environment(env):
            rooms = [self.admin_room_summary(room.to_dict()) for room in env.room_manager.list_rooms()]
        host_records = [
            env.host_processes.poll(str(record["id"])).to_public_dict()
            for record in env.storage.load_host_processes().values()
            if record.get("id")
        ]
        sessions = [
            env.sessions.get_session(str(session["id"])).to_dict()
            for session in env.storage.load_link_sessions().values()
            if session.get("id")
        ]
        events = sorted(
            env.storage.load_session_events().values(),
            key=lambda item: item.get("created_at", ""),
            reverse=True,
        )[:80]
        return {
            "server": self.public_environment(env),
            "rooms": rooms,
            "host_processes": sorted(host_records, key=lambda item: item.get("updated_at", ""), reverse=True)[:40],
            "link_sessions": sorted(sessions, key=lambda item: item.get("updated_at", ""), reverse=True)[:40],
            "events": events,
        }

    def write_server_log(self, level: str, message: str) -> None:
        timestamp = datetime.now(timezone.utc).isoformat()
        safe_level = level.upper().replace("\n", " ")[:12]
        safe_message = message.replace("\n", "\\n")
        line = f"{timestamp} {safe_level} {safe_message}\n"
        with self.log_lock:
            with self.log_file.open("a", encoding="utf-8") as handle:
                handle.write(line)

    def admin_logs(self, limit: int = 400) -> dict[str, Any]:
        max_lines = max(1, min(limit, 1000))
        with self.log_lock:
            if not self.log_file.exists():
                return {"log": {"path": str(self.log_file), "lines": [], "line_count": 0, "download_url": self.admin_path("/logs/download")}}
            with self.log_file.open("r", encoding="utf-8", errors="replace") as handle:
                lines = [line.rstrip("\n") for line in deque(handle, maxlen=max_lines)]
        return {
            "log": {
                "path": str(self.log_file),
                "lines": lines,
                "line_count": len(lines),
                "download_url": self.admin_path("/logs/download"),
            }
        }

    def admin_room_summary(self, room: dict[str, Any]) -> dict[str, Any]:
        room = dict(room)
        room["users"] = [dict(user) for user in room.get("users", [])]
        for user in room["users"]:
            user.pop("auth_session_id_digest", None)
        link_session_id = room.get("link_session_id")
        session_status = None
        host_process_id = None
        if link_session_id:
            try:
                session = self.sessions.get_session(str(link_session_id))
            except NotFoundError:
                session_status = "MISSING"
            else:
                session_status = session.status
                host_process_id = session.host_process_id
        room_status = "USED" if room.get("game_started") and not link_session_id else "OPEN"
        if link_session_id:
            room_status = session_status or "SESSION"
        return {
            **room,
            "room_status": room_status,
            "session_status": session_status,
            "host_process_id": host_process_id,
        }

    def require_user(self, bearer_token: str | None):
        token = self.require_bearer_token(bearer_token)
        context = self.request_context
        if context is not None and context.user is not None:
            return context.user
        return self.auth.require_user(token)

    def require_bearer_token(self, bearer_token: str | None) -> str:
        if not bearer_token:
            raise AuthenticationError("missing bearer token")
        return bearer_token

    def require_auth_session_id_digest(self) -> str:
        context = self.request_context
        if context is None or not context.auth_session_id_digest:
            raise AuthenticationError("missing authenticated request context")
        return context.auth_session_id_digest


def create_handler(application: LeagueApplication) -> type[BaseHTTPRequestHandler]:
    class LeagueRequestHandler(BaseHTTPRequestHandler):
        server_version = "IntegralEmulatorHTTP/0.1"

        def setup(self) -> None:
            super().setup()
            self.connection.settimeout(REQUEST_TIMEOUT_SECONDS)

        def do_GET(self) -> None:
            self._dispatch("GET")

        def do_POST(self) -> None:
            self._dispatch("POST")

        def do_PUT(self) -> None:
            self._dispatch("PUT")

        def do_PATCH(self) -> None:
            self._dispatch("PATCH")

        def log_message(self, format: str, *args: Any) -> None:
            return

        def _dispatch(self, method: str) -> None:
            path = urlparse(self.path).path
            try:
                if method == "GET" and path == "/admin":
                    if self._admin_session_token() and self._has_admin_session():
                        self._send_html(
                            HTTPStatus.OK,
                            render_admin_html(application.admin_base_path),
                        )
                    else:
                        self._send_html(
                            HTTPStatus.OK,
                            render_admin_login_html(application.admin_base_path),
                        )
                    application.write_server_log("INFO", f"{self.client_address[0]} GET {application.admin_base_path} 200")
                    return
                segments = [segment for segment in path.split("/") if segment]
                if method == "POST" and segments == ["admin", "session"]:
                    result = self._route(method, segments)
                    token = str(result["admin_session"]["token"])
                    self._send_json(HTTPStatus.OK, {"ok": True}, headers=[("Set-Cookie", self._admin_cookie(token))])
                    application.write_server_log("INFO", f"{self.client_address[0]} POST {application.admin_path('/session')} 200")
                    return
                if method == "POST" and segments == ["admin", "logout"]:
                    application.destroy_admin_session(self._admin_session_token())
                    self._send_json(HTTPStatus.OK, {"logged_out": True}, headers=[("Set-Cookie", self._expired_admin_cookie())])
                    application.write_server_log("INFO", f"{self.client_address[0]} POST {application.admin_path('/logout')} 200")
                    return
                if segments and segments[0] == "admin":
                    application.require_admin_session(self._admin_session_token())
                if method == "GET" and segments == ["admin", "logs", "download"]:
                    self._send_log_download()
                    application.write_server_log("INFO", f"{self.client_address[0]} GET {application.admin_path('/logs/download')} 200")
                    return
                result = self._route(method, segments)
                if isinstance(result, BinaryResponse):
                    self._send_binary(HTTPStatus.OK, result)
                else:
                    self._send_json(
                        HTTPStatus.OK,
                        result,
                        headers=list(self._sensitive_json_headers(path)),
                    )
                application.write_server_log("INFO", f"{self.client_address[0]} {method} {path} 200")
            except DuplicateUserError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 duplicate_user {error}")
                self._send_error(HTTPStatus.CONFLICT, "duplicate_user", str(error))
            except AuthenticationError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 401 authentication_failed {error}")
                self._send_error(HTTPStatus.UNAUTHORIZED, "authentication_failed", str(error))
            except ClientVersionNotAllowedError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 426 client_version_not_allowed")
                self._send_error(HTTPStatus.UPGRADE_REQUIRED, "client_version_not_allowed", str(error))
            except RegistrationDisabledError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 403 registration_disabled")
                self._send_error(HTTPStatus.FORBIDDEN, "registration_disabled", str(error))
            except RevisionConflictError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 revision_conflict {error}")
                self._send_error(HTTPStatus.CONFLICT, "revision_conflict", str(error))
            except SaveLockedError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 save_locked {error}")
                self._send_error(HTTPStatus.CONFLICT, "save_locked", str(error))
            except GameSessionFenceError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 stale_fence {error}")
                self._send_error(HTTPStatus.CONFLICT, "stale_fence", str(error))
            except GameSessionExpiredError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 410 game_session_expired {error}")
                self._send_error(HTTPStatus.GONE, "game_session_expired", str(error))
            except MobileCreateAuthSessionConflictError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 mobile_create_auth_session_conflict")
                self._send_error(HTTPStatus.CONFLICT, "mobile_create_auth_session_conflict", str(error))
            except MobileCreateAbortedError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 mobile_create_aborted")
                self._send_error(HTTPStatus.CONFLICT, "mobile_create_aborted", str(error))
            except InvalidRoomModeError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 400 invalid_room_mode")
                self._send_error(HTTPStatus.BAD_REQUEST, "invalid_room_mode", str(error))
            except RoomPoolFullError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 room_pool_full")
                self._send_error(HTTPStatus.CONFLICT, "room_pool_full", str(error))
            except InvalidRoomCodeError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 400 invalid_room_code")
                self._send_error(HTTPStatus.BAD_REQUEST, "invalid_room_code", str(error))
            except RoomCodeUnavailableError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 404 room_code_unavailable")
                self._send_error(HTTPStatus.NOT_FOUND, "room_code_unavailable", str(error))
            except AlreadyInRoomError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 409 already_in_room")
                self._send_error(HTTPStatus.CONFLICT, "already_in_room", str(error))
            except RoomJoinRateLimitedError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 429 room_join_rate_limited")
                self._send_error(HTTPStatus.TOO_MANY_REQUESTS, "room_join_rate_limited", str(error))
            except RequestBodyTooLarge as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 413 request_body_too_large {error}")
                self._send_error(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "request_body_too_large", str(error))
            except RateLimitExceeded as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 429 rate_limited")
                error_code = (
                    "room_join_rate_limited"
                    if path == "/room-matching/join"
                    else "rate_limited"
                )
                self._send_error(HTTPStatus.TOO_MANY_REQUESTS, error_code, str(error))
            except NotFoundError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 404 not_found {error}")
                self._send_error(HTTPStatus.NOT_FOUND, "not_found", str(error))
            except ValidationError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 400 validation_failed {error}")
                self._send_error(HTTPStatus.BAD_REQUEST, "validation_failed", str(error))
            except LeagueError as error:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 400 league_error {error}")
                self._send_error(HTTPStatus.BAD_REQUEST, "league_error", str(error))
            except json.JSONDecodeError:
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 400 invalid_json")
                self._send_error(HTTPStatus.BAD_REQUEST, "invalid_json", "request body must be JSON")
            except (TimeoutError, socket.timeout):
                application.write_server_log("WARN", f"{self.client_address[0]} {method} {path} 408 request_timeout")
                self._send_error(HTTPStatus.REQUEST_TIMEOUT, "request_timeout", "request timed out")
            except Exception:
                request_id = f"req_{secrets.token_urlsafe(12)}"
                application.write_server_log(
                    "ERROR",
                    f"{self.client_address[0]} {method} {path} 500 internal_error "
                    f"request_id={request_id}\n{traceback.format_exc()}",
                )
                self._send_error(
                    HTTPStatus.INTERNAL_SERVER_ERROR,
                    "internal_error",
                    "an unexpected server error occurred",
                    request_id=request_id,
                )

        def _route(self, method: str, segments: list[str]) -> dict[str, Any] | BinaryResponse:
            path = "/" + "/".join(segments)
            self._metric_route_name = application.route_metric_name(method, path)
            if method == "POST" and path == "/auth/register" and not application.allow_self_registration:
                raise RegistrationDisabledError("self-registration is disabled")
            body = (
                self._read_json(application.json_body_limit(path))
                if method in {"POST", "PUT", "PATCH"}
                else {}
            )
            application.enforce_request_rate_limit(self.client_address[0], path, body)
            bearer_token = self._bearer_token()
            return application.handle_request(
                method,
                path,
                body,
                bearer_token=bearer_token,
            )

        def _bearer_token(self) -> str | None:
            header = self.headers.get("Authorization", "")
            prefix = "Bearer "
            if not header:
                return None
            if not header.startswith(prefix):
                raise AuthenticationError("missing bearer token")
            return header[len(prefix) :]

        def _admin_session_token(self) -> str | None:
            cookies = self.headers.get("Cookie", "")
            for item in cookies.split(";"):
                name, separator, value = item.strip().partition("=")
                if separator and name == "integral_admin_session":
                    return value
            return None

        def _has_admin_session(self) -> bool:
            try:
                application.require_admin_session(self._admin_session_token())
                return True
            except AuthenticationError:
                return False

        def _read_json(self, maximum_bytes: int = MAX_JSON_BODY_BYTES) -> dict[str, Any]:
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError as error:
                raise ValidationError("Content-Length must be an integer") from error
            if length < 0:
                raise ValidationError("Content-Length must not be negative")
            if length > maximum_bytes:
                raise RequestBodyTooLarge(f"request body must be {maximum_bytes} bytes or fewer")
            if length == 0:
                return {}
            read_started = time.perf_counter()
            data = self.rfile.read(length)
            application.performance_metrics.record(
                "request_body_read",
                getattr(self, "_metric_route_name", "UNKNOWN"),
                (time.perf_counter() - read_started) * 1000.0,
                byte_count=len(data),
            )
            if len(data) != length:
                raise ValidationError("request body was incomplete")
            parse_started = time.perf_counter()
            body = json.loads(data.decode("utf-8"))
            application.performance_metrics.record(
                "request_body_parse",
                getattr(self, "_metric_route_name", "UNKNOWN"),
                (time.perf_counter() - parse_started) * 1000.0,
                byte_count=len(data),
            )
            if not isinstance(body, dict):
                raise ValidationError("request body must be a JSON object")
            return body

        def _send_json(self, status: HTTPStatus, payload: dict[str, Any], headers: list[tuple[str, str]] | None = None) -> None:
            route_name = getattr(self, "_metric_route_name", "UNKNOWN")
            serialize_started = time.perf_counter()
            data = json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
            application.performance_metrics.record(
                "response_serialize",
                route_name,
                (time.perf_counter() - serialize_started) * 1000.0,
                byte_count=len(data),
            )
            self.send_response(status.value)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            for name, value in headers or []:
                self.send_header(name, value)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            write_started = time.perf_counter()
            self.wfile.write(data)
            application.performance_metrics.record(
                "response_write",
                route_name,
                (time.perf_counter() - write_started) * 1000.0,
                byte_count=len(data),
            )

        @staticmethod
        def _sensitive_json_headers(path: str) -> tuple[tuple[str, str], ...]:
            if re.fullmatch(r"/gb-runtime-fixed-host-sessions/[^/]+/runtime-snapshots", path):
                return (
                    ("Cache-Control", "no-store"),
                    ("Pragma", "no-cache"),
                    ("X-Content-Type-Options", "nosniff"),
                )
            return ()

        def _send_html(self, status: HTTPStatus, html: str) -> None:
            data = html.encode("utf-8")
            self.send_response(status.value)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _send_binary(self, status: HTTPStatus, response: BinaryResponse) -> None:
            self.send_response(status.value)
            self.send_header("Content-Type", response.content_type)
            for name, value in response.headers:
                self.send_header(name, value)
            self.send_header("Content-Length", str(len(response.data)))
            self.end_headers()
            self.wfile.write(response.data)

        def _send_log_download(self) -> None:
            with application.log_lock:
                data = application.log_file.read_bytes() if application.log_file.exists() else b""
            timestamp = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")
            self.send_response(HTTPStatus.OK.value)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Disposition", f'attachment; filename="integral-emulator-server-{timestamp}.log"')
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _send_error(
            self,
            status: HTTPStatus,
            code: str,
            message: str,
            *,
            request_id: str | None = None,
        ) -> None:
            error = {"code": code, "message": message}
            if request_id:
                error["request_id"] = request_id
            self._send_json(status, {"error": error})

        def _admin_cookie(self, token: str) -> str:
            parts = [
                f"integral_admin_session={token}",
                f"Path={application.public_base_path or '/'}",
                "HttpOnly",
                "SameSite=Strict",
                "Max-Age=43200",
            ]
            if application.admin_cookie_secure:
                parts.append("Secure")
            return "; ".join(parts)

        def _expired_admin_cookie(self) -> str:
            parts = [
                "integral_admin_session=",
                f"Path={application.public_base_path or '/'}",
                "HttpOnly",
                "SameSite=Strict",
                "Max-Age=0",
            ]
            if application.admin_cookie_secure:
                parts.append("Secure")
            return "; ".join(parts)

    return LeagueRequestHandler


def run_server(host: str, port: int, storage_root: Path | str) -> None:
    application = LeagueApplication(storage_root=storage_root)
    application.recover_startup_state()
    server = ThreadingHTTPServer((host, port), create_handler(application))
    stop_sweeper = threading.Event()

    def sweep() -> None:
        while not stop_sweeper.wait(application.room_session_policy.sweeper_interval_seconds):
            try:
                actions = application.reconcile_session_lifecycle()
                application.lifecycle_metrics["last_sweeper_success_at"] = datetime.now(timezone.utc).isoformat()
                application.lifecycle_metrics["last_sweeper_error"] = None
                if actions:
                    application.write_server_log(
                        "WARN", f"session lifecycle sweep applied {len(actions)} action(s)"
                    )
            except Exception as error:  # keep the authority loop alive and observable
                application.lifecycle_metrics["sweeper_errors"] += 1
                application.lifecycle_metrics["last_sweeper_error"] = str(error)[:240]
                application.write_server_log("ERROR", f"session lifecycle sweep failed: {error}")

    sweeper = threading.Thread(target=sweep, name="room-session-sweeper", daemon=True)
    sweeper.start()
    try:
        server.serve_forever()
    finally:
        stop_sweeper.set()
        sweeper.join(timeout=application.room_session_policy.sweeper_interval_seconds + 1)
        server.server_close()


def public_user(data: dict[str, Any]) -> dict[str, Any]:
    safe = data.copy()
    safe.pop("password_hash", None)
    safe.pop("username_normalized", None)
    return safe


def encode_bytes(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def decode_base64_field(body: dict[str, Any], field: str) -> bytes:
    value = body.get(field)
    if not isinstance(value, str):
        raise ValidationError(f"{field} must be base64 text")
    try:
        return base64.b64decode(value.encode("ascii"), validate=True)
    except ValueError as error:
        raise ValidationError(f"{field} must be valid base64") from error


def require_json_string(
    body: dict[str, Any],
    field: str,
    *,
    default: str | None = None,
) -> str:
    if field not in body:
        if default is not None:
            return default
        raise ValidationError(f"{field} is required")
    value = body[field]
    if not isinstance(value, str):
        raise ValidationError(f"{field} must be a string")
    return value
