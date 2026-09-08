# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SQLite-backed ROM and SAV metadata services for the authority cutover."""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import os
import shutil
import threading
from typing import Any

from .errors import NotFoundError, RevisionConflictError, SaveLockedError, ValidationError
from .models import SaveRecord
from .rom_metadata import rom_platform_from_filename
from .roms import (
    RomRegistration,
    find_allowed_registration,
    validate_registration_metadata,
)
from .save_contract import MAX_SAVE_BYTES, REQUEST_ID, SaveUploadAuthority, normalize_game_type
from .security import issue_secret
from .storage import LeagueStorage
from .sqlite_repositories import RepositoryConflictError, SQLiteAssetRepository


class SQLiteRomRegistry:
    """ROM registrations backed exclusively by normalized SQLite rows."""

    def __init__(
        self,
        repository: SQLiteAssetRepository,
        server_id: str,
        *,
        allow_unlisted_roms: bool = True,
    ):
        self.repository = repository
        self.server_id = server_id
        self.allow_unlisted_roms = allow_unlisted_roms

    def register(
        self,
        user_id: str,
        sha256: str,
        title: str,
        platform: str,
        region: str,
        sha1: str | None = None,
        rom_header_title: str | None = None,
    ) -> RomRegistration:
        values = self.validate_registration_metadata(
            sha256, title, platform, region, sha1, rom_header_title
        )
        timestamp = self._now_ms()
        record = {
            "rom_id": issue_secret("rom"),
            "server_id": self.server_id,
            "user_id": user_id,
            **values,
            "size_bytes": None,
            "created_at_ms": timestamp,
            "updated_at_ms": timestamp,
        }
        try:
            self.repository.register_rom(record)
        except RepositoryConflictError as error:
            raise ValidationError("ROM is already registered for this user") from error
        return self._registration(record)

    def validate_registration_metadata(
        self,
        sha256: str,
        title: str,
        platform: str,
        region: str,
        sha1: str | None = None,
        rom_header_title: str | None = None,
    ) -> dict[str, str | None]:
        return validate_registration_metadata(
            sha256,
            title,
            platform,
            region,
            sha1,
            rom_header_title,
            allow_unlisted_roms=self.allow_unlisted_roms,
        )

    def register_or_get(
        self,
        user_id: str,
        sha256: str,
        title: str,
        platform: str,
        region: str,
        sha1: str | None = None,
        rom_header_title: str | None = None,
    ) -> RomRegistration:
        values = self.validate_registration_metadata(
            sha256, title, platform, region, sha1, rom_header_title
        )
        existing = self.repository.find_rom_by_hash(
            self.server_id, user_id, str(values["sha256"])
        )
        if existing is None:
            return self.register(
                user_id,
                str(values["sha256"]),
                str(values["title"]),
                str(values["platform"]),
                str(values["region"]),
                values["sha1"],
                values["rom_header_title"],
            )
        if (
            existing.get("game_type") != values["game_type"]
            or rom_platform_from_filename(str(existing.get("title", "")))
            != values["platform"]
            or existing.get("region") != values["region"]
            or existing.get("rom_header_title") != values["rom_header_title"]
        ):
            raise ValidationError("existing ROM metadata does not match server-derived metadata")
        return self._registration(existing)

    def list_for_user(self, user_id: str) -> list[RomRegistration]:
        return [
            self._registration(record)
            for record in self.repository.list_roms(self.server_id, user_id)
        ]

    def get(self, registration_id: str, user_id: str) -> RomRegistration:
        record = self.repository.get_rom(registration_id, user_id)
        if record is None or record["server_id"] != self.server_id:
            raise NotFoundError("ROM registration not found")
        return self._registration(record)

    def ensure_trusted_header_title(
        self, registration_id: str, user_id: str
    ) -> RomRegistration:
        registration = self.get(registration_id, user_id)
        allowed = find_allowed_registration(
            registration.platform,
            registration.region,
            registration.sha256,
            registration.sha1,
            allow_unlisted_roms=self.allow_unlisted_roms,
        )
        if not registration.rom_header_title:
            raise ValidationError("registered ROM header title is missing")
        if allowed and allowed.rom_header_title:
            if registration.rom_header_title != allowed.rom_header_title:
                raise ValidationError(
                    "registered ROM header title conflicts with the enabled ROM catalog"
                )
        return registration

    @classmethod
    def _registration(cls, record: dict[str, Any]) -> RomRegistration:
        platform = rom_platform_from_filename(str(record["title"]))
        if platform is None:
            raise ValidationError("stored ROM filename has no supported platform")
        return RomRegistration(
            id=str(record["rom_id"]),
            user_id=str(record["user_id"]),
            sha256=str(record["sha256"]),
            title=str(record["title"]),
            game_type=str(record["game_type"]),
            platform=platform,
            region=str(record["region"]),
            created_at=cls._iso(int(record["created_at_ms"])),
            sha1=str(record["sha1"]) if record.get("sha1") else None,
            verified_name=(
                str(record["verified_name"]) if record.get("verified_name") else None
            ),
            rom_header_title=(
                str(record["rom_header_title"])
                if record.get("rom_header_title")
                else None
            ),
        )

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now(timezone.utc).timestamp() * 1000)

    @staticmethod
    def _iso(value: int) -> str:
        return datetime.fromtimestamp(value / 1000, tz=timezone.utc).isoformat()


