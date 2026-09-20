# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Persistent Phase-2 control plane for fixed-Host GB sessions."""

from __future__ import annotations

import hashlib
import hmac
import logging
import math
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
from typing import Any

from .errors import NotFoundError, ValidationError
from .gb_runtime_fixed_host_protocol import (
    GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
    GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
    GB_RUNTIME_FIXED_HOST_RELAY_TICKET_MAX_SECONDS,
    GBRuntimeFixedHostManifest,
)
from .models import now_iso
from .rom_metadata import normalize_rom_header_title, normalize_rom_platform
from .save_contract import normalize_game_type
from .security import issue_secret
from .storage import LeagueStorage


GB_RUNTIME_FIXED_HOST_SESSION_LOCK = "gb-runtime-fixed-host-session"
GB_RUNTIME_FIXED_HOST_REMOTE_RESUME_SECONDS = 25
GB_RUNTIME_FIXED_HOST_ACTIVE_STATES = {
    "PREFLIGHT", "READY", "BLOCKED", "WAITING_PEER", "RUNNING", "PAUSED_REMOTE", "FINALIZING"
}


@dataclass(frozen=True)
class GBRuntimeFixedHostSession:
    id: str
    room_number: int
    manifest: dict[str, Any]
    manifest_digest: str
    state: str
    created_at: str
    updated_at: str
    preflights: dict[str, dict[str, Any]]
    host_ticket_sha256: str | None = None
    remote_ticket_sha256: str | None = None
    host_ticket_expires_at: str | None = None
    remote_ticket_expires_at: str | None = None
    host_ticket_used: bool = False
    remote_ticket_used: bool = False
    pause_expires_at: str | None = None
    remote_disconnect_count: int = 0
    termination_reason: str | None = None
    blocked_reason: str | None = None
    host_finish: dict[str, Any] | None = None
    remote_receipt: dict[str, Any] | None = None
    commit_result: dict[str, Any] | None = None
    row_version: int = 0

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "GBRuntimeFixedHostSession":
        normalized = {field: data.get(field) for field in cls.__dataclass_fields__}
        normalized["preflights"] = dict(data.get("preflights") or {})
        normalized["host_ticket_used"] = bool(data.get("host_ticket_used", False))
        normalized["remote_ticket_used"] = bool(data.get("remote_ticket_used", False))
        normalized["remote_disconnect_count"] = int(data.get("remote_disconnect_count", 0))
        normalized["host_finish"] = dict(data["host_finish"]) if data.get("host_finish") else None
        normalized["remote_receipt"] = dict(data["remote_receipt"]) if data.get("remote_receipt") else None
        normalized["commit_result"] = dict(data["commit_result"]) if data.get("commit_result") else None
        normalized["row_version"] = int(data.get("__row_version", data.get("row_version", 0)))
        return cls(**normalized)

    def to_storage_dict(self) -> dict[str, Any]:
        data = self.__dict__.copy()
        data.pop("row_version", None)
        return data

    def to_public_dict(self, user_id: str) -> dict[str, Any]:
        manifest = self.manifest_object()
        pause_remaining_seconds = 0
        if self.state == "PAUSED_REMOTE" and self.pause_expires_at:
            try:
                deadline = datetime.fromisoformat(self.pause_expires_at)
                if deadline.tzinfo is None:
                    deadline = deadline.replace(tzinfo=timezone.utc)
                pause_remaining_seconds = max(
                    0, math.ceil((deadline - datetime.now(timezone.utc)).total_seconds())
                )
            except ValueError:
                pause_remaining_seconds = 0
        return {
            "id": self.id,
            "room_number": self.room_number,
            "role": manifest.role_for(user_id),
            "protocol_id": GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
            "media_scope": GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
            "manifest": self.manifest,
            "manifest_digest": self.manifest_digest,
            "state": self.state,
            "preflight_roles": sorted(self.preflights),
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "termination_reason": self.termination_reason,
            "blocked_reason": self.blocked_reason,
            "pause_expires_at": self.pause_expires_at,
            "pause_remaining_seconds": pause_remaining_seconds,
            "remote_disconnect_count": self.remote_disconnect_count,
            "finish_roles": sorted(
                role for role, value in (
                    ("host", self.host_finish), ("remote", self.remote_receipt)
                ) if value
            ),
            "commit_result": self.commit_result,
        }

    def manifest_object(self) -> GBRuntimeFixedHostManifest:
        return GBRuntimeFixedHostManifest(**self.manifest).validated()


