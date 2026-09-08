# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Link session lifecycle management."""

from __future__ import annotations

import shutil
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
from enum import StrEnum
from pathlib import Path
from typing import Any

from .errors import (
    GameSessionExpiredError,
    GameSessionFenceError,
    NotFoundError,
    RevisionConflictError,
    ValidationError,
)
from .gb_runtime_fixed_host_protocol import GB_RUNTIME_FIXED_HOST_PROTOCOL_ID
from .gb_runtime_link_modes import (
    GB_RUNTIME_LINK_RUNTIME,
    GBRuntimeLinkMode,
    effective_link_mode,
    normalize_link_mode,
)
from .models import now_iso
from .security import issue_secret, sha256_bytes
from .room_session_policy import RoomSessionPolicy


class LinkSessionStatus(StrEnum):
    CREATED = "CREATED"
    WAITING_PLAYER_A = "WAITING_PLAYER_A"
    WAITING_PLAYER_B = "WAITING_PLAYER_B"
    PREPARING = "PREPARING"
    RUNNING = "RUNNING"
    FINALIZING = "FINALIZING"
    RECOVERING = "RECOVERING"
    COMPLETED = "COMPLETED"
    FAILED = "FAILED"
    CANCELLED = "CANCELLED"
    EXPIRED = "EXPIRED"


ACTIVE_STATUSES = {
    LinkSessionStatus.CREATED,
    LinkSessionStatus.WAITING_PLAYER_A,
    LinkSessionStatus.WAITING_PLAYER_B,
    LinkSessionStatus.PREPARING,
    LinkSessionStatus.RUNNING,
    LinkSessionStatus.FINALIZING,
    LinkSessionStatus.RECOVERING,
}

MATCHABLE_STATUSES = {
    LinkSessionStatus.CREATED,
    LinkSessionStatus.WAITING_PLAYER_A,
    LinkSessionStatus.WAITING_PLAYER_B,
    LinkSessionStatus.PREPARING,
    LinkSessionStatus.RUNNING,
}

LEASE_RENEWABLE_STATUSES = MATCHABLE_STATUSES

FINALIZE_TIMEOUT_SECONDS = 5 * 60

COALESCED_EVENT_TYPES = {
    "LOCKS_RENEWED",
    "PLAYER_LOCK_RENEWED",
    "GAME_SESSION_LOCK_RENEWED",
}

TERMINAL_STATUSES = {
    LinkSessionStatus.COMPLETED,
    LinkSessionStatus.FAILED,
    LinkSessionStatus.CANCELLED,
    LinkSessionStatus.EXPIRED,
}

ALLOWED_TRANSITIONS = {
    LinkSessionStatus.CREATED: {
        LinkSessionStatus.PREPARING,
        LinkSessionStatus.RUNNING,
        LinkSessionStatus.COMPLETED,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.EXPIRED,
    },
    LinkSessionStatus.WAITING_PLAYER_A: {
        LinkSessionStatus.PREPARING,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.EXPIRED,
    },
    LinkSessionStatus.WAITING_PLAYER_B: {
        LinkSessionStatus.PREPARING,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.EXPIRED,
    },
    LinkSessionStatus.PREPARING: {
        LinkSessionStatus.RUNNING,
        LinkSessionStatus.COMPLETED,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.EXPIRED,
    },
    LinkSessionStatus.RUNNING: {
        LinkSessionStatus.FINALIZING,
        LinkSessionStatus.COMPLETED,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.EXPIRED,
    },
    LinkSessionStatus.FINALIZING: {
        LinkSessionStatus.COMPLETED,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.EXPIRED,
        LinkSessionStatus.RECOVERING,
    },
    LinkSessionStatus.RECOVERING: {
        LinkSessionStatus.COMPLETED,
        LinkSessionStatus.CANCELLED,
        LinkSessionStatus.FAILED,
        LinkSessionStatus.EXPIRED,
    },
    LinkSessionStatus.COMPLETED: set(),
    LinkSessionStatus.FAILED: set(),
    LinkSessionStatus.CANCELLED: set(),
    LinkSessionStatus.EXPIRED: set(),
}


@dataclass(frozen=True)
class LinkSession:
    id: str
    player_a_user_id: str
    player_b_user_id: str
    save_a_id: str
    save_b_id: str
    status: str
    temp_dir: str
    created_at: str
    updated_at: str
    started_at: str | None = None
    ended_at: str | None = None
    failure_reason: str | None = None
    host_process_id: str | None = None
    room_number: int | None = None
    base_port: int | None = None
    link_mode: str = GB_RUNTIME_LINK_RUNTIME
    requested_link_mode: str = GBRuntimeLinkMode.BATTLE.value
    protocol_id: str = GB_RUNTIME_FIXED_HOST_PROTOCOL_ID
    player_a_ticket_sha256: str | None = None
    player_b_ticket_sha256: str | None = None
    player_a_ticket_used: bool = False
    player_b_ticket_used: bool = False
    player_a_final_revision: int | None = None
    player_b_final_revision: int | None = None
    player_a_base_revision: int | None = None
    player_b_base_revision: int | None = None
    player_a_base_sha256: str | None = None
    player_b_base_sha256: str | None = None
    player_a_state: str = "RUNNING"
    player_b_state: str = "RUNNING"
    finalizing_started_at: str | None = None
    expires_at: str | None = None
    termination_reason: str | None = None
    row_version: int = 0

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "LinkSession":
        allowed = {field: data.get(field) for field in cls.__dataclass_fields__}
        if allowed.get("link_mode") != GB_RUNTIME_LINK_RUNTIME:
            raise ValidationError("stored Link session runtime must be gb_runtime")
        allowed["requested_link_mode"] = normalize_link_mode(
            allowed.get("requested_link_mode")
        )
        allowed["protocol_id"] = str(data.get("protocol_id") or GB_RUNTIME_FIXED_HOST_PROTOCOL_ID)
        allowed["player_a_ticket_used"] = bool(data.get("player_a_ticket_used", False))
        allowed["player_b_ticket_used"] = bool(data.get("player_b_ticket_used", False))
        allowed["player_a_state"] = data.get("player_a_state") or ("UPLOADED" if data.get("player_a_final_revision") is not None else "RUNNING")
        allowed["player_b_state"] = data.get("player_b_state") or ("UPLOADED" if data.get("player_b_final_revision") is not None else "RUNNING")
        allowed["row_version"] = int(data.get("__row_version", data.get("row_version", 0)))
        return cls(**allowed)

    def to_dict(self) -> dict[str, Any]:
        data = self.__dict__.copy()
        data.pop("player_a_ticket_sha256", None)
        data.pop("player_b_ticket_sha256", None)
        data.pop("player_a_ticket_used", None)
        data.pop("player_b_ticket_used", None)
        data.pop("row_version", None)
        return data

    def to_storage_dict(self) -> dict[str, Any]:
        data = self.__dict__.copy()
        data.pop("row_version", None)
        return data


