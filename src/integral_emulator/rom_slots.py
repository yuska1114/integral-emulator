# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Server-managed ROM slot assignments."""

from __future__ import annotations

from dataclasses import dataclass
import logging
import time
from typing import Any

from .errors import ValidationError
from .models import now_iso
from .rom_metadata import rom_platform_from_filename
from .roms import RomRegistration


MAX_ROM_SLOTS = 8
INITIAL_SAVE_BYTES = bytes(32 * 1024)
INITIAL_N64_SAVE_BYTES = bytes([0xFF]) * (128 * 1024)
@dataclass(frozen=True)
class RomSlot:
    user_id: str
    slot: int
    rom_id: str | None
    save_id: str | None
    filename: str | None
    sha256: str | None
    sha1: str | None
    game_type: str | None
    updated_at: str
    rom_header_title: str | None = None
    row_version: int = 0

    @classmethod
    def empty(cls, user_id: str, slot: int) -> "RomSlot":
        return cls(
            user_id=user_id,
            slot=slot,
            rom_id=None,
            save_id=None,
            filename=None,
            sha256=None,
            sha1=None,
            game_type=None,
            updated_at=now_iso(),
            rom_header_title=None,
        )

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RomSlot":
        normalized = {
            key: data.get(key) for key in cls.__dataclass_fields__
        }
        normalized.setdefault("sha1", None)
        normalized.setdefault("rom_header_title", None)
        normalized["row_version"] = int(data.get("__row_version", data.get("row_version", 0)))
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        data = {**self.__dict__, "platform": rom_platform_from_filename(self.filename)}
        data.pop("row_version", None)
        return data