class GBRuntimeFixedHostSessionManager:
    def __init__(self, storage: LeagueStorage):
        self.storage = storage

    def create(self, room_number: int, manifest: GBRuntimeFixedHostManifest) -> GBRuntimeFixedHostSession:
        manifest = manifest.validated()
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            existing = self._active_for_room_locked(records, room_number)
            if existing:
                if existing.manifest_digest != manifest.digest:
                    raise ValidationError("fixed Host room already has another session")
                return existing
            timestamp = now_iso()
            session = GBRuntimeFixedHostSession(
                id=manifest.session_id,
                room_number=room_number,
                manifest=manifest.__dict__.copy(),
                manifest_digest=manifest.digest,
                state="PREFLIGHT",
                created_at=timestamp,
                updated_at=timestamp,
                preflights={},
            )
            self._save_locked(records, session)
            return self.get(manifest.session_id)

    def get(self, session_id: str) -> GBRuntimeFixedHostSession:
        records = self.storage.load_gb_runtime_fixed_host_sessions()
        data = records.get(session_id)
        if not data:
            raise NotFoundError("fixed Host session not found")
        session = GBRuntimeFixedHostSession.from_dict(data)
        session.manifest_object()
        if not hmac.compare_digest(session.manifest_digest, session.manifest_object().digest):
            raise ValidationError("fixed Host stored manifest digest mismatch")
        if session.state == "PAUSED_REMOTE" and self._pause_expired(session):
            return self.expire_remote_pause(session_id)
        return session

    def active_for_user(self, user_id: str) -> list[GBRuntimeFixedHostSession]:
        sessions = []
        for data in self.storage.load_gb_runtime_fixed_host_sessions().values():
            session = GBRuntimeFixedHostSession.from_dict(data)
            manifest = session.manifest_object()
            if session.state in GB_RUNTIME_FIXED_HOST_ACTIVE_STATES and user_id in {
                manifest.host_user_id,
                manifest.remote_user_id,
            }:
                sessions.append(session)
        return sessions

    def block_preflight(self, session_id: str, user_id: str, reason: str) -> GBRuntimeFixedHostSession:
        if reason not in {"rtc_save_required", "rom_unreadable"}:
            raise ValidationError("invalid fixed Host blocked reason")
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.manifest_object().role_for(user_id) != "host":
                raise ValidationError("only Host can report SAV preflight")
            if session.state not in {"PREFLIGHT", "READY", "BLOCKED"}:
                raise ValidationError("fixed Host preflight is closed")
            previous_state = session.state
            session = replace(session, state="BLOCKED", blocked_reason=reason, updated_at=now_iso())
            self._save_locked(records, session)
            logging.getLogger(__name__).info(
                "fixed_host_lifecycle session=%s role=host from=%s to=BLOCKED blocked_reason=%s",
                session.id, previous_state, reason,
            )
            return session

    def submit_preflight(
        self,
        session_id: str,
        user_id: str,
        *,
        manifest_digest: str,
        protocol_id: str,
        runtime_build_id: str,
        available_roms: list[dict[str, str]],
    ) -> GBRuntimeFixedHostSession:
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state not in {"PREFLIGHT", "READY"}:
                raise ValidationError("fixed Host preflight is closed")
            manifest = session.manifest_object()
            role = manifest.role_for(user_id)
            if (
                protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID
                or runtime_build_id != manifest.runtime_build_id
            ):
                raise ValidationError("fixed Host client preflight is invalid")
            if not hmac.compare_digest(manifest_digest, session.manifest_digest):
                raise ValidationError("fixed Host preflight manifest mismatch")
            normalized_roms = []
            for item in available_roms:
                if set(item) != {"game_type", "platform", "rom_header_title"} or not all(
                    isinstance(item.get(field), str)
                    for field in ("game_type", "platform", "rom_header_title")
                ):
                    raise ValidationError("fixed Host available ROM metadata is invalid")
                try:
                    game_type = normalize_game_type(item["game_type"])
                    platform = normalize_rom_platform(item["platform"])
                    header = normalize_rom_header_title(item["rom_header_title"])
                except (ValueError, ValidationError) as error:
                    raise ValidationError("fixed Host available ROM metadata is invalid") from error
                if (game_type, platform, header) != (
                    item["game_type"], item["platform"], item["rom_header_title"]
                ):
                    raise ValidationError("fixed Host available ROM metadata is invalid")
                normalized_roms.append((game_type, platform, header))
            normalized_roms = sorted(set(normalized_roms))
            required = (
                {
                    (
                        manifest.host_game_type,
                        manifest.host_platform,
                        manifest.host_rom_header_title,
                    ),
                    (
                        manifest.remote_game_type,
                        manifest.remote_platform,
                        manifest.remote_rom_header_title,
                    ),
                }
                if role == "host"
                else set()
            )
            if not required.issubset(set(normalized_roms)):
                raise ValidationError("fixed Host client is missing required ROM metadata")
            roms = [
                {
                    "game_type": game_type,
                    "platform": platform,
                    "rom_header_title": header,
                }
                for game_type, platform, header in normalized_roms
            ]
            claim = {
                "role": role,
                "user_id": user_id,
                "protocol_id": protocol_id,
                "runtime_build_id": runtime_build_id,
                "manifest_digest": manifest_digest,
                "available_roms": roms,
            }
            preflights = dict(session.preflights)
            if role in preflights and preflights[role] != claim:
                raise ValidationError("fixed Host client preflight is immutable")
            preflights[role] = claim
            session = replace(
                session,
                preflights=preflights,
                state="READY" if set(preflights) == {"host", "remote"} else "PREFLIGHT",
                updated_at=now_iso(),
            )
            self._save_locked(records, session)
            return self.get(session_id)

    def issue_ticket(
        self, session_id: str, user_id: str, ttl_seconds: int = 30, *, resume: bool = False
    ) -> tuple[GBRuntimeFixedHostSession, str, str]:
        if not 1 <= ttl_seconds <= GB_RUNTIME_FIXED_HOST_RELAY_TICKET_MAX_SECONDS:
            raise ValidationError("invalid fixed Host relay ticket lifetime")
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state == "PAUSED_REMOTE" and self._pause_expired(session):
                session = replace(
                    session,
                    state="ABORTED",
                    termination_reason="remote resume timeout",
                    pause_expires_at=None,
                    updated_at=now_iso(),
                )
                self._save_locked(records, session)
                raise ValidationError("fixed Host remote resume timed out")
            role = session.manifest_object().role_for(user_id)
            if resume and session.state in {"RUNNING", "WAITING_PEER", "PAUSED_REMOTE"}:
                session = replace(session, state="PAUSED_REMOTE", pause_expires_at=(
                    session.pause_expires_at or (datetime.now(timezone.utc) + timedelta(
                        seconds=GB_RUNTIME_FIXED_HOST_REMOTE_RESUME_SECONDS)).isoformat()))
            if session.state not in {"READY", "WAITING_PEER", "PAUSED_REMOTE"}:
                raise ValidationError("fixed Host session is not ready")
            resuming_remote = session.state == "PAUSED_REMOTE" and (role == "remote" or resume)
            if session.state == "PAUSED_REMOTE" and not resuming_remote:
                raise ValidationError("only the Remote may resume a paused fixed Host session")
            if getattr(session, f"{role}_ticket_sha256") is not None and not resuming_remote:
                raise ValidationError("fixed Host relay ticket already issued")
            token = issue_secret("gbfixedticket")
            expires_at = (datetime.now(timezone.utc) + timedelta(seconds=ttl_seconds)).isoformat()
            session = replace(
                session,
                **{
                    f"{role}_ticket_sha256": hashlib.sha256(token.encode()).hexdigest(),
                    f"{role}_ticket_expires_at": expires_at,
                    f"{role}_ticket_used": False,
                    "state": "PAUSED_REMOTE" if resuming_remote else "WAITING_PEER",
                    "updated_at": now_iso(),
                },
            )
            self._save_locked(records, session)
            return session, role, token

    def validate_ticket(
        self, session_id: str, role: str, scope: str, token: str, *, consume: bool = True
    ) -> bool:
        if role not in {"host", "remote"} or scope != GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE or not token:
            return False
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            try:
                session = self._get_locked(records, session_id)
            except (NotFoundError, ValidationError):
                return False
            if session.state not in GB_RUNTIME_FIXED_HOST_ACTIVE_STATES:
                return False
            if session.pause_expires_at and self._pause_expired(session):
                return False
            expected = getattr(session, f"{role}_ticket_sha256")
            expires_text = getattr(session, f"{role}_ticket_expires_at")
            used = getattr(session, f"{role}_ticket_used")
            try:
                expires_at = datetime.fromisoformat(expires_text or "")
            except ValueError:
                return False
            if expires_at.tzinfo is None:
                expires_at = expires_at.replace(tzinfo=timezone.utc)
            actual = hashlib.sha256(token.encode()).hexdigest()
            if used or not expected or datetime.now(timezone.utc) >= expires_at or not hmac.compare_digest(expected, actual):
                return False
            if consume:
                host_used = session.host_ticket_used or role == "host"
                remote_used = session.remote_ticket_used or role == "remote"
                session = replace(
                    session,
                    **{
                        f"{role}_ticket_used": True,
                        "state": "RUNNING" if host_used and remote_used else (
                            "PAUSED_REMOTE" if session.pause_expires_at else "WAITING_PEER"
                        ),
                        "pause_expires_at": None if host_used and remote_used else session.pause_expires_at,
                        "updated_at": now_iso(),
                    },
                )
                self._save_locked(records, session)
            return True

    def remote_disconnected(
        self, session_id: str, resume_seconds: int = GB_RUNTIME_FIXED_HOST_REMOTE_RESUME_SECONDS
    ) -> GBRuntimeFixedHostSession:
        if resume_seconds <= 0:
            raise ValidationError("fixed Host resume timeout must be positive")
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state != "RUNNING":
                return session
            expires_at = (
                datetime.now(timezone.utc) + timedelta(seconds=resume_seconds)
            ).isoformat()
            session = replace(
                session,
                state="PAUSED_REMOTE",
                pause_expires_at=expires_at,
                remote_ticket_sha256=None,
                remote_ticket_expires_at=None,
                remote_ticket_used=False,
                remote_disconnect_count=session.remote_disconnect_count + 1,
                updated_at=now_iso(),
            )
            self._save_locked(records, session)
            return session

    def expire_remote_pause(self, session_id: str) -> GBRuntimeFixedHostSession:
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state != "PAUSED_REMOTE":
                return session
            if not self._pause_expired(session):
                return session
            session = replace(
                session,
                state="ABORTED",
                termination_reason="remote resume timeout",
                pause_expires_at=None,
                updated_at=now_iso(),
            )
            self._save_locked(records, session)
            return session

    @staticmethod
    def _pause_expired(session: GBRuntimeFixedHostSession) -> bool:
        try:
            deadline = datetime.fromisoformat(session.pause_expires_at or "")
        except ValueError:
            return True
        if deadline.tzinfo is None:
            deadline = deadline.replace(tzinfo=timezone.utc)
        return datetime.now(timezone.utc) >= deadline

    def peer_disconnected(self, session_id: str, role: str) -> GBRuntimeFixedHostSession:
        if role == "remote":
            return self.remote_disconnected(session_id)
        if role != "host":
            raise ValidationError("invalid fixed Host disconnect role")
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state not in {"READY", "WAITING_PEER", "RUNNING", "PAUSED_REMOTE"}:
                return session
            session = self._pause_transport(session)
            self._save_locked(records, session)
            return session

    @staticmethod
    def _pause_transport(session: GBRuntimeFixedHostSession) -> GBRuntimeFixedHostSession:
        return replace(session, state="PAUSED_REMOTE",
                       pause_expires_at=session.pause_expires_at or (
                           datetime.now(timezone.utc) + timedelta(
                               seconds=GB_RUNTIME_FIXED_HOST_REMOTE_RESUME_SECONDS)).isoformat(),
                       host_ticket_sha256=None, remote_ticket_sha256=None,
                       host_ticket_expires_at=None, remote_ticket_expires_at=None,
                       host_ticket_used=False, remote_ticket_used=False, updated_at=now_iso())

    def cancel(self, session_id: str, reason: str) -> GBRuntimeFixedHostSession:
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state in GB_RUNTIME_FIXED_HOST_ACTIVE_STATES:
                session = replace(
                    session,
                    state="ABORTED",
                    termination_reason=reason,
                    updated_at=now_iso(),
                )
                self._save_locked(records, session)
            return session

    def submit_host_finish(
        self,
        session_id: str,
        user_id: str,
        *,
        terminal_digest: str,
        final_frame: int,
        host_candidate_sha256: str,
        remote_candidate_sha256: str,
    ) -> GBRuntimeFixedHostSession:
        claim = {
            "terminal_digest": self._require_digest(terminal_digest),
            "final_frame": self._require_frame(final_frame),
            "host_candidate_sha256": self._require_digest(host_candidate_sha256),
            "remote_candidate_sha256": self._require_digest(remote_candidate_sha256),
        }
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            manifest = session.manifest_object()
            if manifest.role_for(user_id) != "host":
                raise ValidationError("fixed Host finish requires host role")
            if manifest.save_policy != "commit_pair":
                raise ValidationError("fixed Host Battle cannot submit save candidates")
            if session.state == "FINISHED" and session.host_finish == claim:
                return session
            if session.state not in {"RUNNING", "FINALIZING"}:
                raise ValidationError("fixed Host session is not accepting finish receipts")
            if session.host_finish and session.host_finish != claim:
                raise ValidationError("fixed Host finish is immutable")
            session = replace(
                session,
                host_finish=claim,
                state="FINALIZING",
                updated_at=now_iso(),
            )
            self._save_locked(records, session)
            return session

    def submit_remote_receipt(
        self,
        session_id: str,
        user_id: str,
        *,
        terminal_digest: str,
        final_frame: int,
    ) -> GBRuntimeFixedHostSession:
        claim = {
            "terminal_digest": self._require_digest(terminal_digest),
            "final_frame": self._require_frame(final_frame),
        }
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            manifest = session.manifest_object()
            if manifest.role_for(user_id) != "remote":
                raise ValidationError("fixed Host terminal receipt requires remote role")
            if manifest.save_policy != "commit_pair":
                raise ValidationError("fixed Host Battle cannot submit terminal receipt")
            if session.state == "FINISHED" and session.remote_receipt == claim:
                return session
            if session.state not in {"RUNNING", "FINALIZING"}:
                raise ValidationError("fixed Host session is not accepting finish receipts")
            if session.remote_receipt and session.remote_receipt != claim:
                raise ValidationError("fixed Host terminal receipt is immutable")
            if session.host_finish and not self._receipts_match(session.host_finish, claim):
                raise ValidationError("fixed Host terminal receipts do not match")
            session = replace(
                session,
                remote_receipt=claim,
                state="FINALIZING",
                updated_at=now_iso(),
            )
            self._save_locked(records, session)
            return session

    def ready_to_commit(self, session_id: str) -> bool:
        session = self.get(session_id)
        return bool(
            session.host_finish
            and session.remote_receipt
            and self._receipts_match(session.host_finish, session.remote_receipt)
        )

    def complete(self, session_id: str, result: dict[str, Any]) -> GBRuntimeFixedHostSession:
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            session = self._get_locked(records, session_id)
            if session.state == "FINISHED":
                if session.commit_result != result:
                    raise ValidationError("fixed Host commit result is immutable")
                return session
            if session.state != "FINALIZING" or not session.host_finish or not session.remote_receipt:
                raise ValidationError("fixed Host session is not ready to complete")
            if not self._receipts_match(session.host_finish, session.remote_receipt):
                raise ValidationError("fixed Host terminal receipts do not match")
            session = replace(
                session,
                state="FINISHED",
                commit_result=dict(result),
                termination_reason="normal",
                updated_at=now_iso(),
            )
            self._save_locked(records, session)
            return self.get(session_id)

    @staticmethod
    def _require_digest(value: str) -> str:
        if not isinstance(value, str) or len(value) != 64:
            raise ValidationError("invalid fixed Host digest")
        try:
            bytes.fromhex(value)
        except ValueError as exc:
            raise ValidationError("invalid fixed Host digest") from exc
        return value.lower()

    @staticmethod
    def _require_frame(value: int) -> int:
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise ValidationError("invalid fixed Host final frame")
        return value

    @staticmethod
    def _receipts_match(host: dict[str, Any], remote: dict[str, Any]) -> bool:
        return (
            hmac.compare_digest(host["terminal_digest"], remote["terminal_digest"])
            and host["final_frame"] == remote["final_frame"]
        )

    def session_is_active(self, session_id: str) -> bool:
        try:
            return self.expire_remote_pause(session_id).state in GB_RUNTIME_FIXED_HOST_ACTIVE_STATES
        except (NotFoundError, ValidationError):
            return False

    def pause_relay_orphans(self) -> list[str]:
        """Give existing processes one bounded recovery window after restart."""
        aborted: list[str] = []
        with self.storage.exclusive_lock(GB_RUNTIME_FIXED_HOST_SESSION_LOCK):
            records = self.storage.load_gb_runtime_fixed_host_sessions()
            for session_id, data in list(records.items()):
                session = GBRuntimeFixedHostSession.from_dict(data)
                transport_started = session.state in {"RUNNING", "PAUSED_REMOTE"} or (
                    session.state == "WAITING_PEER" and
                    (session.host_ticket_used or session.remote_ticket_used)
                )
                if not transport_started:
                    continue
                session = self._pause_transport(session)
                self._save_locked(records, session)
                aborted.append(session_id)
        return aborted

    def _get_locked(self, records: dict[str, Any], session_id: str) -> GBRuntimeFixedHostSession:
        data = records.get(session_id)
        if not data:
            raise NotFoundError("fixed Host session not found")
        return GBRuntimeFixedHostSession.from_dict(data)

    def _active_for_room_locked(
        self, records: dict[str, Any], room_number: int
    ) -> GBRuntimeFixedHostSession | None:
        for data in records.values():
            session = GBRuntimeFixedHostSession.from_dict(data)
            if session.room_number == room_number and session.state in GB_RUNTIME_FIXED_HOST_ACTIVE_STATES:
                return session
        return None

    def _save_locked(
        self, records: dict[str, Any], session: GBRuntimeFixedHostSession
    ) -> None:
        if session.row_version:
            self.storage.update_gb_runtime_fixed_host_session(
                session.to_storage_dict(), session.row_version
            )
        else:
            self.storage.insert_gb_runtime_fixed_host_session(session.to_storage_dict())
