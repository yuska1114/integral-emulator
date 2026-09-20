# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Game-agnostic GB Mobile v2 package and session lifecycle service."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import re
from typing import Any, Callable

from ..errors import (
    GameSessionExpiredError,
    MobileCreateAbortedError,
    MobileCreateAuthSessionConflictError,
    NotFoundError,
    RevisionConflictError,
    ValidationError,
)
from ..models import now_iso
from ..sqlite_assets import SQLiteSaveManager
from ..security import issue_secret
from ..sessions import LinkSessionManager
from ..sqlite_repositories import (
    RepositoryConflictError,
    SQLiteSessionAuthorityRepository,
)
from ..storage import LeagueStorage
from .package_catalog import MobilePackageRelease

REQUEST_ID = re.compile(r"[A-Za-z0-9._:-]{1,128}\Z")
TERMINAL_STATUSES = {"COMPLETED", "CANCELLED", "EXPIRED", "FAILED"}
MOBILE_CREATE_JOURNAL_SCHEMA_VERSION = 2


@dataclass(frozen=True)
class MobileSessionRecord:
    id: str
    user_id: str
    auth_session_id_digest: str
    server_id: str
    save_id: str
    rom_id: str
    rom_header_title: str
    package_id: str
    release_id: str
    package_digest: str
    game_session_id: str
    fencing_token: int
    status: str
    lease_expires_at: str
    created_at: str
    updated_at: str
    failure_reason: str | None = None
    scenario_id: str = "default"
    scenario_display_name: str = "DEFAULT"

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "MobileSessionRecord":
        normalized = {key: data.get(key) for key in cls.__dataclass_fields__}
        normalized["scenario_id"] = data.get("scenario_id") or "default"
        normalized["scenario_display_name"] = data.get("scenario_display_name") or "DEFAULT"
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def to_public_dict(self) -> dict[str, Any]:
        value = self.to_dict()
        value.pop("auth_session_id_digest", None)
        return value