class SaveCommitService:
    """Recoverable filesystem SAV replace with SQLite metadata authority."""

    def __init__(
        self,
        storage: LeagueStorage,
        repository: SQLiteAssetRepository,
        *,
        backup_generations: int = 5,
        fault_injector=None,
    ):
        self.storage = storage
        self.repository = repository
        self.backup_generations = backup_generations
        self.fault_injector = fault_injector or (lambda _point: None)
        self._request_locks = tuple(threading.Lock() for _ in range(64))

    def commit(
        self,
        *,
        user_id: str,
        save_id: str,
        request_id: str,
        expected_revision: int,
        save_bytes: bytes,
        authority: SaveUploadAuthority,
    ) -> tuple[SaveRecord, bool]:
        if not REQUEST_ID.fullmatch(request_id):
            raise ValidationError("request_id is required and must use safe ASCII characters")
        self._validate_save_bytes(save_bytes)
        candidate_sha256 = hashlib.sha256(save_bytes).hexdigest()
        request_key = hashlib.sha256(f"{user_id}\0{request_id}".encode("utf-8")).hexdigest()
        request_lock = self._request_locks[int(request_key[:2], 16) % len(self._request_locks)]
        with request_lock:
            with self.storage.exclusive_lock(f"sqlite-save-upload-{request_key}"):
                return self._commit_locked(
                    request_key=request_key,
                    user_id=user_id,
                    save_id=save_id,
                    expected_revision=expected_revision,
                    save_bytes=save_bytes,
                    candidate_sha256=candidate_sha256,
                    authority=authority,
                )

    def _commit_locked(
        self,
        *,
        request_key: str,
        user_id: str,
        save_id: str,
        expected_revision: int,
        save_bytes: bytes,
        candidate_sha256: str,
        authority: SaveUploadAuthority,
    ) -> tuple[SaveRecord, bool]:
        previous = self.repository.get_save_upload(request_key)
        current = self.repository.get_save(save_id, user_id)
        if current is None:
            raise NotFoundError("save not found")
        identity = {
            "save_id": save_id,
            "user_id": user_id,
            "expected_revision": expected_revision,
            "candidate_sha256": candidate_sha256,
            "candidate_size": len(save_bytes),
            "authority_mode": authority.mode,
            "mobile_session_id": authority.mobile_session_id,
            "game_run_id": authority.game_run_id or authority.game_session_id or None,
            "game_session_id": authority.game_session_id or None,
            "fencing_token": authority.fencing_token,
            "lock_owner": authority.lock_owner,
            "auth_session_id": authority.auth_session_id,
        }
        if previous is not None:
            self._require_same_request(previous, identity)
            if previous["state"] == "COMMITTED":
                return self._committed_record(previous, current), True
            if previous["state"] != "PREPARED":
                raise ValidationError("save upload request journal is invalid")
        else:
            if int(current["revision"]) != expected_revision:
                raise RevisionConflictError(
                    f"expected revision {expected_revision}, current revision is {current['revision']}"
                )
            base_sha256 = str(current["sha256"])
            target = self.storage.resolve_relative(str(current["relative_path"]))
            if self._file_sha256(target) != base_sha256:
                raise RevisionConflictError("SAV file hash does not match its metadata")
            candidate_relative = f"save_upload_candidates/{request_key}.sav"
            backup_relative = (
                f"save_backups/{user_id}/{save_id}/backup_{expected_revision:04d}.sav"
            )
            timestamp = self._now_ms()
            try:
                previous, _created = self.repository.prepare_save_upload(
                    {
                        "request_id": request_key,
                        **identity,
                        "base_sha256": base_sha256,
                        "candidate_relative_path": candidate_relative,
                        "backup_relative_path": backup_relative,
                        "created_at_ms": timestamp,
                        "updated_at_ms": timestamp,
                    },
                    timestamp,
                )
            except RepositoryConflictError as error:
                self._raise_authority_conflict(error, current, expected_revision)
            self._require_same_request(previous, identity)
            self.fault_injector("after_prepare")

        record = self._finish_prepared(previous, save_bytes)
        self.fault_injector("before_response")
        return record, False

    def find_committed(self, user_id: str, request_id: str) -> dict[str, Any] | None:
        if not REQUEST_ID.fullmatch(request_id):
            return None
        key = hashlib.sha256(f"{user_id}\0{request_id}".encode("utf-8")).hexdigest()
        journal = self.repository.get_save_upload(key)
        if journal is None or journal.get("state") != "COMMITTED":
            return None
        return {
            **journal,
            "request_id": request_id,
            "payload_sha256": journal.get("candidate_sha256"),
            "target_revision": int(journal.get("expected_revision", -1)) + 1,
        }

    def _finish_prepared(self, journal: dict[str, Any], save_bytes: bytes) -> SaveRecord:
        current = self.repository.get_save(str(journal["save_id"]), str(journal["user_id"]))
        if current is None:
            raise NotFoundError("save not found")
        target = self.storage.resolve_relative(str(current["relative_path"]))
        candidate = self.storage.resolve_relative(str(journal["candidate_relative_path"]))
        backup = self.storage.resolve_relative(str(journal["backup_relative_path"]))
        target_sha256 = self._file_sha256(target)
        base_sha256 = str(journal["base_sha256"])
        candidate_sha256 = str(journal["candidate_sha256"])

        if target_sha256 == base_sha256:
            if not backup.exists():
                self.storage.atomic_write_bytes(backup, target.read_bytes(), metric_name="save_backup")
                self._fsync_directory(backup.parent)
            elif self._file_sha256(backup) != base_sha256:
                raise RevisionConflictError("SAV backup does not match prepared upload")
            self.fault_injector("after_backup")
            self.storage.atomic_write_bytes(candidate, save_bytes, metric_name="save_candidate")
            self._fsync_directory(candidate.parent)
            if self._file_sha256(candidate) != candidate_sha256:
                raise RevisionConflictError("SAV candidate hash verification failed")
            os.replace(candidate, target)
            self._fsync_directory(target.parent)
            self.fault_injector("after_atomic_replace")
        elif target_sha256 == candidate_sha256:
            if not backup.is_file() or self._file_sha256(backup) != base_sha256:
                raise RevisionConflictError("replaced SAV has no valid recovery backup")
        else:
            raise RevisionConflictError("SAV bytes no longer match prepared upload")

        committed = self.repository.commit_prepared_save_upload(
            str(journal["request_id"]), self._now_ms()
        )
        if committed is None:
            raise ValidationError("save upload request journal disappeared")
        self.fault_injector("after_metadata_update")
        refreshed = self.repository.get_save(
            str(journal["save_id"]), str(journal["user_id"])
        )
        self._prune_backups(str(journal["user_id"]), str(journal["save_id"]))
        return self._save_record(refreshed or committed)

    def _prune_backups(self, user_id: str, save_id: str) -> None:
        directory = self.storage.backups_dir / user_id / save_id
        if not directory.is_dir():
            return
        backups = sorted(directory.glob("backup_*.sav"), reverse=True)
        for path in backups[self.backup_generations:]:
            path.unlink(missing_ok=True)

    def reconcile_prepared(self, limit: int = 100) -> list[str]:
        completed: list[str] = []
        for journal in self.repository.list_prepared_save_uploads(limit):
            save = self.repository.get_save(str(journal["save_id"]), str(journal["user_id"]))
            if save is None:
                continue
            target = self.storage.resolve_relative(str(save["relative_path"]))
            candidate = self.storage.resolve_relative(str(journal["candidate_relative_path"]))
            target_hash = self._file_sha256(target)
            if target_hash == journal["candidate_sha256"]:
                if self._valid_backup(journal):
                    self.repository.commit_prepared_save_upload(
                        str(journal["request_id"]), self._now_ms()
                    )
                    completed.append(str(journal["request_id"]))
            elif target_hash == journal["base_sha256"] and candidate.is_file():
                if self._file_sha256(candidate) == journal["candidate_sha256"]:
                    self._finish_prepared(journal, candidate.read_bytes())
                    completed.append(str(journal["request_id"]))
        return completed

    def _valid_backup(self, journal: dict[str, Any]) -> bool:
        backup = self.storage.resolve_relative(str(journal["backup_relative_path"]))
        return backup.is_file() and self._file_sha256(backup) == journal["base_sha256"]

    @staticmethod
    def _require_same_request(previous: dict[str, Any], identity: dict[str, Any]) -> None:
        for field, expected in identity.items():
            if previous.get(field) != expected:
                raise ValidationError("request_id was already used for a different save upload")

    @staticmethod
    def _raise_authority_conflict(
        error: RepositoryConflictError, current: dict[str, Any], expected_revision: int
    ) -> None:
        if "revision" in str(error):
            raise RevisionConflictError(
                f"expected revision {expected_revision}, current revision is {current['revision']}"
            ) from error
        raise SaveLockedError(str(error)) from error

    @classmethod
    def _save_record(cls, record: dict[str, Any]) -> SaveRecord:
        return SaveRecord(
            id=str(record["save_id"]),
            user_id=str(record["user_id"]),
            game_type=str(record["game_type"]),
            revision=int(record["revision"]),
            sha256=str(record["sha256"]),
            storage_path=str(record["relative_path"]),
            created_at=cls._iso(int(record["created_at_ms"])),
            updated_at=cls._iso(int(record["updated_at_ms"])),
            lock_owner=(str(record["lock_owner_id"]) if record.get("lock_owner_id") else None),
            lock_expires_at=(
                cls._iso(int(record["lock_expires_at_ms"]))
                if record.get("lock_expires_at_ms") is not None
                else None
            ),
        )

    @classmethod
    def _committed_record(
        cls, journal: dict[str, Any], current: dict[str, Any]
    ) -> SaveRecord:
        historical = {
            **current,
            "revision": int(journal["expected_revision"]) + 1,
            "sha256": str(journal["candidate_sha256"]),
            "updated_at_ms": int(journal["committed_at_ms"] or journal["updated_at_ms"]),
        }
        return cls._save_record(historical)

    @staticmethod
    def _validate_save_bytes(save_bytes: bytes) -> None:
        if not save_bytes:
            raise ValidationError("save data is empty")
        if len(save_bytes) > MAX_SAVE_BYTES:
            raise ValidationError("save data is too large")

    @staticmethod
    def _file_sha256(path) -> str:
        if not path.is_file() or path.is_symlink():
            raise RevisionConflictError("SAV file is missing or unsafe")
        return hashlib.sha256(path.read_bytes()).hexdigest()

    @staticmethod
    def _fsync_directory(path) -> None:
        if os.name == "nt":
            return
        descriptor = os.open(path, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now(timezone.utc).timestamp() * 1000)

    @staticmethod
    def _iso(value: int) -> str:
        return datetime.fromtimestamp(value / 1000, tz=timezone.utc).isoformat()