@dataclass(frozen=True)
class GameSessionLock:
    user_id: str
    game_session_id: str
    game_run_id: str
    link_session_id: str
    auth_session_id: str
    fencing_token: int
    started_at: str
    last_heartbeat: str
    lease_expires_at: str
    execution_mode: str = "LINK_SESSION"
    server_id: str = ""
    save_bindings: list[dict[str, Any]] | None = None
    expires_at: str | None = None

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "GameSessionLock":
        normalized = data.copy()
        if "auth_session_id_digest" in normalized:
            normalized["auth_session_id"] = normalized.pop("auth_session_id_digest")
        normalized.pop("authority_schema_version", None)
        normalized.setdefault("execution_mode", "LINK_SESSION")
        normalized.setdefault("link_session_id", normalized.get("game_run_id", ""))
        normalized.setdefault("auth_session_id", "")
        normalized.setdefault("last_heartbeat", normalized.get("started_at", now_iso()))
        normalized.setdefault("lease_expires_at", normalized.get("last_heartbeat", now_iso()))
        normalized.setdefault("fencing_token", 0)
        normalized.setdefault("server_id", "")
        normalized.setdefault("save_bindings", [])
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        value = self.__dict__.copy()
        value["auth_session_id_digest"] = value.pop("auth_session_id")
        value["authority_schema_version"] = 2
        return value


