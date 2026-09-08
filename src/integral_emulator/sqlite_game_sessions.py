# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SQLite-backed Game Session Lock authority."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from .errors import GameSessionExpiredError, GameSessionFenceError, ValidationError
from .security import issue_secret
from .sessions import GameSessionLock
from .sqlite_repositories import RepositoryConflictError, SQLiteGameSessionRepository


class SQLiteGameSessionAuthority:
    def __init__(self, repository: SQLiteGameSessionRepository, server_id: str):
        self.repository = repository
        self.server_id = server_id

    def acquire_single(
        self,
        *,
        user_id: str,
        auth_session_id: str,
        execution_mode: str,
        lease_expires_at: str,
        expires_at: str,
        save_bindings: list[dict[str, Any]] | None = None,
        game_run_id: str | None = None,
    ) -> GameSessionLock:
        now_ms = self._now_ms()
        run_id = game_run_id or issue_secret("run")
        client_id = issue_secret("game")
        try:
            token = self.repository.acquire_single(
                {
                    "game_run_id": run_id,
                    "server_id": self.server_id,
                    "execution_mode": execution_mode,
                    "status": "RUNNING",
                    "auth_session_id": auth_session_id,
                    "created_at_ms": now_ms,
                    "expires_at_ms": self._epoch_ms(expires_at),
                    "reuse_existing_run": game_run_id is not None,
                },
                {
                    "user_id": user_id,
                    "auth_session_id": auth_session_id,
                    "game_session_id": client_id,
                    "lease_expires_at_ms": self._epoch_ms(lease_expires_at),
                    "last_heartbeat_at_ms": now_ms,
                    "save_binding": save_bindings or [],
                },
                now_ms=now_ms,
            )
        except RepositoryConflictError as error:
            raise ValidationError("user already has an active game session") from error
        record = self.repository.get_lock(user_id)
        if record is None:
            raise ValidationError("game session lock was not created")
        record["fencing_token"] = token
        return self._lock(record)

    def acquire_pair(
        self,
        *,
        game_run_id: str,
        first_user_id: str,
        first_auth_session_id: str,
        first_save_bindings: list[dict[str, Any]],
        second_user_id: str,
        second_auth_session_id: str,
        second_save_bindings: list[dict[str, Any]],
        lease_expires_at: str,
        expires_at: str,
    ) -> tuple[GameSessionLock, GameSessionLock]:
        now_ms = self._now_ms()
        if first_user_id == second_user_id:
            if first_auth_session_id != second_auth_session_id:
                raise ValidationError(
                    "same-account pair requires one authenticated session"
                )
            combined: dict[str, dict[str, Any]] = {}
            for binding in [*first_save_bindings, *second_save_bindings]:
                combined[str(binding.get("save_id"))] = binding
            lock = self.acquire_single(
                user_id=first_user_id,
                auth_session_id=first_auth_session_id,
                execution_mode="LINK_SESSION",
                lease_expires_at=lease_expires_at,
                expires_at=expires_at,
                save_bindings=list(combined.values()),
                game_run_id=game_run_id,
            )
            return lock, lock
        first_client_id = issue_secret("game")
        second_client_id = issue_secret("game")
        common_expiry = self._epoch_ms(lease_expires_at)
        try:
            self.repository.acquire_pair(
                {
                    "game_run_id": game_run_id,
                    "server_id": self.server_id,
                    "execution_mode": "LINK_SESSION",
                    "status": "RUNNING",
                    "auth_session_id": first_auth_session_id,
                    "created_at_ms": now_ms,
                    "expires_at_ms": self._epoch_ms(expires_at),
                },
                {
                    "user_id": first_user_id,
                    "auth_session_id": first_auth_session_id,
                    "game_session_id": first_client_id,
                    "lease_expires_at_ms": common_expiry,
                    "last_heartbeat_at_ms": now_ms,
                    "save_binding": first_save_bindings,
                },
                {
                    "user_id": second_user_id,
                    "auth_session_id": second_auth_session_id,
                    "game_session_id": second_client_id,
                    "lease_expires_at_ms": common_expiry,
                    "last_heartbeat_at_ms": now_ms,
                    "save_binding": second_save_bindings,
                },
                now_ms=now_ms,
            )
        except RepositoryConflictError as error:
            raise ValidationError("user already has an active game session") from error
        first = self.repository.get_lock(first_user_id)
        second = self.repository.get_lock(second_user_id)
        if first is None or second is None:
            raise ValidationError("game session pair was not created")
        return self._lock(first), self._lock(second)

    def active_for_user(self, user_id: str) -> GameSessionLock | None:
        record = self.repository.get_active_lock(user_id, self._now_ms())
        if record is None:
            return None
        return self._lock(record)

    def renew(
        self,
        *,
        user_id: str,
        game_run_id: str,
        game_session_id: str,
        auth_session_id: str,
        fencing_token: int,
        lease_expires_at: str,
    ) -> GameSessionLock:
        current = self.repository.get_active_lock(user_id, self._now_ms())
        self._require_identity(
            current, game_run_id, game_session_id, auth_session_id, fencing_token
        )
        now_ms = self._now_ms()
        if not self.repository.renew_lock(
            user_id,
            game_run_id,
            auth_session_id,
            fencing_token,
            self._epoch_ms(lease_expires_at),
            now_ms,
        ):
            raise GameSessionExpiredError("game session lock expired")
        renewed = self.repository.get_active_lock(user_id, now_ms)
        if renewed is None:
            raise GameSessionExpiredError("game session lock expired")
        return self._lock(renewed)

    def release(
        self,
        *,
        user_id: str,
        game_session_id: str,
        auth_session_id: str,
        fencing_token: int,
        status: str = "COMPLETED",
        reason: str | None = None,
    ) -> GameSessionLock:
        current = self.repository.get_active_lock(user_id, self._now_ms())
        if current is None:
            raise GameSessionExpiredError("game session lock expired")
        self._require_identity(
            current,
            str(current["game_run_id"]),
            game_session_id,
            auth_session_id,
            fencing_token,
        )
        removed = self.repository.release_lock(
            user_id,
            game_session_id,
            auth_session_id,
            fencing_token,
            status=status,
            reason=reason,
        )
        if removed is None:
            raise GameSessionExpiredError("game session lock expired")
        return self._lock({**current, **removed})

    def require_save_available(self, save_id: str) -> None:
        if self.repository.active_locks_for_save(save_id, self._now_ms()):
            raise ValidationError("save is bound to an active game session")

    @staticmethod
    def _require_identity(
        current: dict[str, Any] | None,
        game_run_id: str,
        game_session_id: str,
        auth_session_id: str,
        fencing_token: int,
    ) -> None:
        if current is None:
            raise GameSessionExpiredError("game session lock expired")
        if current["auth_session_id"] != auth_session_id:
            raise ValidationError("game session belongs to a different auth session")
        if (
            current["game_run_id"] != game_run_id
            or current["game_session_id"] != game_session_id
            or int(current["fencing_token"]) != fencing_token
        ):
            raise GameSessionFenceError("stale game session fence")

    @classmethod
    def _lock(cls, record: dict[str, Any]) -> GameSessionLock:
        mode = str(record["execution_mode"])
        return GameSessionLock(
            user_id=str(record["user_id"]),
            game_session_id=str(record["game_session_id"]),
            game_run_id=str(record["game_run_id"]),
            link_session_id=(str(record["game_run_id"]) if mode == "LINK_SESSION" else ""),
            auth_session_id=str(record["auth_session_id"]),
            fencing_token=int(record["fencing_token"]),
            started_at=cls._iso(int(record["created_at_ms"])),
            last_heartbeat=cls._iso(
                int(record.get("last_heartbeat_at_ms") or record["created_at_ms"])
            ),
            lease_expires_at=cls._iso(int(record["lease_expires_at_ms"])),
            execution_mode=mode,
            server_id=str(record["server_id"]),
            save_bindings=list(record.get("save_binding") or []),
            expires_at=cls._optional_iso(record.get("expires_at_ms")),
        )

    @staticmethod
    def _epoch_ms(value: str) -> int:
        parsed = datetime.fromisoformat(value)
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return int(parsed.timestamp() * 1000)

    @classmethod
    def _optional_ms(cls, value: str | None) -> int | None:
        return cls._epoch_ms(value) if value else None

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now(timezone.utc).timestamp() * 1000)

    @staticmethod
    def _iso(value: int) -> str:
        return datetime.fromtimestamp(value / 1000, tz=timezone.utc).isoformat()

    @classmethod
    def _optional_iso(cls, value: Any) -> str | None:
        return cls._iso(int(value)) if value is not None else None