class SQLiteSaveManager:
    """SQLite SAV catalog; byte updates remain delegated to the journal service."""

    def __init__(
        self,
        storage: LeagueStorage,
        repository: SQLiteAssetRepository,
        server_id: str,
        backup_generations: int = 5,
    ):
        self.storage = storage
        self.repository = repository
        self.server_id = server_id
        self.backup_generations = backup_generations
        self.commit_service = SaveCommitService(
            storage, repository, backup_generations=backup_generations
        )
        self._user_locks = tuple(threading.Lock() for _ in range(64))

    def create_save(self, user_id: str, game_type: str, save_bytes: bytes) -> SaveRecord:
        game = self._validate_game_type(game_type)
        self._validate_save_bytes(save_bytes)
        user_lock = self._user_locks[self._stripe(user_id)]
        with user_lock:
            with self.storage.exclusive_lock(f"sqlite-save-create-{self._safe_lock_key(user_id)}"):
                if self.repository.find_save_by_game(self.server_id, user_id, game):
                    raise ValidationError("save already exists for this user and game")
                save_id = issue_secret("save")
                return self._create(
                    user_id,
                    game,
                    save_bytes,
                    f"saves/{user_id}/{game}/{save_id}/current.sav",
                    save_id,
                )

    def create_save_for_slot(
        self, user_id: str, slot: int, game_type: str, save_bytes: bytes
    ) -> SaveRecord:
        game = self._validate_game_type(game_type)
        self._validate_save_bytes(save_bytes)
        if slot < 1 or slot > 8:
            raise ValidationError("slot must be 1 to 8")
        save_id = issue_secret("save")
        return self._create(
            user_id,
            game,
            save_bytes,
            f"saves/{user_id}/slots/slot{slot}/{save_id}/current.sav",
            save_id,
        )

    def _create(
        self,
        user_id: str,
        game_type: str,
        save_bytes: bytes,
        relative_path: str,
        save_id: str,
    ) -> SaveRecord:
        path = self.storage.resolve_relative(relative_path)
        self.storage.atomic_write_bytes(path, save_bytes, metric_name="save_create")
        self._fsync_directory(path.parent)
        timestamp = self._now_ms()
        record = {
            "save_id": save_id,
            "server_id": self.server_id,
            "user_id": user_id,
            "rom_id": None,
            "game_type": game_type,
            "relative_path": relative_path,
            "size_bytes": len(save_bytes),
            "sha256": hashlib.sha256(save_bytes).hexdigest(),
            "revision": 1,
            "created_at_ms": timestamp,
            "updated_at_ms": timestamp,
        }
        try:
            self.repository.create_save(record)
        except Exception:
            path.unlink(missing_ok=True)
            raise
        return self._save_record(record)

    def get_save(self, save_id: str, user_id: str | None = None) -> SaveRecord:
        record = self.repository.get_save(save_id, user_id)
        if record is None or record["server_id"] != self.server_id:
            raise NotFoundError("save not found")
        return self._save_record(record)

    def list_for_user(self, user_id: str) -> list[SaveRecord]:
        return [
            self._save_record(record)
            for record in self.repository.list_saves(self.server_id, user_id)
        ]

    def download(self, save_id: str, user_id: str) -> tuple[SaveRecord, bytes]:
        record = self.get_save(save_id, user_id)
        payload = self.storage.read_bytes(record.storage_path)
        if hashlib.sha256(payload).hexdigest() != record.sha256:
            raise RevisionConflictError("SAV file hash does not match its metadata")
        return record, payload

    def upload(
        self, save_id: str, user_id: str, expected_revision: int,
        save_bytes: bytes, *, fault_injector=None,
    ) -> SaveRecord:
        request_id = self._commit_request_id(
            "single", save_id, expected_revision, save_bytes
        )
        service = self._commit_service_for(fault_injector)
        return service.commit(
            user_id=user_id,
            save_id=save_id,
            request_id=request_id,
            expected_revision=expected_revision,
            save_bytes=save_bytes,
            authority=SaveUploadAuthority.unlocked("SINGLE_UPLOAD"),
        )[0]

    def admin_replace_save(self, save_id: str, save_bytes: bytes) -> SaveRecord:
        raw = self.repository.get_save(save_id)
        if raw is None:
            raise NotFoundError("save not found")
        if raw.get("lock_owner_id"):
            self.repository.release_save_lock(
                save_id, str(raw["user_id"]), str(raw["lock_owner_id"]),
                int(raw.get("lock_fencing_token") or 0),
            )
            raw = self.repository.get_save(save_id)
        expected_revision = int(raw["revision"])
        return self.commit_service.commit(
            user_id=str(raw["user_id"]),
            save_id=save_id,
            request_id=self._commit_request_id(
                "admin", save_id, expected_revision, save_bytes
            ),
            expected_revision=expected_revision,
            save_bytes=save_bytes,
            authority=SaveUploadAuthority.unlocked("ADMIN_REPLACE"),
        )[0]

    def verify_locked_revision(
        self, save_id: str, user_id: str, owner: str, expected_revision: int
    ) -> SaveRecord:
        raw = self.repository.get_save(save_id, user_id)
        if raw is None:
            raise NotFoundError("save not found")
        if raw.get("lock_owner_id") != owner:
            raise SaveLockedError("save is not locked by this owner")
        if int(raw.get("lock_expires_at_ms") or 0) <= self._now_ms():
            raise SaveLockedError("save lock expired")
        if int(raw["revision"]) != expected_revision:
            raise RevisionConflictError(
                f"expected revision {expected_revision}, current revision is {raw['revision']}"
            )
        return self._save_record(raw)

    def validate_locked_commit(
        self, save_id: str, user_id: str, owner: str,
        expected_revision: int, save_bytes: bytes,
    ) -> SaveRecord:
        self._validate_save_bytes(save_bytes)
        return self.verify_locked_revision(save_id, user_id, owner, expected_revision)

    def commit_locked(
        self, save_id: str, user_id: str, owner: str,
        expected_revision: int, save_bytes: bytes, *, fault_injector=None,
    ) -> SaveRecord:
        self._validate_save_bytes(save_bytes)
        self.verify_locked_revision(save_id, user_id, owner, expected_revision)
        raw = self.repository.get_save(save_id, user_id)
        service = self._commit_service_for(fault_injector)
        return service.commit(
            user_id=user_id,
            save_id=save_id,
            request_id=self._commit_request_id(
                f"pair:{owner}", save_id, expected_revision, save_bytes
            ),
            expected_revision=expected_revision,
            save_bytes=save_bytes,
            authority=SaveUploadAuthority(
                mode="PAIR_COMMIT",
                game_session_id=str(raw.get("lock_game_run_id") or ""),
                game_run_id=str(raw.get("lock_game_run_id") or ""),
                fencing_token=int(raw.get("lock_fencing_token") or 0),
                lock_owner=owner,
                auth_session_id=str(raw.get("lock_auth_session_id") or ""),
            ),
        )[0]

    def reconcile_prepared_locked_commit(
        self, save_id: str, user_id: str, owner: str,
        base_revision: int, base_sha256: str, payload_sha256: str,
    ) -> SaveRecord:
        self.commit_service.reconcile_prepared()
        current = self.get_save(save_id, user_id)
        if current.revision != base_revision + 1 or current.sha256 != payload_sha256:
            raise RevisionConflictError("prepared save metadata changed")
        return current

    def _commit_service_for(self, fault_injector) -> SaveCommitService:
        if fault_injector is None:
            return self.commit_service
        return SaveCommitService(
            self.storage,
            self.repository,
            backup_generations=self.backup_generations,
            fault_injector=fault_injector,
        )

    @staticmethod
    def _commit_request_id(
        prefix: str, save_id: str, expected_revision: int, save_bytes: bytes
    ) -> str:
        identity = hashlib.sha256(save_bytes).hexdigest()
        return f"{prefix}:{save_id}:{expected_revision}:{identity}"[:192]

    def delete_save(self, save_id: str, user_id: str) -> SaveRecord:
        current = self.get_save(save_id, user_id)
        try:
            deleted = self.repository.delete_save(save_id, user_id, self._now_ms())
        except RepositoryConflictError as error:
            raise SaveLockedError(str(error)) from error
        if not deleted:
            raise SaveLockedError("save is locked")
        path = self.storage.resolve_relative(current.storage_path)
        path.unlink(missing_ok=True)
        self._fsync_directory(path.parent)
        self._remove_empty_parents(path.parent, self.storage.saves_dir)
        backup_dir = self.storage.backups_dir / user_id / save_id
        if backup_dir.exists():
            shutil.rmtree(backup_dir)
            self._fsync_directory(backup_dir.parent)
            self._remove_empty_parents(backup_dir.parent, self.storage.backups_dir)
        return current

    def lock_save(
        self,
        save_id: str,
        user_id: str,
        owner: str,
        expires_at: str,
        *,
        auth_session_id: str | None = None,
        game_run_id: str | None = None,
        fencing_token: int | None = None,
    ) -> SaveRecord:
        if not auth_session_id or fencing_token is None:
            raise ValidationError("SQLite save lock requires auth session and fencing token")
        lease_ms = self._epoch_ms(expires_at)
        if lease_ms <= self._now_ms():
            raise SaveLockedError("save lock expired")
        if not self.repository.acquire_save_lock(
            {
                "save_id": save_id,
                "user_id": user_id,
                "owner_id": owner,
                "game_run_id": game_run_id,
                "auth_session_id": auth_session_id,
                "fencing_token": fencing_token,
                "lease_expires_at_ms": lease_ms,
            },
            self._now_ms(),
        ):
            raise SaveLockedError("save is already locked")
        return self.get_save(save_id, user_id)

    def renew_lock(
        self,
        save_id: str,
        user_id: str,
        owner: str,
        expires_at: str,
        *,
        auth_session_id: str | None = None,
        fencing_token: int | None = None,
    ) -> SaveRecord:
        if not auth_session_id or fencing_token is None:
            current = self.repository.get_save(save_id, user_id)
            if current is None or current.get("lock_owner_id") != owner:
                raise ValidationError("SQLite save lock requires current owner authority")
            auth_session_id = str(current.get("lock_auth_session_id") or "")
            fencing_token = int(current.get("lock_fencing_token") or 0)
            if not auth_session_id or fencing_token <= 0:
                raise ValidationError("SQLite save lock requires auth session and fencing token")
        now_ms = self._now_ms()
        if not self.repository.renew_save_lock(
            save_id,
            user_id,
            owner,
            auth_session_id,
            fencing_token,
            self._epoch_ms(expires_at),
            now_ms,
        ):
            raise SaveLockedError("save lock authority is stale")
        return self.get_save(save_id, user_id)

    def unlock_save(
        self,
        save_id: str,
        user_id: str,
        owner: str,
        *,
        fencing_token: int | None = None,
    ) -> SaveRecord:
        if fencing_token is None:
            current = self.repository.get_save(save_id, user_id)
            if current is None or current.get("lock_owner_id") != owner:
                raise ValidationError("SQLite save unlock requires current owner authority")
            fencing_token = int(current.get("lock_fencing_token") or 0)
            if fencing_token <= 0:
                raise ValidationError("SQLite save unlock requires fencing token")
        if not self.repository.release_save_lock(
            save_id, user_id, owner, fencing_token
        ):
            raise SaveLockedError("save lock authority is stale")
        return self.get_save(save_id, user_id)

    @classmethod
    def _save_record(cls, record: dict[str, Any]) -> SaveRecord:
        return SaveCommitService._save_record(record)

    @staticmethod
    def _validate_game_type(game_type: str) -> str:
        try:
            return normalize_game_type(game_type)
        except ValueError as error:
            raise ValidationError(str(error)) from error

    @staticmethod
    def _validate_save_bytes(save_bytes: bytes) -> None:
        SaveCommitService._validate_save_bytes(save_bytes)

    @staticmethod
    def _epoch_ms(value: str) -> int:
        try:
            parsed = datetime.fromisoformat(value)
        except ValueError as error:
            raise ValidationError("invalid save lock expiry") from error
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return int(parsed.timestamp() * 1000)

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now(timezone.utc).timestamp() * 1000)

    @staticmethod
    def _safe_lock_key(value: str) -> str:
        return hashlib.sha256(value.encode("utf-8")).hexdigest()

    @staticmethod
    def _stripe(value: str) -> int:
        return int(hashlib.sha256(value.encode("utf-8")).hexdigest()[:2], 16) % 64

    @staticmethod
    def _fsync_directory(path) -> None:
        SaveCommitService._fsync_directory(path)

    @staticmethod
    def _remove_empty_parents(path, boundary) -> None:
        boundary = boundary.resolve()
        current = path.resolve()
        while current != boundary and boundary in current.parents:
            try:
                current.rmdir()
            except OSError:
                break
            current = current.parent
