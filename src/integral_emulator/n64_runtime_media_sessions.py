# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Authenticated N64 Runtime remote-2P media-session metadata and tickets."""

from __future__ import annotations

import hashlib
import hmac
from dataclasses import dataclass, replace
from datetime import datetime, timezone
from typing import Any

from .errors import NotFoundError, ValidationError
from .models import now_iso
from .security import issue_secret
from .storage import LeagueStorage
from .room_session_policy import RoomSessionPolicy


MEDIA_TICKET_SCOPE = "n64_runtime_media"
N64_RUNTIME_MEDIA_SESSION_TTL_MINUTES = 30
ACTIVE_MEDIA_STATUSES = {"CREATED", "WAITING_PEER", "READY", "RUNNING"}
N64_RUNTIME_MEDIA_SESSION_LOCK = "n64_runtime_media_session"


@dataclass(frozen=True)
class N64RuntimeMediaSession:
    id: str
    room_number: int
    host_user_id: str
    remote_user_id: str
    host_n64_slot: str
    host_gb_slot: str
    remote_gb_slot: str
    host_n64_rom_id: str
    host_gb_rom_id: str
    remote_gb_rom_id: str
    host_n64_save_id: str
    host_save_id: str
    remote_save_id: str
    game_type: str
    status: str
    created_at: str
    updated_at: str
    expires_at: str
    started_at: str | None = None
    termination_reason: str | None = None
    no_save: bool = True
    ticket_scope: str = MEDIA_TICKET_SCOPE
    host_ticket_sha256: str | None = None
    remote_ticket_sha256: str | None = None
    host_ticket_used: bool = False
    remote_ticket_used: bool = False
    row_version: int = 0

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "N64RuntimeMediaSession":
        normalized = {field: data.get(field) for field in cls.__dataclass_fields__}
        normalized["no_save"] = bool(data.get("no_save", True))
        normalized["ticket_scope"] = str(data.get("ticket_scope") or MEDIA_TICKET_SCOPE)
        normalized["host_ticket_used"] = bool(data.get("host_ticket_used", False))
        normalized["remote_ticket_used"] = bool(data.get("remote_ticket_used", False))
        normalized["row_version"] = int(data.get("__row_version", data.get("row_version", 0)))
        if not isinstance(normalized.get("expires_at"), str) or not normalized["expires_at"]:
            raise ValidationError("stored N64 Runtime media expiry is invalid")
        return cls(**normalized)

    def to_storage_dict(self) -> dict[str, Any]:
        data = self.__dict__.copy()
        data.pop("row_version", None)
        return data

    def to_public_dict(self, role: str) -> dict[str, Any]:
        return {
            "id": self.id,
            "room_number": self.room_number,
            "role": role,
            "host_n64_slot": self.host_n64_slot,
            "host_gb_slot": self.host_gb_slot,
            "remote_gb_slot": self.remote_gb_slot,
            "game_type": self.game_type,
            "status": self.status,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "expires_at": self.expires_at,
            "started_at": self.started_at,
            "termination_reason": self.termination_reason,
            "no_save": self.no_save,
            "ticket_scope": self.ticket_scope,
        }