class RomSlotManager:
    def __init__(self, storage, roms: Any, saves, sessions=None):
        self.storage = storage
        self.roms = roms
        self.saves = saves
        self.sessions = sessions

    def list_for_user(self, user_id: str) -> list[RomSlot]:
        records = self.storage.load_rom_slots()
        by_slot = {
            int(item["slot"]): RomSlot.from_dict(item)
            for item in records.values()
            if item["user_id"] == user_id
        }
        return [by_slot.get(slot, RomSlot.empty(user_id, slot)) for slot in range(1, MAX_ROM_SLOTS + 1)]

    def ensure_trusted_header_title(self, slot: RomSlot) -> tuple[RomSlot, RomRegistration]:
        if not slot.rom_id:
            raise ValidationError("ROM slot is not registered")
        registration = self.roms.ensure_trusted_header_title(slot.rom_id, slot.user_id)
        if not slot.rom_header_title:
            raise ValidationError("ROM slot header title is missing")
        if slot.rom_header_title != registration.rom_header_title:
            raise ValidationError("ROM slot header title conflicts with its registration")
        return slot, registration

    def apply_slots(self, user_id: str, desired_slots: list[dict[str, Any]], confirm_delete_saves: bool = False) -> dict[str, Any]:
        if len(desired_slots) > MAX_ROM_SLOTS:
            raise ValidationError("too many ROM slots")

        current = {slot.slot: slot for slot in self.list_for_user(user_id)}
        normalized = {}
        deletions: list[RomSlot] = []
        changes = []
        for item in desired_slots:
            slot_number = int(item.get("slot", 0))
            if slot_number < 1 or slot_number > MAX_ROM_SLOTS:
                raise ValidationError("slot must be 1 to 8")
            if slot_number in normalized:
                raise ValidationError("duplicate ROM slot")
            filename_value = item.get("filename", "")
            sha256_value = item.get("sha256", "")
            sha1_value = item.get("sha1", "")
            platform = item.get("platform", "")
            region = item.get("region", "")
            rom_header_title = item.get("rom_header_title")
            if not all(isinstance(value, str) for value in (
                filename_value, sha256_value, sha1_value, platform, region
            )) or (rom_header_title is not None and not isinstance(rom_header_title, str)):
                raise ValidationError("ROM slot metadata must use strings")
            filename = filename_value.strip()
            sha256 = sha256_value.strip().lower()
            sha1 = sha1_value.strip().lower()
            initial_save_bytes = item.get("initial_save_bytes")
            if initial_save_bytes is not None and not isinstance(initial_save_bytes, bytes):
                raise ValidationError("initial_save_bytes must be bytes")
            if not filename and not sha256:
                normalized[slot_number] = None
                if current[slot_number].save_id:
                    deletions.append(current[slot_number])
                continue
            if not filename or not sha256 or not platform:
                raise ValidationError("filename, sha256, and platform are required")
            if rom_platform_from_filename(filename) is None:
                raise ValidationError("filename must use a supported GB/GBC or N64 ROM extension")
            existing = current[slot_number]
            validated = self.roms.validate_registration_metadata(
                sha256,
                filename,
                platform,
                region,
                sha1,
                rom_header_title,
            )
            changed = (
                existing.sha256 != validated["sha256"]
                or existing.filename != validated["title"]
            )
            normalized[slot_number] = {
                "filename": validated["title"],
                "sha256": validated["sha256"],
                "sha1": validated["sha1"],
                "game_type": validated["game_type"],
                "platform": validated["platform"],
                "region": validated["region"],
                "initial_save_bytes": initial_save_bytes,
                "rom_header_title": validated["rom_header_title"],
            }
            if changed and existing.save_id:
                deletions.append(existing)
            if changed:
                changes.append(slot_number)

        if deletions and not confirm_delete_saves:
            return {
                "requires_confirmation": True,
                "delete_save_ids": [slot.save_id for slot in deletions if slot.save_id],
                "changed_slots": changes,
                "slots": [slot.to_dict() for slot in current.values()],
            }

        # Confirmation is read-only. Recovery, if necessary, commits separately:
        # a later registration failure must not roll back recovered SAV metadata.
        if self.sessions:
            for old in deletions:
                self.sessions.settle_save_replacement(old.save_id, user_id)
        created = []
        removed = []
        try:
            with self.storage.database.write_unit() as connection:
                self._require_idle(connection, user_id)
                latest = {slot.slot: slot for slot in self.list_for_user(user_id)}
                if any(latest[n].row_version != current[n].row_version for n in current):
                    raise ValidationError("ROM slots changed; reload and confirm again")
                result = self._apply_normalized(
                    user_id, normalized, current, deletions, changes, created, removed,
                    connection,
                )
        except Exception:
            for save in created:
                self._cleanup_files(save)
            raise
        cleanup_pending = [save.id for save in removed if not self._cleanup_files(save)]
        if cleanup_pending:
            result["cleanup_pending_save_ids"] = cleanup_pending
        return result

    def _cleanup_files(self, save):
        try:
            self.saves.remove_save_files(save)
            return True
        except OSError:
            logging.getLogger(__name__).exception("SAV file cleanup pending: %s", save.id)
            return False

    @staticmethod
    def _require_idle(connection, user_id):
        now_ms = int(time.time() * 1000)
        if connection.execute(
            "SELECT 1 FROM game_session_locks WHERE user_id=? AND lease_expires_at_ms>?",
            (user_id, now_ms),
        ).fetchone():
            raise ValidationError("ROM slots cannot be changed during an active game session")

    def _detach_history(self, connection, save_id, user_id):
        now_ms = int(time.time() * 1000)
        if connection.execute(
            "SELECT 1 FROM save_locks WHERE save_id=? AND lease_expires_at_ms>?",
            (save_id, now_ms),
        ).fetchone():
            raise ValidationError("SAV is in use; finish the game before replacing it")
        # Retain terminal session identities, so delayed requests cannot restart
        # them. The saved payload is no longer required by finished history.
        pending = connection.execute(
            "SELECT 1 FROM link_sessions WHERE (save_a_id=? OR save_b_id=?) "
            "AND status NOT IN ('COMPLETED','CANCELLED','FAILED','EXPIRED')",
            (save_id, save_id),
        ).fetchone()
        if pending:
            raise ValidationError("SAV recovery must be resolved before replacement")
        if connection.execute(
            "SELECT 1 FROM pair_save_commit_journals WHERE (player_a_save_id=? OR player_b_save_id=?) "
            "AND state NOT IN ('COMMITTED','ABORTED')", (save_id, save_id),
        ).fetchone():
            raise ValidationError("SAV pair recovery must be resolved before replacement")
        pending = connection.execute(
            "SELECT 1 FROM mobile_sessions WHERE save_id=? AND status='RUNNING' "
            "AND lease_expires_at_ms>?", (save_id, now_ms),
        ).fetchone()
        if pending:
            raise ValidationError("Mobile game is still running")
        connection.execute(
            "UPDATE mobile_sessions SET status='EXPIRED', row_version=row_version+1 "
            "WHERE save_id=? AND status='RUNNING'", (save_id,),
        )
        connection.execute("DELETE FROM mobile_create_requests WHERE save_id=?", (save_id,))
        connection.execute("UPDATE mobile_sessions SET save_id=NULL WHERE save_id=?", (save_id,))
        for column in ('save_a_id', 'save_b_id'):
            connection.execute(f"UPDATE link_sessions SET {column}=NULL, row_version=row_version+1 WHERE {column}=?", (save_id,))
        for column in ('player_a_save_id', 'player_b_save_id'):
            connection.execute(f"UPDATE pair_save_commit_journals SET {column}=NULL WHERE {column}=?", (save_id,))

    def _apply_normalized(self, user_id, normalized, current, deletions, changes,
                          created, removed, connection):
        delete_ids: list[str] = []
        applied = []
        for slot_number in range(1, MAX_ROM_SLOTS + 1):
            key = self._key(user_id, slot_number)
            if slot_number not in normalized:
                applied.append(current[slot_number])
                continue
            desired = normalized[slot_number]
            if desired is None:
                if current[slot_number].row_version:
                    self.storage.delete_rom_slot(
                        user_id, slot_number, current[slot_number].row_version
                    )
                applied.append(RomSlot.empty(user_id, slot_number))
                continue
            existing = current[slot_number]
            if existing.sha256 == desired["sha256"] and existing.filename == desired["filename"] and existing.rom_id and existing.save_id:
                if desired["initial_save_bytes"] is not None:
                    raise ValidationError(
                        "initial_save_data cannot update an existing SAV; "
                        "use the Admin SAV Replace screen"
                    )
                registration = self.roms.register_or_get(
                    user_id,
                    sha256=desired["sha256"],
                    sha1=desired["sha1"],
                    title=desired["filename"],
                    platform=desired["platform"],
                    region=desired["region"],
                    rom_header_title=desired["rom_header_title"],
                )
                if registration.id != existing.rom_id:
                    raise ValidationError("ROM slot registration changed unexpectedly")
                if existing.rom_header_title != registration.rom_header_title:
                    raise ValidationError("ROM slot header changed unexpectedly")
                applied.append(existing)
                continue
            registration = self.roms.register_or_get(
                user_id,
                sha256=desired["sha256"],
                sha1=desired["sha1"],
                title=desired["filename"],
                platform=desired["platform"],
                region=desired["region"],
                rom_header_title=desired["rom_header_title"],
            )
            save = self.saves.create_save_for_slot(
                user_id,
                slot_number,
                registration.game_type,
                (desired["initial_save_bytes"]
                 if desired["initial_save_bytes"] is not None
                 else self._initial_save_bytes_for_rom(desired["filename"])),
            )
            created.append(save)
            slot = RomSlot(
                user_id=user_id,
                slot=slot_number,
                rom_id=registration.id,
                save_id=save.id,
                filename=desired["filename"],
                sha256=registration.sha256,
                sha1=registration.sha1,
                game_type=registration.game_type,
                updated_at=now_iso(),
                rom_header_title=registration.rom_header_title,
            )
            if existing.row_version:
                stored = self.storage.update_rom_slot(
                    slot.to_dict(), existing.row_version
                )
            else:
                stored = self.storage.insert_rom_slot(slot.to_dict())
            slot = RomSlot.from_dict(stored)
            applied.append(slot)

        for old_slot in deletions:
            if old_slot.save_id:
                self._detach_history(connection, old_slot.save_id, user_id)
                old_save = self.saves.get_save(old_slot.save_id, user_id)
                if not self.saves.repository.delete_save(old_slot.save_id, user_id, int(time.time() * 1000)):
                    raise ValidationError("SAV is still in use")
                removed.append(old_save)
                delete_ids.append(old_slot.save_id)
        return {
            "requires_confirmation": False,
            "deleted_save_ids": delete_ids,
            "changed_slots": changes,
            "slots": [slot.to_dict() for slot in applied],
        }

    def _key(self, user_id: str, slot: int) -> str:
        return f"{user_id}:slot{slot}"

    def _initial_save_bytes_for_rom(self, filename: str) -> bytes:
        if rom_platform_from_filename(filename) == "n64":
            return INITIAL_N64_SAVE_BYTES
        return INITIAL_SAVE_BYTES