class MobileSessionManager:
    def __init__(self, storage: LeagueStorage, saves: SQLiteSaveManager, game_sessions: LinkSessionManager, *, server_id: str, session_repository: SQLiteSessionAuthorityRepository, fault_injector: Callable[[str], None] | None = None):
        self.storage = storage
        self.saves = saves
        self.game_sessions = game_sessions
        self.server_id = server_id
        self.fault_injector = fault_injector or (lambda _point: None)
        self.session_repository = session_repository

    def create(
        self,
        *,
        user_id: str,
        auth_session_id_digest: str,
        save_id: str,
        rom_id: str,
        rom_header_title: str,
        requested_scenario_id: str | None = None,
        package_resolver: Callable[[], MobilePackageRelease] | None = None,
        package: MobilePackageRelease | None = None,
        lease_expires_at: str,
        create_request_id: str,
    ) -> tuple[MobileSessionRecord, dict[str, Any] | None, bool]:
        if not REQUEST_ID.fullmatch(create_request_id) or not create_request_id.startswith("mobile-create:"):
            raise ValidationError("Mobile create request_id is invalid")
        journal_key = hashlib.sha256(
            f"{user_id}\0{create_request_id}".encode("utf-8")
        ).hexdigest()
        if package_resolver is None:
            if package is None:
                raise ValidationError("Mobile package resolver is required")
            package_resolver = lambda: package
        request_identity = {
            "user_id": user_id,
            "request_id": create_request_id,
            "save_id": save_id,
            "rom_id": rom_id,
            "rom_header_title": rom_header_title,
            "requested_scenario_id": requested_scenario_id or "",
        }
        package = package_resolver()
        identity = {
            **request_identity,
            "package_id": package.package_id,
            "release_id": package.release_id,
            "package_digest": package.package_digest,
            "scenario_id": package.scenario_id,
            "auth_session_id": auth_session_id_digest,
        }
        return self._create_sqlite(
            identity=identity,
            journal_key=journal_key,
            user_id=user_id,
            auth_session_id_digest=auth_session_id_digest,
            save_id=save_id,
            rom_id=rom_id,
            rom_header_title=rom_header_title,
            package=package,
            lease_expires_at=lease_expires_at,
        )

    def _create_sqlite(self, *, identity, journal_key, user_id, auth_session_id_digest, save_id, rom_id, rom_header_title, package, lease_expires_at):
        # A replaced slot must not recreate a retired Mobile request/session.
        self.saves.get_save(save_id, user_id)
        timestamp_ms = self._now_ms()
        try:
            journal, _is_new = self.session_repository.prepare_mobile_create(
                {
                    **identity,
                    "request_digest": journal_key,
                    "created_at_ms": timestamp_ms,
                }
            )
        except RepositoryConflictError as error:
            raise ValidationError(str(error)) from error
        if journal.get("auth_session_id") != auth_session_id_digest:
            raise MobileCreateAuthSessionConflictError(
                "Previous Mobile session belongs to a different login session; retry after it expires"
            )
        if journal["state"] == "CREATED":
            row = self.session_repository.get(
                "mobile", str(journal["mobile_session_id"])
            )
            if row is None:
                raise RevisionConflictError("Mobile create journal is inconsistent")
            record = self._record_from_row(row)
            if record.status != "RUNNING":
                return record, None, True
            return record, package.runtime_contract(), True

        save = self.saves.get_save(save_id, user_id)
        session_id = str(journal["reserved_session_id"])
        active = self.game_sessions.active_game_session_for_user(user_id)
        lock = active["lock"] if active else None
        if lock is not None:
            bindings = {str(item.get("save_id")) for item in lock.save_bindings}
            if not (
                lock.execution_mode == "MOBILE_CLIENT"
                and lock.auth_session_id == auth_session_id_digest
                and save_id in bindings
            ):
                raise ValidationError("user already has an active game session")
        else:
            lock = self.game_sessions.acquire_local_game_session_lock(
                user_id, lease_expires_at, auth_session_id_digest,
                execution_mode="MOBILE_CLIENT", saves=[save],
            )
        acquired_game = active is None
        committed = False
        try:
            contract = package.runtime_contract()
            for artifact in package.artifacts:
                self._persist_artifact(artifact.sha256, artifact.data)
            updated_ms = self._now_ms()
            row = self.session_repository.commit_mobile_create(
                journal_key,
                {
                    "session_id": session_id, "server_id": self.server_id,
                    "user_id": user_id, "save_id": save.id, "rom_id": rom_id,
                    "auth_session_id": auth_session_id_digest,
                    "rom_header_title": rom_header_title,
                    "package_id": package.package_id, "release_id": package.release_id,
                    "package_digest": package.package_digest,
                    "game_run_id": lock.game_run_id,
                    "game_session_id": lock.game_session_id,
                    "fencing_token": lock.fencing_token,
                    "scenario_id": package.scenario_id, "status": "RUNNING",
                    "created_at_ms": timestamp_ms, "updated_at_ms": updated_ms,
                    "lease_expires_at_ms": self._epoch_ms(lease_expires_at),
                    "scenario_display_name": package.scenario_display_name,
                },
            )
            committed = True
            self.fault_injector("before_response")
            return self._record_from_row(row), contract, False
        except Exception:
            if not committed:
                if acquired_game:
                    self._best_effort_release_game_lock(
                        lock.game_session_id, lock.fencing_token, user_id,
                        auth_session_id_digest,
                    )
            raise

    def get(self, session_id: str, user_id: str) -> MobileSessionRecord:
        data = self.session_repository.find_mobile(session_id, user_id)
        if data is None:
            raise NotFoundError("Mobile session not found")
        return self._record_from_row(data)

    def heartbeat(self, session_id: str, *, user_id: str, auth_session_id_digest: str, game_session_id: str, fencing_token: int, lease_expires_at: str) -> MobileSessionRecord:
        record = self._require_running(session_id, user_id, auth_session_id_digest, game_session_id, fencing_token)
        self.game_sessions.renew_local_game_session_lock(user_id, lease_expires_at, auth_session_id_digest, game_session_id, fencing_token)
        row = self.session_repository.find_mobile(session_id, user_id)
        if row is None:
            raise NotFoundError("Mobile session not found")
        timestamp_ms = self._now_ms()
        if not self.session_repository.renew_mobile(
            session_id, user_id=user_id,
            auth_session_id=auth_session_id_digest,
            game_run_id=str(row["game_run_id"]), fencing_token=fencing_token,
            expected_version=int(row["row_version"]),
            lease_expires_at_ms=self._epoch_ms(lease_expires_at),
            updated_at_ms=timestamp_ms,
        ):
            raise GameSessionExpiredError("Mobile session expired")
        renewed = self.session_repository.find_mobile(session_id, user_id)
        if renewed is None:
            raise GameSessionExpiredError("Mobile session expired")
        return self._record_from_row(renewed)

    def complete(self, session_id: str, *, user_id: str, auth_session_id_digest: str, game_session_id: str, fencing_token: int) -> MobileSessionRecord:
        record = self.get(session_id, user_id)
        self._require_owner(record, auth_session_id_digest, game_session_id, fencing_token)
        if record.status in TERMINAL_STATUSES:
            return record
        record = self._require_running(
            record.id, user_id, auth_session_id_digest,
            game_session_id, fencing_token,
        )
        return self._terminalize(record, "COMPLETED")

    def cancel(self, session_id: str, *, user_id: str, auth_session_id_digest: str, game_session_id: str, fencing_token: int, reason: str = "client canceled") -> MobileSessionRecord:
        record = self.get(session_id, user_id)
        self._require_owner(record, auth_session_id_digest, game_session_id, fencing_token)
        if record.status in TERMINAL_STATUSES:
            return record
        return self._terminalize(record, "CANCELLED", failure_reason=reason[:160])

    def expire_due(self, current_time: datetime | None = None) -> list[MobileSessionRecord]:
        current_time = current_time or datetime.now(timezone.utc)
        results = []
        now_ms = int(current_time.timestamp() * 1000)
        candidates = self.session_repository.list_due(
            "mobile", server_id=self.server_id,
            active_states={"RUNNING"}, now_ms=now_ms,
        )
        for data in candidates:
            session_id = str(data.get("session_id", "")) if isinstance(data, dict) else ""
            if not isinstance(data, dict):
                continue
            record = self.get(session_id, str(data["user_id"]))
            if record.status != "RUNNING" or not self._deadline_reached_at(record.lease_expires_at, current_time):
                continue
            results.append(self._terminalize(record, "EXPIRED", failure_reason="lease expired"))
        return results

    def _require_running(self, session_id, user_id, auth_session_id_digest, game_session_id, fencing_token):
        record = self.get(session_id, user_id)
        if record.status == "EXPIRED":
            raise GameSessionExpiredError("Mobile session expired")
        if record.status != "RUNNING":
            raise ValidationError("Mobile session is not running")
        self._require_owner(record, auth_session_id_digest, game_session_id, fencing_token)
        if self._deadline_reached(record.lease_expires_at):
            self._terminalize(record, "EXPIRED", failure_reason="lease expired")
            raise GameSessionExpiredError("Mobile session expired")
        return record

    @staticmethod
    def _require_owner(record, auth_session_id_digest, game_session_id, fencing_token):
        if record.auth_session_id_digest != auth_session_id_digest:
            raise ValidationError("Mobile session belongs to a different auth session")
        if record.game_session_id != game_session_id or record.fencing_token != fencing_token:
            raise ValidationError("stale Mobile session fence")

    def _terminalize(self, record, status, **updates):
        row = self.session_repository.find_mobile(record.id, record.user_id)
        if row is None:
            raise NotFoundError("Mobile session not found")
        updated_ms = self._now_ms()
        if not self.session_repository.complete_mobile(
            record.id, user_id=record.user_id,
            auth_session_id=record.auth_session_id_digest,
            game_run_id=str(row["game_run_id"]),
            fencing_token=record.fencing_token,
            expected_version=int(row["row_version"]), status=status,
            updated_at_ms=updated_ms,
            failure_reason=updates.get("failure_reason"),
        ):
            current = self.get(record.id, record.user_id)
            if current.status in TERMINAL_STATUSES:
                return current
            raise ValidationError("Mobile session transition fence is stale")
        terminal = self.get(record.id, record.user_id)
        self._release_authority(terminal)
        return terminal

    def _release_authority(self, record):
        self._best_effort_release_game_lock(record.game_session_id, record.fencing_token, record.user_id, record.auth_session_id_digest)

    def _best_effort_release_game_lock(self, game_session_id, fencing_token, user_id, auth_session_id):
        try:
            active = self.game_sessions.active_game_session_for_user(user_id)
            if active and active["lock"].game_session_id == game_session_id:
                self.game_sessions.release_user_game_session_lock(user_id, auth_session_id, game_session_id, fencing_token)
        except Exception:
            pass

    @classmethod
    def _record_from_row(cls, row: dict[str, Any]) -> MobileSessionRecord:
        metadata = row.get("metadata_json") or {}
        return MobileSessionRecord(
            id=str(row["session_id"]), user_id=str(row["user_id"]),
            auth_session_id_digest=str(row["auth_session_id"]),
            server_id=str(row["server_id"]), save_id=str(row["save_id"]),
            rom_id=str(row["rom_id"]),
            rom_header_title=str(metadata.get("rom_header_title") or ""),
            package_id=str(row["package_id"]), release_id=str(row["release_id"]),
            package_digest=str(row["package_digest"]),
            game_session_id=str(row.get("game_session_id") or row["game_run_id"]),
            fencing_token=int(row["fencing_token"]), status=str(row["status"]),
            lease_expires_at=cls._iso_ms(int(row["lease_expires_at_ms"])),
            created_at=cls._iso_ms(int(row["created_at_ms"])),
            updated_at=cls._iso_ms(int(row["updated_at_ms"])),
            failure_reason=row.get("failure_reason"),
            scenario_id=str(row.get("scenario_id") or "default"),
            scenario_display_name=str(
                metadata.get("scenario_display_name") or "DEFAULT"
            ),
        )

    @staticmethod
    def _epoch_ms(value: str) -> int:
        parsed = datetime.fromisoformat(value)
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return int(parsed.timestamp() * 1000)

    @staticmethod
    def _iso_ms(value: int) -> str:
        return datetime.fromtimestamp(value / 1000, timezone.utc).isoformat()

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now(timezone.utc).timestamp() * 1000)

    @staticmethod
    def _replace(record, **updates):
        value = record.to_dict()
        value.update(updates)
        return MobileSessionRecord.from_dict(value)

    def _persist_artifact(self, digest, payload):
        if hashlib.sha256(payload).hexdigest() != digest:
            raise ValidationError("Mobile artifact payload does not match its storage key")
        path = self.storage.resolve_relative(f"mobile_artifacts/{digest[:2]}/{digest}.bin")
        if path.exists():
            if (
                path.is_symlink() or not path.is_file() or
                path.stat().st_size != len(payload) or
                hashlib.sha256(path.read_bytes()).hexdigest() != digest
            ):
                raise ValidationError("Mobile artifact store is inconsistent")
            return
        self.storage.atomic_write_bytes(path, payload)

    @staticmethod
    def _deadline_reached(value):
        return MobileSessionManager._deadline_reached_at(value, datetime.now(timezone.utc))

    @staticmethod
    def _deadline_reached_at(value, current):
        try:
            deadline = datetime.fromisoformat(value)
        except ValueError:
            return True
        if deadline.tzinfo is None:
            deadline = deadline.replace(tzinfo=timezone.utc)
        return deadline <= current