class LinkSessionManager:
    def __init__(
        self,
        storage,
        saves,
        server_id: str = "",
        policy: RoomSessionPolicy | None = None,
        *,
        game_session_authority,
    ):
        self.storage = storage
        self.saves = saves
        self.server_id = server_id
        self.policy = policy or RoomSessionPolicy.from_environment()
        self.game_session_authority = game_session_authority

    def create_session(
        self,
        player_a_user_id: str,
        player_b_user_id: str,
        save_a_id: str,
        save_b_id: str,
        lock_expires_at: str,
        room_number: int | None = None,
        base_port: int | None = None,
        link_mode: str | None = None,
        protocol_id: str = GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
        auth_session_id: str = "",
        auth_session_ids: dict[str, str] | None = None,
    ) -> LinkSession:
        self.saves.get_save(save_a_id, player_a_user_id)
        self.saves.get_save(save_b_id, player_b_user_id)

        timestamp = now_iso()
        created_at = datetime.fromisoformat(timestamp)
        session_id = issue_secret("link")
        temp_dir = f"sessions/{session_id}"
        requested_link_mode = normalize_link_mode(
            link_mode if link_mode is not None else GBRuntimeLinkMode.BATTLE.value
        )
        if protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            raise ValidationError("unsupported Link protocol")
        session = LinkSession(
            id=session_id,
            player_a_user_id=player_a_user_id,
            player_b_user_id=player_b_user_id,
            save_a_id=save_a_id,
            save_b_id=save_b_id,
            status=LinkSessionStatus.CREATED.value,
            temp_dir=temp_dir,
            created_at=timestamp,
            updated_at=timestamp,
            room_number=room_number,
            base_port=base_port,
            link_mode=effective_link_mode(requested_link_mode),
            requested_link_mode=requested_link_mode,
            protocol_id=protocol_id,
            player_a_base_revision=self.saves.get_save(save_a_id, player_a_user_id).revision,
            player_b_base_revision=self.saves.get_save(save_b_id, player_b_user_id).revision,
            player_a_base_sha256=self.saves.get_save(save_a_id, player_a_user_id).sha256,
            player_b_base_sha256=self.saves.get_save(save_b_id, player_b_user_id).sha256,
        )

        game_locks_acquired = False
        session_saved = False
        locked_a = False
        try:
            acquired_locks = self.acquire_game_session_locks(
                session,
                lock_expires_at,
                auth_session_id=auth_session_id,
                auth_session_ids=auth_session_ids,
            )
            game_locks_acquired = True
            session = LinkSession.from_dict(
                self.storage.insert_link_session(session.to_storage_dict())
            )
            session_saved = True
            lock_by_user = {lock.user_id: lock for lock in acquired_locks}
            lock_a = lock_by_user[player_a_user_id]
            lock_b = lock_by_user[player_b_user_id]
            self.saves.lock_save(
                save_a_id, player_a_user_id, owner=session_id,
                expires_at=lock_expires_at, auth_session_id=lock_a.auth_session_id,
                game_run_id=lock_a.game_run_id, fencing_token=lock_a.fencing_token,
            )
            locked_a = True
            self.saves.lock_save(
                save_b_id, player_b_user_id, owner=session_id,
                expires_at=lock_expires_at, auth_session_id=lock_b.auth_session_id,
                game_run_id=lock_b.game_run_id, fencing_token=lock_b.fencing_token,
            )
            self.session_temp_path(session).mkdir(parents=True, exist_ok=True)
            self.record_event(session_id, "SESSION_CREATED", {"status": session.status})
        except Exception:
            if locked_a:
                self.saves.unlock_save(
                    save_a_id, player_a_user_id, owner=session_id,
                    fencing_token=lock_a.fencing_token,
                )
            if game_locks_acquired:
                self.release_game_session_locks(session)
            if session_saved:
                self.cleanup_temp_files(session_id)
            current = self.storage.get_link_session(session_id)
            if current is not None:
                self.storage.delete_link_session(session_id, int(current["__row_version"]))
            raise
        return session

    def get_session(self, session_id: str) -> LinkSession:
        data = self.storage.get_link_session(session_id)
        if not data:
            raise NotFoundError("link session not found")
        return self._ensure_deadlines(LinkSession.from_dict(data))

    def all_sessions(self) -> list[LinkSession]:
        sessions = self.storage.load_link_sessions()
        return [self._ensure_deadlines(LinkSession.from_dict(data)) for data in sessions.values()]

    def issue_connection_ticket(self, session_id: str, user_id: str) -> tuple[LinkSession, str, str]:
        with self.storage.exclusive_lock("link-relay-ticket"):
            data = self.storage.get_link_session(session_id)
            if not data:
                raise NotFoundError("link session not found")
            current = LinkSession.from_dict(data)
            self.require_active(current)
            if user_id == current.player_a_user_id:
                role = "a"
            elif user_id == current.player_b_user_id:
                role = "b"
            else:
                raise ValidationError("link session access denied")
            ticket = issue_secret("fgbcauth")
            digest = sha256_bytes(ticket.encode("utf-8"))
            updates = {
                f"player_{role}_ticket_sha256": digest,
                f"player_{role}_ticket_used": False,
                "updated_at": now_iso(),
            }
            issued = replace(current, **updates)
            issued = LinkSession.from_dict(self.storage.update_link_session(
                issued.to_storage_dict(), current.row_version
            ))
        self.record_event(session_id, "CONNECTION_TICKET_ISSUED", {"player": role})
        return issued, role, ticket

    def list_for_user(self, user_id: str) -> list[LinkSession]:
        sessions = self.storage.load_link_sessions()
        records = [
            LinkSession.from_dict(session)
            for session in sessions.values()
            if session["player_a_user_id"] == user_id or session["player_b_user_id"] == user_id
        ]
        return sorted(records, key=lambda session: session.created_at)

    def find_active_for_match(
        self,
        player_a_user_id: str,
        player_b_user_id: str,
        save_a_id: str,
        save_b_id: str,
        link_mode: str | None = None,
        protocol_id: str | None = None,
    ) -> LinkSession | None:
        requested_mode = normalize_link_mode(
            link_mode if link_mode is not None else GBRuntimeLinkMode.BATTLE.value
        )
        sessions = self.storage.load_link_sessions()
        active_statuses = {status.value for status in MATCHABLE_STATUSES}
        matches = []
        for session_data in sessions.values():
            session = LinkSession.from_dict(session_data)
            if session.status not in active_statuses:
                continue
            if self.deadline_reached(session.expires_at):
                continue
            if (
                session.player_a_user_id == player_a_user_id
                and session.player_b_user_id == player_b_user_id
                and session.save_a_id == save_a_id
                and session.save_b_id == save_b_id
                and session.link_mode == effective_link_mode(requested_mode)
                and session.requested_link_mode == requested_mode
                and (protocol_id is None or session.protocol_id == protocol_id)
            ):
                matches.append(session)
        if not matches:
            return None
        return sorted(matches, key=lambda session: session.created_at)[-1]

    def list_events(self, session_id: str) -> list[dict[str, Any]]:
        self.get_session(session_id)
        events = self.storage.load_session_events()
        records = [event for event in events.values() if event["session_id"] == session_id]
        return sorted(records, key=lambda event: event["created_at"])

    def transition(self, session_id: str, status: LinkSessionStatus, reason: str | None = None) -> LinkSession:
        current = self.get_session(session_id)
        current_status = LinkSessionStatus(current.status)
        if status == current_status:
            if status in TERMINAL_STATUSES:
                self.release_locks(current)
                self.release_game_session_locks(current)
                self.cleanup_temp_files(session_id)
            return current
        if status not in ALLOWED_TRANSITIONS[current_status]:
            raise ValidationError(f"cannot transition link session from {current.status} to {status.value}")
        timestamp = now_iso()
        next_session = replace(
            current,
            status=status.value,
            updated_at=timestamp,
            failure_reason=reason,
            termination_reason=reason if status in TERMINAL_STATUSES or status == LinkSessionStatus.RECOVERING else current.termination_reason,
        )
        if status == LinkSessionStatus.RUNNING and current.started_at is None:
            started = datetime.fromisoformat(timestamp)
            next_session = replace(
                next_session,
                started_at=timestamp,
                expires_at=self.policy.deadline(started, self.policy.link_seconds),
            )
        if status == LinkSessionStatus.FINALIZING:
            next_session = replace(
                next_session,
                finalizing_started_at=timestamp,
                expires_at=self.policy.deadline(
                    datetime.fromisoformat(timestamp), self.policy.finalize_seconds
                ),
            )
        if status == LinkSessionStatus.RECOVERING:
            next_session = replace(
                next_session,
                player_a_state="UPLOADED" if current.player_a_state == "UPLOADED" else "RECOVERABLE",
                player_b_state="UPLOADED" if current.player_b_state == "UPLOADED" else "RECOVERABLE",
            )
        if status not in ACTIVE_STATUSES and current.ended_at is None:
            next_session = replace(next_session, ended_at=timestamp)

        next_session = LinkSession.from_dict(self.storage.update_link_session(
            next_session.to_storage_dict(), current.row_version
        ))
        if status == LinkSessionStatus.RUNNING:
            self._set_game_lock_deadlines(next_session)
        elif status == LinkSessionStatus.FINALIZING:
            self._set_finalization_locks(next_session)
        self.record_event(session_id, "STATUS_CHANGED", {"from": current.status, "to": status.value, "reason": reason})
        if status in TERMINAL_STATUSES:
            self.release_locks(next_session)
            self.release_game_session_locks(next_session)
            self.cleanup_temp_files(session_id)
        elif status == LinkSessionStatus.RECOVERING:
            self.release_locks(next_session)
            self.release_game_session_locks(next_session)
        return next_session

    def attach_host_process(self, session_id: str, host_process_id: str, replace_existing: bool = False) -> LinkSession:
        current = self.get_session(session_id)
        self.require_active(current)
        if current.host_process_id and not replace_existing:
            return current
        next_session = replace(current, host_process_id=host_process_id, updated_at=now_iso())
        next_session = LinkSession.from_dict(self.storage.update_link_session(
            next_session.to_storage_dict(), current.row_version
        ))
        self.record_event(session_id, "HOST_PROCESS_ATTACHED", {"host_process_id": host_process_id})
        return next_session

    def release_locks(self, session: LinkSession) -> None:
        for save_id, user_id in (
            (session.save_a_id, session.player_a_user_id),
            (session.save_b_id, session.player_b_user_id),
        ):
            current = self.saves.get_save(save_id, user_id)
            if current.lock_owner == session.id:
                self.saves.unlock_save(save_id, user_id, owner=session.id)

    def renew_player_save_locks(self, session_id: str, user_id: str, expires_at: str) -> LinkSession:
        session = self.get_session(session_id)
        if LinkSessionStatus(session.status) not in LEASE_RENEWABLE_STATUSES:
            return session
        renewed_ids: set[str] = set()
        if session.player_a_user_id == user_id:
            self.saves.renew_lock(session.save_a_id, user_id, owner=session.id, expires_at=expires_at)
            renewed_ids.add(session.save_a_id)
        if session.player_b_user_id == user_id and session.save_b_id not in renewed_ids:
            self.saves.renew_lock(session.save_b_id, user_id, owner=session.id, expires_at=expires_at)
        renewed = replace(session, updated_at=now_iso())
        renewed = LinkSession.from_dict(self.storage.update_link_session(
            renewed.to_storage_dict(), session.row_version
        ))
        self.record_event(session_id, "PLAYER_LOCK_RENEWED", {"user_id": user_id, "lock_expires_at": expires_at})
        return renewed

    def acquire_game_session_locks(
        self,
        session: LinkSession,
        lease_expires_at: str,
        auth_session_id: str = "",
        auth_session_ids: dict[str, str] | None = None,
    ) -> list[GameSessionLock]:
        auth_by_user = auth_session_ids or {}
        first_auth = auth_by_user.get(session.player_a_user_id, auth_session_id)
        second_auth = auth_by_user.get(session.player_b_user_id, "")
        if not first_auth or not second_auth:
            raise ValidationError("both participants require an authenticated ROOM session")
        saves_by_user = {
            session.player_a_user_id: [self.saves.get_save(session.save_a_id, session.player_a_user_id)],
            session.player_b_user_id: [self.saves.get_save(session.save_b_id, session.player_b_user_id)],
        }
        expires_at = self.policy.deadline(
            datetime.fromisoformat(session.created_at), self.policy.link_seconds
        )
        first, second = self.game_session_authority.acquire_pair(
            game_run_id=session.id,
            first_user_id=session.player_a_user_id,
            first_auth_session_id=first_auth,
            first_save_bindings=[
                {"save_id": save.id, "revision": save.revision, "sha256": save.sha256}
                for save in saves_by_user[session.player_a_user_id]
            ],
            second_user_id=session.player_b_user_id,
            second_auth_session_id=second_auth,
            second_save_bindings=[
                {"save_id": save.id, "revision": save.revision, "sha256": save.sha256}
                for save in saves_by_user[session.player_b_user_id]
            ],
            lease_expires_at=lease_expires_at,
            expires_at=expires_at,
        )
        return [first, second]

    def release_game_session_locks(self, session: LinkSession) -> None:
        self.game_session_authority.repository.release_run(
            session.id, status="COMPLETED"
        )

    def acquire_local_game_session_lock(
        self,
        user_id: str,
        lease_expires_at: str,
        auth_session_id: str,
        execution_mode: str = "LOCAL_CLIENT",
        saves: list[Any] | None = None,
        operation_id: str | None = None,
    ) -> GameSessionLock:
        expires_at = self.policy.deadline(
            datetime.now(timezone.utc),
            self.policy.n64_runtime_seconds
            if execution_mode.startswith("N64_RUNTIME")
            else self.policy.link_seconds,
        )
        return self.game_session_authority.acquire_single(
            user_id=user_id,
            auth_session_id=auth_session_id,
            execution_mode=execution_mode,
            lease_expires_at=lease_expires_at,
            expires_at=expires_at,
            save_bindings=[
                {"save_id": save.id, "revision": save.revision, "sha256": save.sha256}
                for save in (saves or [])
            ],
            game_run_id=operation_id,
        )

    def acquire_media_game_session_lock(
        self,
        user_id: str,
        media_session_id: str,
        lease_expires_at: str,
        auth_session_id: str,
        saves: list[Any],
        expires_at: str | None = None,
    ) -> GameSessionLock:
        existing = self.game_session_authority.active_for_user(user_id)
        if existing is not None:
            if (
                existing.execution_mode != "N64_RUNTIME_NOSAVE"
                or existing.game_run_id != media_session_id
                or existing.auth_session_id != auth_session_id
            ):
                raise ValidationError("user already has an active game session")
            return self.game_session_authority.renew(
                user_id=user_id,
                game_run_id=existing.game_run_id,
                game_session_id=existing.game_session_id,
                auth_session_id=auth_session_id,
                fencing_token=existing.fencing_token,
                lease_expires_at=lease_expires_at,
            )
        return self.game_session_authority.acquire_single(
            user_id=user_id,
            auth_session_id=auth_session_id,
            execution_mode="N64_RUNTIME_NOSAVE",
            lease_expires_at=lease_expires_at,
            expires_at=expires_at
            or self.policy.deadline(
                datetime.now(timezone.utc), self.policy.n64_runtime_seconds
            ),
            game_run_id=media_session_id,
            save_bindings=[
                {"save_id": save.id, "revision": save.revision, "sha256": save.sha256}
                for save in saves
            ],
        )

    def release_media_game_session_locks(self, media_session_id: str, user_ids: set[str]) -> None:
        self.game_session_authority.repository.release_run(
            media_session_id, status="COMPLETED"
        )

    def release_user_game_session_lock(
        self,
        user_id: str,
        auth_session_id: str,
        game_session_id: str = "",
        fencing_token: int = 0,
    ) -> GameSessionLock:
        return self.game_session_authority.release(
            user_id=user_id,
            game_session_id=game_session_id,
            auth_session_id=auth_session_id,
            fencing_token=fencing_token,
        )

    def renew_local_game_session_lock(
        self,
        user_id: str,
        lease_expires_at: str,
        auth_session_id: str,
        game_session_id: str,
        fencing_token: int,
    ) -> GameSessionLock:
        active = self.game_session_authority.active_for_user(user_id)
        if active is None:
            raise GameSessionExpiredError("game session lock expired")
        if active.execution_mode not in {
            "LOCAL_CLIENT", "MOBILE_CLIENT", "N64_RUNTIME_CLIENT", "N64_RUNTIME_NOSAVE"
        } or active.link_session_id:
            raise ValidationError("active game session is not local play")
        return self.game_session_authority.renew(
            user_id=user_id,
            game_run_id=active.game_run_id,
            game_session_id=game_session_id,
            auth_session_id=auth_session_id,
            fencing_token=fencing_token,
            lease_expires_at=lease_expires_at,
        )

    def authorize_normal_save_upload(
        self,
        user_id: str,
        save_id: str,
        auth_session_id: str,
        game_session_id: str,
        fencing_token: int,
    ) -> GameSessionLock | None:
        active = self.active_game_session_for_user(user_id)
        if not active:
            if game_session_id or fencing_token > 0:
                raise GameSessionExpiredError("game session lock expired")
            return None
        lock = active["lock"]
        if not any(binding.get("save_id") == save_id for binding in (lock.save_bindings or [])):
            return None
        if lock.server_id and lock.server_id != self.server_id:
            raise ValidationError("save is bound to a game session on another server")
        if lock.execution_mode == "N64_RUNTIME_NOSAVE":
            raise ValidationError("save uploads are disabled for no-save game sessions")
        if lock.link_session_id or lock.execution_mode == "LINK_SESSION":
            raise ValidationError("save is bound to an active link session; use the finalize endpoint")
        if lock.auth_session_id != auth_session_id:
            raise ValidationError("game session belongs to a different auth session")
        self._require_matching_fence(lock, game_session_id, fencing_token)
        return lock

    def require_save_not_in_active_game(self, save_id: str) -> None:
        self.game_session_authority.require_save_available(save_id)

    def _require_matching_fence(self, lock: GameSessionLock, game_session_id: str, fencing_token: int) -> None:
        if not game_session_id or fencing_token <= 0:
            raise GameSessionFenceError("game_session_id and fencing_token are required")
        if lock.game_session_id != game_session_id or lock.fencing_token != fencing_token:
            raise GameSessionFenceError("stale game session fence")

    def renew_game_session_lock(self, session_id: str, user_id: str, lease_expires_at: str, auth_session_id: str = "") -> GameSessionLock:
        session = self.get_session(session_id)
        if LinkSessionStatus(session.status) not in LEASE_RENEWABLE_STATUSES:
            raise ValidationError("game session is not active")
        if user_id not in {session.player_a_user_id, session.player_b_user_id}:
            raise ValidationError("game session does not belong to user")
        active = self.game_session_authority.active_for_user(user_id)
        if active is None or active.game_run_id != session.id:
            raise ValidationError("game session lock not found")
        renewed = self.game_session_authority.renew(
            user_id=user_id,
            game_run_id=session.id,
            game_session_id=active.game_session_id,
            auth_session_id=auth_session_id,
            fencing_token=active.fencing_token,
            lease_expires_at=lease_expires_at,
        )
        self.record_event(session_id, "GAME_SESSION_LOCK_RENEWED", {"user_id": user_id, "lease_expires_at": lease_expires_at})
        return renewed

    def bind_game_session_lock(self, session_id: str, user_id: str, auth_session_id: str) -> GameSessionLock:
        session = self.get_session(session_id)
        if user_id not in {session.player_a_user_id, session.player_b_user_id}:
            raise ValidationError("game session does not belong to user")
        active = self.game_session_authority.active_for_user(user_id)
        if active is None or active.game_run_id != session.id:
            raise ValidationError("game session lock not found")
        if active.auth_session_id != auth_session_id:
            raise ValidationError("game session belongs to a different auth session")
        return active

    def require_game_session_lock(self, session: LinkSession, user_id: str, auth_session_id: str = "") -> GameSessionLock:
        if user_id not in {session.player_a_user_id, session.player_b_user_id}:
            raise ValidationError("game session does not belong to user")
        active = self.game_session_authority.active_for_user(user_id)
        if active is None or active.game_run_id != session.id:
            raise ValidationError("game session lock not found")
        if auth_session_id and active.auth_session_id != auth_session_id:
            raise ValidationError("game session belongs to a different auth session")
        return active

    def active_game_session_for_user(self, user_id: str) -> dict[str, Any] | None:
        lock = self.game_session_authority.active_for_user(user_id)
        if lock is None:
            return None
        session = None
        if lock.link_session_id:
            try:
                session = self.get_session(lock.link_session_id)
            except NotFoundError:
                session = None
        return {"lock": lock, "link_session": session}

    def require_game_session_owner(self, user_id: str, auth_session_id: str) -> dict[str, Any]:
        active = self.active_game_session_for_user(user_id)
        if not active:
            raise ValidationError("active game session not found")
        lock = active["lock"]
        if not lock.auth_session_id:
            raise ValidationError("game session is not bound to this auth session")
        if lock.auth_session_id != auth_session_id:
            raise ValidationError("game session belongs to a different auth session")
        return active

    def require_matching_game_session_fence(
        self,
        lock: GameSessionLock,
        game_session_id: str,
        fencing_token: int,
    ) -> None:
        self._require_matching_fence(lock, game_session_id, fencing_token)

    def cleanup_temp_files(self, session_id: str) -> None:
        session = self.get_session(session_id)
        temp_path = self.session_temp_path(session)
        if temp_path.exists():
            shutil.rmtree(temp_path)
        self.record_event(session_id, "TEMP_FILES_CLEANED", {"temp_dir": session.temp_dir})

    def commit_changed_staged_saves(self, session_id: str, durable_final: bool = False) -> dict[str, Any]:
        session = self.get_session(session_id)
        existing_journal = self.storage.get_pair_save_journal(session_id)
        if (
            existing_journal
            and existing_journal.get("state") == "COMMITTED"
            and session.status == LinkSessionStatus.COMPLETED.value
        ):
            return dict(existing_journal["result"])
        if session.status not in {
            LinkSessionStatus.PREPARING.value,
            LinkSessionStatus.RUNNING.value,
            LinkSessionStatus.FINALIZING.value,
        }:
            raise ValidationError("link session cannot commit saves in this state")
        temp_path = self.session_temp_path(session)
        save_a_path = temp_path / "player_a.sav"
        save_b_path = temp_path / "player_b.sav"
        if not save_a_path.exists() or not save_b_path.exists():
            raise ValidationError("staged save files are missing")

        save_a = self.saves.get_save(session.save_a_id, session.player_a_user_id)
        save_b = self.saves.get_save(session.save_b_id, session.player_b_user_id)
        self.require_game_session_lock(session, session.player_a_user_id)
        self.require_game_session_lock(session, session.player_b_user_id)
        save_a_bytes = save_a_path.read_bytes()
        save_b_bytes = save_b_path.read_bytes()
        save_a_changed = sha256_bytes(save_a_bytes) != save_a.sha256
        save_b_changed = sha256_bytes(save_b_bytes) != save_b.sha256
        replace_journal_version: int | None = None
        if session.requested_link_mode == GBRuntimeLinkMode.BATTLE.value:
            self.saves.verify_locked_revision(
                session.save_a_id,
                session.player_a_user_id,
                owner=session.id,
                expected_revision=save_a.revision,
            )
            self.saves.verify_locked_revision(
                session.save_b_id,
                session.player_b_user_id,
                owner=session.id,
                expected_revision=save_b.revision,
            )
            result = {
                "player_a_revision": save_a.revision,
                "player_b_revision": save_b.revision,
                "changed": [],
            }
            if save_a_changed or save_b_changed:
                self.record_event(session_id, "BATTLE_STAGED_SAVES_SKIPPED", result)
            return result
        if existing_journal and existing_journal.get("state") == "COMMITTED":
            previous_players = existing_journal.get("players", {})
            same_payloads = (
                previous_players.get("player_a", {}).get("payload_sha256") == sha256_bytes(save_a_bytes)
                and previous_players.get("player_b", {}).get("payload_sha256") == sha256_bytes(save_b_bytes)
            )
            if same_payloads and not save_a_changed and not save_b_changed:
                result = {
                    "player_a_revision": save_a.revision,
                    "player_b_revision": save_b.revision,
                    "changed": [],
                }
                if durable_final:
                    existing_journal["result"] = result
                    existing_journal["final"] = True
                    existing_journal["committed_at"] = now_iso()
                    existing_journal["updated_at"] = now_iso()
                    existing_journal = self.storage.update_pair_save_journal(
                        existing_journal, int(existing_journal["__row_version"])
                    )
                return result
            replace_journal_version = int(existing_journal["__row_version"])
            existing_journal = None
        if existing_journal:
            if existing_journal.get("source") != "host-staged":
                raise ValidationError("link save commit journal source mismatch")
            journal = dict(existing_journal)
        else:
            players = {
                "player_a": {
                    "save_id": session.save_a_id,
                    "user_id": session.player_a_user_id,
                    "base_revision": save_a.revision,
                    "target_revision": save_a.revision + (1 if save_a_changed else 0),
                    "payload_sha256": sha256_bytes(save_a_bytes),
                    "changed": save_a_changed,
                    "stage_path": str(save_a_path),
                    "committed": False,
                    "result_revision": None,
                },
                "player_b": {
                    "save_id": session.save_b_id,
                    "user_id": session.player_b_user_id,
                    "base_revision": save_b.revision,
                    "target_revision": save_b.revision + (1 if save_b_changed else 0),
                    "payload_sha256": sha256_bytes(save_b_bytes),
                    "changed": save_b_changed,
                    "stage_path": str(save_b_path),
                    "committed": False,
                    "result_revision": None,
                },
            }
            for entry, payload in ((players["player_a"], save_a_bytes), (players["player_b"], save_b_bytes)):
                if entry["changed"]:
                    self.saves.validate_locked_commit(
                        entry["save_id"],
                        entry["user_id"],
                        owner=session.id,
                        expected_revision=entry["base_revision"],
                        save_bytes=payload,
                    )
                else:
                    self.saves.verify_locked_revision(
                        entry["save_id"],
                        entry["user_id"],
                        owner=session.id,
                        expected_revision=entry["base_revision"],
                    )
            journal = {
                "session_id": session_id,
                "source": "host-staged",
                "state": "PREPARED",
                "prepared_at": now_iso(),
                "players": players,
            }
            if replace_journal_version is None:
                journal = self.storage.insert_pair_save_journal(journal)
            else:
                journal = self.storage.update_pair_save_journal(
                    journal, replace_journal_version
                )

        journal["state"] = "COMMITTING"
        journal["updated_at"] = now_iso()
        journal = self.storage.update_pair_save_journal(
            journal, int(journal["__row_version"])
        )
        for player_name in ("player_a", "player_b"):
            entry = journal["players"][player_name]
            if entry.get("committed"):
                continue
            current = self.saves.get_save(entry["save_id"], entry["user_id"])
            payload = Path(entry["stage_path"]).read_bytes()
            if sha256_bytes(payload) != entry["payload_sha256"]:
                raise ValidationError("staged save changed after commit preparation")
            if entry["changed"]:
                if current.revision == entry["base_revision"]:
                    current = self.saves.commit_locked(
                        entry["save_id"],
                        entry["user_id"],
                        owner=session.id,
                        expected_revision=entry["base_revision"],
                        save_bytes=payload,
                    )
                elif not (
                    current.revision == entry["target_revision"]
                    and current.sha256 == entry["payload_sha256"]
                    and sha256_bytes(self.storage.read_bytes(current.storage_path)) == entry["payload_sha256"]
                ):
                    raise RevisionConflictError(
                        f"expected revision {entry['base_revision']}, current revision is {current.revision}"
                    )
            else:
                if current.revision != entry["base_revision"]:
                    raise RevisionConflictError(
                        f"expected revision {entry['base_revision']}, current revision is {current.revision}"
                    )
            entry["committed"] = True
            entry["result_revision"] = current.revision
            journal["players"][player_name] = entry

        changed = [name for name in ("player_a", "player_b") if journal["players"][name]["changed"]]
        result = {
            "player_a_revision": journal["players"]["player_a"]["result_revision"],
            "player_b_revision": journal["players"]["player_b"]["result_revision"],
            "changed": changed,
        }
        journal["state"] = "COMMITTED"
        journal["final"] = durable_final
        journal["committed_at"] = now_iso()
        journal["result"] = result
        journal["updated_at"] = now_iso()
        journal = self.storage.update_pair_save_journal(
            journal, int(journal["__row_version"])
        )
        if changed:
            self.record_event(session_id, "STAGED_SAVES_SYNCED", result)
        return result

    def stage_gb_runtime_fixed_host_candidates(
        self,
        session_id: str,
        host_user_id: str,
        auth_session_id: str,
        game_session_id: str,
        fencing_token: int,
        host_candidate: bytes,
        remote_candidate: bytes,
    ) -> dict[str, str]:
        """Validate and stage the Host-owned pair without committing either side."""
        if not host_candidate or not remote_candidate:
            raise ValidationError("both fixed Host save candidates are required")
        session = self.get_session(session_id)
        self.require_active(session)
        if session.protocol_id != "gb_runtime_fixed_host_v1" or session.requested_link_mode != GBRuntimeLinkMode.TRADE.value:
            raise ValidationError("fixed Host save candidates require Trade mode")
        if host_user_id != session.player_a_user_id:
            raise ValidationError("fixed Host save candidates require player A account")
        host_lock = self.require_game_session_lock(
            session, host_user_id, auth_session_id=auth_session_id
        )
        self.require_matching_game_session_fence(
            host_lock, game_session_id, fencing_token
        )
        self.require_game_session_lock(session, session.player_b_user_id)
        self.saves.validate_locked_commit(
            session.save_a_id,
            session.player_a_user_id,
            owner=session.id,
            expected_revision=int(session.player_a_base_revision),
            save_bytes=host_candidate,
        )
        self.saves.validate_locked_commit(
            session.save_b_id,
            session.player_b_user_id,
            owner=session.id,
            expected_revision=int(session.player_b_base_revision),
            save_bytes=remote_candidate,
        )
        host_sha256 = sha256_bytes(host_candidate)
        remote_sha256 = sha256_bytes(remote_candidate)
        journal = self.storage.get_pair_save_journal(session_id)
        if journal and journal.get("state") != "COMMITTED":
            players = journal.get("players") or {}
            if (
                players.get("player_a", {}).get("payload_sha256") != host_sha256
                or players.get("player_b", {}).get("payload_sha256") != remote_sha256
            ):
                raise ValidationError("fixed Host candidate pair is immutable")
        temp_path = self.session_temp_path(session)
        temp_path.mkdir(parents=True, exist_ok=True)
        self.storage.atomic_write_bytes(temp_path / "player_a.sav", host_candidate)
        self.storage.atomic_write_bytes(temp_path / "player_b.sav", remote_candidate)
        self.record_event(
            session_id,
            "GB_RUNTIME_FIXED_HOST_CANDIDATES_STAGED",
            {"host_sha256": host_sha256, "remote_sha256": remote_sha256},
        )
        return {"host_sha256": host_sha256, "remote_sha256": remote_sha256}

    def completed_staged_save_commit_result(self, session_id: str) -> dict[str, Any] | None:
        journal = self.storage.get_pair_save_journal(session_id)
        if not journal or journal.get("state") != "COMMITTED" or not journal.get("result"):
            return None
        return dict(journal["result"])

    def client_saves_are_final(self, session_id: str) -> bool:
        session = self.get_session(session_id)
        return session.player_a_final_revision is not None and session.player_b_final_revision is not None

    def require_saves_not_in_partial_pair_commit(self, save_ids: list[str]) -> None:
        """Prevent readers from observing one side of an unfinished two-SAV commit."""
        requested = set(save_ids)
        if not requested:
            return
        journals = self.storage.load_pair_save_journals()
        for journal in journals.values():
            if journal.get("state") != "COMMITTING":
                continue
            journal_save_ids = {
                str(entry.get("save_id", ""))
                for entry in journal.get("players", {}).values()
            }
            if requested & journal_save_ids:
                raise ValidationError("save pair commit recovery is in progress")

    def mark_participant_disconnected(self, session_id: str, user_id: str) -> LinkSession:
        session = self.get_session(session_id)
        if session.status not in {LinkSessionStatus.FINALIZING.value, LinkSessionStatus.RECOVERING.value}:
            return session
        updates: dict[str, Any] = {"updated_at": now_iso()}
        if session.player_a_user_id == user_id and session.player_a_state != "UPLOADED":
            updates["player_a_state"] = "DISCONNECTED"
        if session.player_b_user_id == user_id and session.player_b_state != "UPLOADED":
            updates["player_b_state"] = "DISCONNECTED"
        if len(updates) == 1:
            return session
        next_session = replace(session, **updates)
        next_session = LinkSession.from_dict(self.storage.update_link_session(
            next_session.to_storage_dict(), session.row_version
        ))
        self.record_event(session_id, "PARTICIPANT_DISCONNECTED", {"user_id": user_id})
        return next_session

    def reconcile_finalization(self, session_id: str) -> LinkSession:
        session = self.get_session(session_id)
        if session.status != LinkSessionStatus.FINALIZING.value or not session.expires_at:
            return session
        try:
            deadline = datetime.fromisoformat(session.expires_at)
        except ValueError:
            deadline = datetime.now(timezone.utc)
        if deadline.tzinfo is None:
            deadline = deadline.replace(tzinfo=timezone.utc)
        if deadline > datetime.now(timezone.utc):
            return session
        self.record_event(session_id, "TRADE_RESULT_DEADLINE_EXPIRED", {"expires_at": session.expires_at})
        return self.transition(
            session_id,
            LinkSessionStatus.EXPIRED,
            reason="trade result deadline expired",
        )

    def recover_orphaned_sessions(self, reason: str = "startup recovery") -> list[LinkSession]:
        sessions = self.storage.load_link_sessions()
        active_statuses = {status.value for status in ACTIVE_STATUSES}
        recovered: list[LinkSession] = []
        for session_data in list(sessions.values()):
            session = LinkSession.from_dict(session_data)
            if session.status == LinkSessionStatus.FINALIZING.value:
                recovered.append(self.transition(session.id, LinkSessionStatus.RECOVERING, reason=reason))
            elif session.status in active_statuses and session.status != LinkSessionStatus.RECOVERING.value:
                recovered.append(self.transition(session.id, LinkSessionStatus.FAILED, reason=reason))
        return recovered

    def prune_expired_game_session_locks(
        self, now: datetime | None = None
    ) -> list[GameSessionLock]:
        """Remove expired standalone or orphaned locks during lifecycle sweeping."""
        current = now or datetime.now(timezone.utc)
        rows = self.game_session_authority.repository.prune_expired_locks(
            int(current.timestamp() * 1000)
        )
        return [self.game_session_authority._lock(row) for row in rows]

    def participant_leases_expired(self, session: LinkSession, now: datetime | None = None) -> bool:
        current = now or datetime.now(timezone.utc)
        locks = self.game_session_authority.repository.active_locks_for_run(
            session.id, int(current.timestamp() * 1000)
        )
        return {lock["user_id"] for lock in locks} != {
            session.player_a_user_id, session.player_b_user_id
        }

    def media_participant_leases_expired(
        self,
        media_session_id: str,
        user_ids: set[str],
        now: datetime | None = None,
    ) -> bool:
        current = now or datetime.now(timezone.utc)
        locks = self.game_session_authority.repository.active_locks_for_run(
            media_session_id, int(current.timestamp() * 1000)
        )
        return {lock["user_id"] for lock in locks} != user_ids

    def _set_game_lock_deadlines(self, session: LinkSession) -> None:
        if not session.expires_at:
            return
        self.game_session_authority.repository.update_run_deadline(
            session.id,
            expires_at_ms=self.game_session_authority._epoch_ms(session.expires_at),
        )

    def _set_finalization_locks(self, session: LinkSession) -> None:
        if not session.expires_at:
            return
        deadline_ms = self.game_session_authority._epoch_ms(session.expires_at)
        self.game_session_authority.repository.update_run_deadline(
            session.id,
            expires_at_ms=deadline_ms,
            status="FINALIZING",
        )
        self.game_session_authority.repository.extend_run_locks(
            session.id, deadline_ms
        )

    def _ensure_deadlines(self, session: LinkSession) -> LinkSession:
        return session

    @staticmethod
    def _parse_datetime(value: str | None) -> datetime | None:
        if not value:
            return None
        try:
            parsed = datetime.fromisoformat(value)
        except ValueError:
            return None
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return parsed

    @classmethod
    def deadline_reached(cls, value: str | None, now: datetime | None = None) -> bool:
        deadline = cls._parse_datetime(value)
        return deadline is not None and deadline <= (now or datetime.now(timezone.utc))

    def _is_game_session_lock_expired(self, lock: GameSessionLock, now: datetime | None = None) -> bool:
        try:
            expires_at = datetime.fromisoformat(lock.lease_expires_at)
        except ValueError:
            return True
        if expires_at.tzinfo is None:
            expires_at = expires_at.replace(tzinfo=timezone.utc)
        return expires_at <= (now or datetime.now(timezone.utc))

    def session_temp_path(self, session: LinkSession) -> Path:
        return self.storage.resolve_relative(session.temp_dir)

    def record_event(self, session_id: str, event_type: str, payload: dict[str, Any]) -> None:
        timestamp = now_iso()
        event = {
            "session_id": session_id,
            "event_type": event_type,
            "payload": payload,
            "created_at": timestamp,
        }
        self.storage.append_session_event(
            event, coalesce=event_type in COALESCED_EVENT_TYPES
        )

    @staticmethod
    def _coalesced_event_key(event: dict[str, Any]) -> tuple[str, str, str] | None:
        event_type = str(event.get("event_type", ""))
        if event_type not in COALESCED_EVENT_TYPES:
            return None
        payload = event.get("payload")
        user_id = str(payload.get("user_id", "")) if isinstance(payload, dict) else ""
        return str(event.get("session_id", "")), event_type, user_id

    @classmethod
    def _compact_coalesced_events(cls, events: dict[str, Any]) -> dict[str, Any]:
        compacted: dict[str, Any] = {}
        grouped: dict[tuple[str, str, str], list[tuple[str, dict[str, Any]]]] = {}
        for event_id, raw_event in events.items():
            if not isinstance(raw_event, dict):
                compacted[event_id] = raw_event
                continue
            key = cls._coalesced_event_key(raw_event)
            if key is None:
                compacted[event_id] = raw_event
                continue
            grouped.setdefault(key, []).append((event_id, raw_event))
        for records in grouped.values():
            latest_id, latest = max(
                records,
                key=lambda item: str(item[1].get("created_at", "")),
            )
            merged = dict(latest)
            created_values = [
                str(event.get("first_created_at") or event.get("created_at") or "")
                for _event_id, event in records
            ]
            merged["id"] = latest_id
            nonempty_created_values = [value for value in created_values if value]
            merged["first_created_at"] = (
                min(nonempty_created_values)
                if nonempty_created_values
                else str(latest.get("created_at", ""))
            )
            merged["occurrences"] = sum(
                max(1, int(event.get("occurrences", 1))) for _event_id, event in records
            )
            compacted[latest_id] = merged
        return compacted

    def require_active(self, session: LinkSession) -> None:
        if LinkSessionStatus(session.status) in TERMINAL_STATUSES:
            raise ValidationError("link session is already ended")