class N64RuntimeMediaSessionManager:
    def __init__(self, storage: LeagueStorage, policy: RoomSessionPolicy | None = None):
        self.storage = storage
        self.policy = policy or RoomSessionPolicy.from_environment()

    def create_or_get(
        self,
        *,
        room_number: int,
        host_user_id: str,
        remote_user_id: str,
        host_n64_slot: str,
        host_gb_slot: str,
        remote_gb_slot: str,
        host_n64_rom_id: str,
        host_gb_rom_id: str,
        remote_gb_rom_id: str,
        host_n64_save_id: str,
        host_save_id: str,
        remote_save_id: str,
        game_type: str,
    ) -> N64RuntimeMediaSession:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            current = self._find_active_for_room_locked(records, room_number)
            if current:
                expected = (
                host_user_id,
                remote_user_id,
                host_n64_slot,
                host_gb_slot,
                remote_gb_slot,
                host_n64_rom_id,
                host_gb_rom_id,
                remote_gb_rom_id,
                host_n64_save_id,
                host_save_id,
                remote_save_id,
                game_type,
            )
                actual = (
                current.host_user_id,
                current.remote_user_id,
                current.host_n64_slot,
                current.host_gb_slot,
                current.remote_gb_slot,
                current.host_n64_rom_id,
                current.host_gb_rom_id,
                current.remote_gb_rom_id,
                current.host_n64_save_id,
                current.host_save_id,
                current.remote_save_id,
                current.game_type,
            )
                if actual != expected:
                    raise ValidationError("N64 room already has a different active media session")
                return current
            timestamp = now_iso()
            started = datetime.fromisoformat(timestamp)
            session = N64RuntimeMediaSession(
                id=issue_secret("n64runtime_media"),
                room_number=room_number,
                host_user_id=host_user_id,
                remote_user_id=remote_user_id,
                host_n64_slot=host_n64_slot,
                host_gb_slot=host_gb_slot,
                remote_gb_slot=remote_gb_slot,
                host_n64_rom_id=host_n64_rom_id,
                host_gb_rom_id=host_gb_rom_id,
                remote_gb_rom_id=remote_gb_rom_id,
                host_n64_save_id=host_n64_save_id,
                host_save_id=host_save_id,
                remote_save_id=remote_save_id,
                game_type=game_type,
                status="CREATED",
                created_at=timestamp,
                updated_at=timestamp,
                expires_at=self.policy.deadline(started, self.policy.n64_runtime_seconds),
                started_at=timestamp,
            )
            self._save_locked(records, session)
            return session

    def get(self, session_id: str) -> N64RuntimeMediaSession:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            session = self._get_locked(records, session_id)
            if self._expired(session) and session.status in ACTIVE_MEDIA_STATUSES:
                session = replace(session, status="EXPIRED", updated_at=now_iso())
                self._save_locked(records, session)
            return session

    def find_active_for_room(self, room_number: int) -> N64RuntimeMediaSession | None:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            return self._find_active_for_room_locked(records, room_number)

    def active_for_user(self, user_id: str) -> list[N64RuntimeMediaSession]:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            sessions: list[N64RuntimeMediaSession] = []
            for session_id, data in records.items():
                session = N64RuntimeMediaSession.from_dict(data)
                if session.status not in ACTIVE_MEDIA_STATUSES or user_id not in {
                    session.host_user_id,
                    session.remote_user_id,
                }:
                    continue
                if self._expired(session):
                    expired = replace(
                        session, status="EXPIRED", updated_at=now_iso()
                    )
                    self._save_locked(records, expired)
                    continue
                sessions.append(session)
            return sessions

    def active_sessions(self) -> list[N64RuntimeMediaSession]:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            sessions: list[N64RuntimeMediaSession] = []
            for session_id, data in records.items():
                session = N64RuntimeMediaSession.from_dict(data)
                if session.status not in ACTIVE_MEDIA_STATUSES:
                    continue
                if self._expired(session):
                    expired = replace(
                        session, status="EXPIRED", updated_at=now_iso()
                    )
                    self._save_locked(records, expired)
                    continue
                sessions.append(session)
            return sessions

    def all_sessions(self) -> list[N64RuntimeMediaSession]:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            return [
                self._ensure_deadlines(N64RuntimeMediaSession.from_dict(data))
                for data in records.values()
            ]

    def cancel(self, session_id: str, reason: str = "cancelled") -> N64RuntimeMediaSession:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            session = self._get_locked(records, session_id)
            if session.status in ACTIVE_MEDIA_STATUSES:
                session = replace(
                    session,
                    status="CANCELLED",
                    updated_at=now_iso(),
                    termination_reason=reason,
                )
                self._save_locked(records, session)
            return session

    def expire(self, session_id: str, reason: str) -> N64RuntimeMediaSession:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            session = self._get_locked(records, session_id)
            if session.status in ACTIVE_MEDIA_STATUSES:
                session = replace(
                    session,
                    status="EXPIRED",
                    updated_at=now_iso(),
                    termination_reason=reason,
                )
                self._save_locked(records, session)
            return session

    def renew(self, session_id: str) -> N64RuntimeMediaSession:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            session = self._get_locked(records, session_id)
            if session.status not in ACTIVE_MEDIA_STATUSES or self._expired(session):
                if session.status in ACTIVE_MEDIA_STATUSES:
                    session = replace(session, status="EXPIRED", updated_at=now_iso())
                    self._save_locked(records, session)
                raise ValidationError("N64 Runtime media session is not active")
            timestamp = now_iso()
            renewed = replace(
                session,
                updated_at=timestamp,
            )
            self._save_locked(records, renewed)
            return renewed

    def role_for_user(self, session: N64RuntimeMediaSession, user_id: str) -> str:
        if hmac.compare_digest(session.host_user_id, user_id):
            return "host"
        if hmac.compare_digest(session.remote_user_id, user_id):
            return "remote"
        raise ValidationError("N64 Runtime media session access denied")

    def runtime_save_binding(
        self, session_id: str, user_id: str, kind: str
    ) -> tuple[N64RuntimeMediaSession, str, str]:
        session = self.get(session_id)
        if session.status not in ACTIVE_MEDIA_STATUSES or not session.no_save:
            raise ValidationError("N64 Runtime no-save runtime is not active")
        if self.role_for_user(session, user_id) != "host":
            raise ValidationError("N64 Runtime runtime save access requires host role")
        bindings = {
            "n64": (session.host_n64_save_id, session.host_user_id),
            "host-gb": (session.host_save_id, session.host_user_id),
            "remote-gb": (session.remote_save_id, session.remote_user_id),
        }
        binding = bindings.get(kind)
        if binding is None:
            raise ValidationError("N64 Runtime runtime save kind is invalid")
        return session, binding[0], binding[1]

    def issue_ticket(self, session_id: str, user_id: str) -> tuple[N64RuntimeMediaSession, str, str]:
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            session = self._get_locked(records, session_id)
            if self._expired(session) and session.status in ACTIVE_MEDIA_STATUSES:
                session = replace(session, status="EXPIRED", updated_at=now_iso())
                self._save_locked(records, session)
            if session.status not in ACTIVE_MEDIA_STATUSES:
                raise ValidationError("N64 Runtime media session is not active")
            role = self.role_for_user(session, user_id)
            ticket = issue_secret("s64ticket")
            digest = hashlib.sha256(ticket.encode("utf-8")).hexdigest()
            updates = {
                f"{role}_ticket_sha256": digest,
                f"{role}_ticket_used": False,
                "status": "WAITING_PEER",
                "updated_at": now_iso(),
            }
            session = replace(session, **updates)
            self._save_locked(records, session)
            return session, role, ticket

    def validate_ticket(self, session_id: str, role: str, ticket: str, consume: bool = True) -> bool:
        if role not in {"host", "remote"} or not ticket:
            return False
        with self.storage.exclusive_lock(N64_RUNTIME_MEDIA_SESSION_LOCK):
            records = self.storage.load_n64_runtime_media_sessions()
            try:
                session = self._get_locked(records, session_id)
            except NotFoundError:
                return False
            if self._expired(session) and session.status in ACTIVE_MEDIA_STATUSES:
                session = replace(session, status="EXPIRED", updated_at=now_iso())
                self._save_locked(records, session)
            if session.status not in ACTIVE_MEDIA_STATUSES or session.ticket_scope != MEDIA_TICKET_SCOPE:
                return False
            expected = getattr(session, f"{role}_ticket_sha256")
            used = getattr(session, f"{role}_ticket_used")
            actual = hashlib.sha256(ticket.encode("utf-8")).hexdigest()
            if not expected or used or not hmac.compare_digest(expected, actual):
                return False
            if consume:
                host_used = session.host_ticket_used or role == "host"
                remote_used = session.remote_ticket_used or role == "remote"
                session = replace(
                    session,
                    **{
                        f"{role}_ticket_used": True,
                        "status": "RUNNING" if host_used and remote_used else "READY",
                        "updated_at": now_iso(),
                    },
                )
                self._save_locked(records, session)
            return True

    def _get_locked(self, records: dict[str, Any], session_id: str) -> N64RuntimeMediaSession:
        data = records.get(session_id)
        if not data:
            raise NotFoundError("N64 Runtime media session not found")
        return N64RuntimeMediaSession.from_dict(data)

    def _ensure_deadlines(self, session: N64RuntimeMediaSession) -> N64RuntimeMediaSession:
        base = self._parse_time(session.started_at or session.created_at) or datetime.now(timezone.utc)
        updates: dict[str, Any] = {}
        if not session.started_at:
            updates["started_at"] = base.isoformat()
        return replace(session, **updates) if updates else session

    @staticmethod
    def _parse_time(value: str | None) -> datetime | None:
        if not value:
            return None
        try:
            parsed = datetime.fromisoformat(value)
        except ValueError:
            return None
        return parsed.replace(tzinfo=parsed.tzinfo or timezone.utc)

    def _find_active_for_room_locked(
        self, records: dict[str, Any], room_number: int
    ) -> N64RuntimeMediaSession | None:
        current: N64RuntimeMediaSession | None = None
        for data in records.values():
            session = N64RuntimeMediaSession.from_dict(data)
            if session.room_number != room_number or session.status not in ACTIVE_MEDIA_STATUSES:
                continue
            if self._expired(session):
                expired = replace(session, status="EXPIRED", updated_at=now_iso())
                self._save_locked(records, expired)
                continue
            current = session
            break
        return current

    def _save_locked(self, records: dict[str, Any], session: N64RuntimeMediaSession) -> None:
        if session.row_version:
            self.storage.update_n64_runtime_media_session(
                session.to_storage_dict(), session.row_version
            )
        else:
            self.storage.insert_n64_runtime_media_session(session.to_storage_dict())

    def _expired(self, session: N64RuntimeMediaSession) -> bool:
        try:
            expires_at = datetime.fromisoformat(session.expires_at)
        except ValueError:
            return True
        if expires_at.tzinfo is None:
            expires_at = expires_at.replace(tzinfo=timezone.utc)
        return expires_at <= datetime.now(timezone.utc)
