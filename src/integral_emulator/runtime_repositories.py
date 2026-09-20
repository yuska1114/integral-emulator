# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Concrete SQLite repositories for runtime-owned records."""

from __future__ import annotations

from contextlib import contextmanager, nullcontext
from datetime import datetime, timezone
import hashlib
import json
from typing import Any, Iterator

from .database import AuthorityDatabase
from .errors import ValidationError
from .gb_runtime_fixed_host_protocol import GB_RUNTIME_FIXED_HOST_PROTOCOL_ID
from .storage import LeagueStorage


class SQLiteRuntimeRepositories:
    """Runtime record repositories plus the associated filesystem root."""

    def __init__(self, files: LeagueStorage, database: AuthorityDatabase, server_id: str):
        self.files = files
        self.database = database
        self.server_id = server_id

    def __getattr__(self, name: str):
        return getattr(self.files, name)

    @contextmanager
    def exclusive_lock(self, name: str) -> Iterator[None]:
        with self.files.exclusive_lock(name):
            yield

    def load_link_sessions(self) -> dict[str, Any]:
        return self._load("link_sessions")

    def get_link_session(self, session_id: str) -> dict[str, Any] | None:
        return self._get_session("link_sessions", session_id, self._link_from_row)

    def insert_link_session(self, record: dict[str, Any]) -> dict[str, Any]:
        self._write_link(record, None)
        return self.get_link_session(str(record["id"]))  # type: ignore[return-value]

    def update_link_session(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        key = str(record["id"])
        self._write_link(record, row_version)
        return self.get_link_session(key)  # type: ignore[return-value]

    def delete_link_session(self, session_id: str, row_version: int) -> None:
        self._delete_row("link_sessions", session_id, row_version)

    def load_gb_runtime_fixed_host_sessions(self) -> dict[str, Any]:
        return self._load("gb_runtime_fixed_host_sessions")

    def get_gb_runtime_fixed_host_session(self, session_id: str) -> dict[str, Any] | None:
        return self._get_session("gb_runtime_fixed_host_sessions", session_id, self._fixed_from_row)

    def insert_gb_runtime_fixed_host_session(self, record: dict[str, Any]) -> dict[str, Any]:
        self._write_fixed(record, None)
        return self.get_gb_runtime_fixed_host_session(str(record["id"]))  # type: ignore[return-value]

    def update_gb_runtime_fixed_host_session(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        key = str(record["id"])
        self._write_fixed(record, row_version)
        return self.get_gb_runtime_fixed_host_session(key)  # type: ignore[return-value]

    def delete_gb_runtime_fixed_host_session(self, session_id: str, row_version: int) -> None:
        self._delete_row("gb_runtime_fixed_host_sessions", session_id, row_version)

    def load_n64_runtime_media_sessions(self) -> dict[str, Any]:
        return self._load("n64_runtime_media_sessions")

    def get_n64_runtime_media_session(self, session_id: str) -> dict[str, Any] | None:
        return self._get_session("n64_runtime_media_sessions", session_id, self._n64_from_row)

    def insert_n64_runtime_media_session(self, record: dict[str, Any]) -> dict[str, Any]:
        with self.database.write_unit() as connection:
            room = connection.execute(
                "SELECT room_code, room_code_created_at_ms FROM rooms WHERE server_id=? AND room_number=?",
                (self.server_id, record["room_number"]),
            ).fetchone()
            self._write_n64({**record, "room_code": room[0] if room else None,
                             "room_created_at_ms": room[1] if room else None}, None)
        return self.get_n64_runtime_media_session(str(record["id"]))  # type: ignore[return-value]

    def update_n64_runtime_media_session(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        key = str(record["id"])
        if record["status"] in {"EXPIRED", "COMPLETED", "CANCELLED"}:
            return self._terminate_n64_room(record, row_version)
        self._write_n64(record, row_version)
        return self.get_n64_runtime_media_session(key)  # type: ignore[return-value]

    def _terminate_n64_room(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        # The manager holds the media file lock before entering this write unit.
        # Lazy expiry (get/ticket/Relay) and the sweeper share this boundary.
        from .sqlite_repositories import SQLiteGameSessionRepository
        from .sqlite_room import SQLiteRoomManager

        key = str(record["id"])
        unit = self.database._unit_connection.get()
        with (nullcontext(unit) if unit is not None else self.database.write_unit()) as connection:
            previous = self.get_n64_runtime_media_session(key)
            if previous and previous["status"] in {"EXPIRED", "COMPLETED", "CANCELLED"}:
                return previous
            self._write_n64({**record, "recovery_deadline": None}, row_version)
            SQLiteGameSessionRepository(self.database).release_run(
                key, status=record["status"], reason=record["termination_reason"])
            rooms = SQLiteRoomManager(self.database, self.server_id)
            room = rooms._load_room(connection, int(record["room_number"]))
            newer_session = connection.execute(
                "SELECT 1 FROM n64_runtime_media_sessions WHERE server_id=? AND room_number=? "
                "AND session_id != ? AND status IN ('CREATED','WAITING_PEER','READY','RUNNING','RECOVERING')",
                (self.server_id, record["room_number"], key),
            ).fetchone()
            if (room is not None and previous and previous.get("room_code")
                    and room["room_code"] == previous["room_code"]
                    and room["room_code_created_at_ms"] == previous.get("room_created_at_ms")
                    and newer_session is None
                    and room["room_type"] == "n64"):
                if record["termination_reason"] == "preflight_failed":
                    # Same transaction as terminal media and lock release; retain
                    # the fenced ROOM, but require explicit READY from both users.
                    rooms.end_game_for_room(int(record["room_number"]))
                else:
                    rooms._close_room(connection, room, record["termination_reason"],
                                      record["status"], rooms._now_ms())
            return self.get_n64_runtime_media_session(key)  # type: ignore[return-value]

    def delete_n64_runtime_media_session(self, session_id: str, row_version: int) -> None:
        self._delete_row("n64_runtime_media_sessions", session_id, row_version)

    def mark_n64_room_started(self, session_id: str):
        from .sqlite_room import SQLiteRoomManager

        with self.database.write_unit() as connection:
            media = self.get_n64_runtime_media_session(session_id)
            if not media or media["status"] not in {"CREATED", "WAITING_PEER", "READY", "RUNNING", "RECOVERING"}:
                raise ValidationError("N64 media session is terminal")
            rooms = SQLiteRoomManager(self.database, self.server_id)
            room = rooms._load_room(connection, int(media["room_number"]))
            if (not room or not media.get("room_code") or room["room_code"] != media["room_code"]
                    or room["room_code_created_at_ms"] != media.get("room_created_at_ms")
                    or {m["user_id"] for m in room["members"]} != {media["host_user_id"], media["remote_user_id"]}):
                raise ValidationError("N64 media session ROOM binding changed")
            return rooms.set_game_started(int(media["room_number"]), True)

    def load_host_processes(self) -> dict[str, Any]:
        return self._load("host_processes")

    def get_host_process(self, process_id: str) -> dict[str, Any] | None:
        return self._get_host_process(process_id)

    def insert_host_process(self, record: dict[str, Any]) -> dict[str, Any]:
        self._write_host(record, None)
        return self.get_host_process(str(record["id"]))  # type: ignore[return-value]

    def update_host_process(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        key = str(record["id"])
        self._write_host(record, row_version)
        return self.get_host_process(key)  # type: ignore[return-value]

    def delete_host_process(self, process_id: str, row_version: int) -> None:
        self._delete_row("host_processes", process_id, row_version, "process_id")

    def load_pair_save_journals(self) -> dict[str, Any]:
        return self._load("link_save_commit_journals")

    def get_pair_save_journal(self, session_id: str) -> dict[str, Any] | None:
        return self._get_pair_journal(session_id)

    def insert_pair_save_journal(self, record: dict[str, Any]) -> dict[str, Any]:
        key = str(record["session_id"])
        self._write_pair_journal(record, None)
        return self.get_pair_save_journal(key)  # type: ignore[return-value]

    def update_pair_save_journal(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        key = str(record["session_id"])
        self._write_pair_journal(record, row_version)
        return self.get_pair_save_journal(key)  # type: ignore[return-value]

    def delete_pair_save_journal(self, session_id: str, row_version: int) -> None:
        self._delete_row("pair_save_commit_journals", session_id, row_version)

    def load_session_events(self) -> dict[str, Any]:
        return self._load("session_events")

    def append_session_event(self, record: dict[str, Any], *, coalesce: bool = False) -> None:
        self._insert_or_coalesce_event(record, coalesce=coalesce)

    def delete_session_event(self, event_id: int) -> None:
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "DELETE FROM session_events WHERE server_id=? AND event_id=?",
                (self.server_id, int(event_id)),
            )

    def load_rom_slots(self) -> dict[str, Any]:
        return self._load("rom_slots")

    def get_rom_slot(self, user_id: str, slot: int) -> dict[str, Any] | None:
        return self._get_rom_slot(user_id, slot)

    def insert_rom_slot(self, record: dict[str, Any]) -> dict[str, Any]:
        key = f"{record['user_id']}:slot{int(record['slot'])}"
        self._write_rom_slot(record, None)
        return self.get_rom_slot(str(record["user_id"]), int(record["slot"]))  # type: ignore[return-value]

    def update_rom_slot(self, record: dict[str, Any], row_version: int) -> dict[str, Any]:
        key = f"{record['user_id']}:slot{int(record['slot'])}"
        self._write_rom_slot(record, row_version)
        return self.get_rom_slot(str(record["user_id"]), int(record["slot"]))  # type: ignore[return-value]

    def delete_rom_slot(self, user_id: str, slot: int, row_version: int) -> None:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """DELETE FROM rom_slots
                   WHERE server_id=? AND user_id=? AND slot=? AND row_version=?""",
                (self.server_id, user_id, int(slot), int(row_version)),
            )
            self._require_cas(cursor.rowcount, "ROM slot")

    def _load(self, name: str) -> dict[str, Any]:
        if name == "link_sessions":
            return self._load_sessions("link_sessions", "status", self._link_from_row)
        if name == "gb_runtime_fixed_host_sessions":
            return self._load_sessions("gb_runtime_fixed_host_sessions", "state", self._fixed_from_row)
        if name == "n64_runtime_media_sessions":
            return self._load_sessions("n64_runtime_media_sessions", "status", self._n64_from_row)
        if name == "host_processes":
            return self._load_host_processes()
        if name == "link_save_commit_journals":
            return self._load_pair_journals()
        if name == "session_events":
            return self._load_events()
        if name == "rom_slots":
            return self._load_rom_slots()
        raise AssertionError(name)

    def _load_sessions(self, table: str, _status: str, converter) -> dict[str, Any]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                f"SELECT * FROM {table} WHERE server_id = ? ORDER BY created_at_ms",
                (self.server_id,),
            ).fetchall()
        return {str(row["session_id"]): converter(dict(row)) for row in rows}

    def _get_session(self, table: str, session_id: str, converter) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                f"SELECT * FROM {table} WHERE server_id=? AND session_id=?",
                (self.server_id, session_id),
            ).fetchone()
        return converter(dict(row)) if row is not None else None

    @classmethod
    def _link_from_row(cls, row: dict[str, Any]) -> dict[str, Any]:
        value = cls._metadata(row)
        value.update(
            id=row["session_id"], room_number=row["room_number"],
            player_a_user_id=row["player_a_user_id"],
            player_b_user_id=row["player_b_user_id"],
            save_a_id=row["save_a_id"], save_b_id=row["save_b_id"],
            status=row["status"], requested_link_mode=row["requested_link_mode"],
            protocol_id=row["protocol_id"], created_at=cls._iso(row["created_at_ms"]),
            updated_at=cls._iso(row["updated_at_ms"]),
            expires_at=cls._optional_iso(row["expires_at_ms"]),
            termination_reason=row["termination_reason"],
            __row_version=int(row["row_version"]),
        )
        return value

    @classmethod
    def _fixed_from_row(cls, row: dict[str, Any]) -> dict[str, Any]:
        value = cls._json(row.get("control_json"))
        value.update(
            id=row["session_id"], room_number=int(row["room_number"]),
            manifest=cls._json(row["manifest_json"]),
            manifest_digest=row["manifest_digest"], state=row["state"],
            created_at=cls._iso(row["created_at_ms"]),
            updated_at=cls._iso(row["updated_at_ms"]),
            termination_reason=row["termination_reason"],
            __row_version=int(row["row_version"]),
        )
        return value

    @classmethod
    def _n64_from_row(cls, row: dict[str, Any]) -> dict[str, Any]:
        value = cls._metadata(row)
        value.update(
            id=row["session_id"], room_number=int(row["room_number"]),
            host_user_id=row["host_user_id"], remote_user_id=row["remote_user_id"],
            status=row["status"], created_at=cls._iso(row["created_at_ms"]),
            updated_at=cls._iso(row["updated_at_ms"]),
            expires_at=cls._iso(row["expires_at_ms"]),
            termination_reason=row["termination_reason"],
            __row_version=int(row["row_version"]),
        )
        return value

    def _write_link(self, raw: dict[str, Any], expected_row_version: int | None) -> None:
        key = str(raw["id"])
        with self.database.transaction(write=True) as connection:
                value = self._clean(raw)
                metadata = self._without(
                    value,
                    "id", "room_number", "player_a_user_id", "player_b_user_id",
                    "save_a_id", "save_b_id", "status", "requested_link_mode",
                    "protocol_id", "created_at", "updated_at", "expires_at",
                    "termination_reason",
                )
                fields = (
                    self.server_id, value.get("room_number"), value["player_a_user_id"],
                    value["player_b_user_id"], value.get("save_a_id"), value.get("save_b_id"),
                    value["status"], value.get("requested_link_mode", "battle"),
                    value.get("protocol_id", GB_RUNTIME_FIXED_HOST_PROTOCOL_ID), self._ms(value["created_at"]),
                    self._ms(value["updated_at"]), self._link_due_ms(value),
                    self._optional_ms(value.get("expires_at")),
                    value.get("termination_reason"), self._bounded(metadata, 65536),
                )
                if expected_row_version is None:
                    connection.execute(
                        """
                        INSERT INTO link_sessions (
                            session_id, server_id, room_number, player_a_user_id,
                            player_b_user_id, save_a_id, save_b_id, status,
                            requested_link_mode, protocol_id, created_at_ms,
                            updated_at_ms, lease_expires_at_ms, expires_at_ms,
                            termination_reason, metadata_json
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (key, *fields),
                    )
                else:
                    cursor = connection.execute(
                        """
                        UPDATE link_sessions SET
                            server_id=?, room_number=?, player_a_user_id=?,
                            player_b_user_id=?, save_a_id=?, save_b_id=?, status=?,
                            requested_link_mode=?, protocol_id=?, created_at_ms=?,
                            updated_at_ms=?, lease_expires_at_ms=?, expires_at_ms=?,
                            termination_reason=?, metadata_json=?,
                            row_version=row_version+1
                        WHERE session_id=? AND row_version=?
                        """,
                        (*fields, key, int(expected_row_version)),
                    )
                    self._require_cas(cursor.rowcount, "Link session")

    def _write_fixed(self, raw: dict[str, Any], expected_row_version: int | None) -> None:
        key = str(raw["id"])
        with self.database.transaction(write=True) as connection:
                value = self._clean(raw)
                control = self._without(
                    value, "id", "room_number", "manifest", "manifest_digest",
                    "state", "created_at", "updated_at", "termination_reason",
                )
                due = self._optional_ms(value.get("pause_expires_at"))
                fields = (
                    self.server_id, int(value["room_number"]), value["state"],
                    value["manifest_digest"], self._bounded(value["manifest"], 65536),
                    self._ms(value["created_at"]), self._ms(value["updated_at"]), due,
                    value.get("termination_reason"), self._bounded(control, 65536),
                )
                if expected_row_version is None:
                    connection.execute(
                        """
                        INSERT INTO gb_runtime_fixed_host_sessions (
                            session_id, server_id, room_number, state, manifest_digest,
                            manifest_json, created_at_ms, updated_at_ms, due_at_ms,
                            termination_reason, control_json
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (key, *fields),
                    )
                else:
                    cursor = connection.execute(
                        """
                        UPDATE gb_runtime_fixed_host_sessions SET server_id=?, room_number=?, state=?,
                            manifest_digest=?, manifest_json=?, created_at_ms=?, updated_at_ms=?,
                            due_at_ms=?, termination_reason=?, control_json=?,
                            row_version=row_version+1
                        WHERE session_id=? AND row_version=?
                        """,
                        (*fields, key, int(expected_row_version)),
                    )
                    self._require_cas(cursor.rowcount, "Fixed Host session")

    def _write_n64(self, raw: dict[str, Any], expected_row_version: int | None) -> None:
        key = str(raw["id"])
        with self.database.transaction(write=True) as connection:
                value = self._clean(raw)
                metadata = self._without(
                    value, "id", "room_number", "host_user_id", "remote_user_id",
                    "status", "created_at", "updated_at", "expires_at",
                    "termination_reason",
                )
                fields = (
                    self.server_id, int(value["room_number"]), value["host_user_id"],
                    value["remote_user_id"], value["status"],
                    self._ms(value["created_at"]), self._ms(value["updated_at"]),
                    self._ms(value["expires_at"]),
                    value.get("termination_reason"), self._bounded(metadata, 65536),
                )
                if expected_row_version is None:
                    connection.execute(
                        """
                        INSERT INTO n64_runtime_media_sessions (
                            session_id, server_id, room_number, host_user_id,
                            remote_user_id, status, created_at_ms, updated_at_ms,
                            expires_at_ms,
                            termination_reason, metadata_json
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (key, *fields),
                    )
                else:
                    cursor = connection.execute(
                        """
                        UPDATE n64_runtime_media_sessions SET server_id=?, room_number=?,
                            host_user_id=?, remote_user_id=?, status=?, created_at_ms=?,
                            updated_at_ms=?, expires_at_ms=?,
                            termination_reason=?, metadata_json=?,
                            row_version=row_version+1
                        WHERE session_id=? AND row_version=?
                        """,
                        (*fields, key, int(expected_row_version)),
                    )
                    self._require_cas(cursor.rowcount, "N64 media session")

    def _load_host_processes(self) -> dict[str, Any]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                "SELECT * FROM host_processes WHERE server_id = ?", (self.server_id,)
            ).fetchall()
        result = {}
        for row in rows:
            value = self._json(row["metadata_json"])
            value.update(
                id=row["process_id"], session_id=row["session_id"], status=row["status"],
                pid=row["pid"], exit_code=row["exit_code"], dry_run=bool(row["dry_run"]),
                created_at=self._iso(row["created_at_ms"]),
                updated_at=self._iso(row["updated_at_ms"]),
                __row_version=int(row["row_version"]),
            )
            result[str(row["process_id"])] = value
        return result

    def _get_host_process(self, process_id: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                "SELECT * FROM host_processes WHERE server_id=? AND process_id=?",
                (self.server_id, process_id),
            ).fetchone()
        if row is None:
            return None
        value = self._json(row["metadata_json"])
        value.update(
            id=row["process_id"], session_id=row["session_id"], status=row["status"],
            pid=row["pid"], exit_code=row["exit_code"], dry_run=bool(row["dry_run"]),
            created_at=self._iso(row["created_at_ms"]), updated_at=self._iso(row["updated_at_ms"]),
            __row_version=int(row["row_version"]),
        )
        return value

    def _write_host(self, raw: dict[str, Any], expected_row_version: int | None) -> None:
        key = str(raw["id"])
        with self.database.transaction(write=True) as connection:
                value = self._clean(raw)
                metadata = self._without(
                    value, "id", "session_id", "status", "pid", "exit_code",
                    "dry_run", "created_at", "updated_at",
                )
                fields = (
                    self.server_id, value["session_id"], value["status"], value.get("pid"),
                    value.get("exit_code"), int(bool(value.get("dry_run"))),
                    self._ms(value["created_at"]), self._ms(value["updated_at"]),
                    self._bounded(metadata, 16384),
                )
                if expected_row_version is None:
                    connection.execute(
                        """
                        INSERT INTO host_processes (
                            process_id, server_id, session_id, status, pid, exit_code,
                            dry_run, created_at_ms, updated_at_ms, metadata_json
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (key, *fields),
                    )
                else:
                    cursor = connection.execute(
                        """
                        UPDATE host_processes SET server_id=?, session_id=?, status=?,
                            pid=?, exit_code=?, dry_run=?, created_at_ms=?, updated_at_ms=?,
                            metadata_json=?, row_version=row_version+1
                        WHERE process_id=? AND row_version=?
                        """,
                        (*fields, key, int(expected_row_version)),
                    )
                    self._require_cas(cursor.rowcount, "Host process")

    def _load_pair_journals(self) -> dict[str, Any]:
        with self.database.transaction() as connection:
            rows = connection.execute("SELECT * FROM pair_save_commit_journals").fetchall()
        result = {}
        for row in rows:
            value = self._json(row["detail_json"])
            value.update(
                session_id=row["session_id"], state=row["state"],
                created_at=self._iso(row["created_at_ms"]),
                updated_at=self._iso(row["updated_at_ms"]),
                __row_version=int(row["row_version"]),
            )
            result[str(row["session_id"])] = value
        return result

    def _get_pair_journal(self, session_id: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                "SELECT * FROM pair_save_commit_journals WHERE session_id=?",
                (session_id,),
            ).fetchone()
        if row is None:
            return None
        value = self._json(row["detail_json"])
        value.update(
            session_id=row["session_id"], state=row["state"],
            created_at=self._iso(row["created_at_ms"]),
            updated_at=self._iso(row["updated_at_ms"]),
            __row_version=int(row["row_version"]),
        )
        return value

    def _write_pair_journal(
        self, raw: dict[str, Any], expected_row_version: int | None
    ) -> None:
        key = str(raw["session_id"])
        with self.database.transaction(write=True) as connection:
                value = self._clean(raw)
                detail = self._without(value, "session_id", "state", "created_at", "updated_at")
                players = value.get("players") or {}
                a = players.get("player_a") or value.get("player_a") or {}
                b = players.get("player_b") or value.get("player_b") or {}
                created_at = (
                    value.get("created_at") or value.get("prepared_at")
                    or value.get("updated_at")
                )
                updated_at = value.get("updated_at") or created_at
                fields = (
                    value["state"], a.get("save_id") or value.get("player_a_save_id"),
                    b.get("save_id") or value.get("player_b_save_id"),
                    self._optional_int(
                        a.get("base_revision", value.get("player_a_expected_revision"))
                    ),
                    self._optional_int(
                        b.get("base_revision", value.get("player_b_expected_revision"))
                    ),
                    a.get("payload_sha256") or a.get("candidate_sha256")
                    or value.get("player_a_candidate_sha256"),
                    b.get("payload_sha256") or b.get("candidate_sha256")
                    or value.get("player_b_candidate_sha256"),
                    self._ms(created_at), self._ms(updated_at),
                    self._bounded(detail, 32768),
                )
                if expected_row_version is None:
                    connection.execute(
                        """
                        INSERT INTO pair_save_commit_journals (
                            session_id, state, player_a_save_id, player_b_save_id,
                            player_a_expected_revision, player_b_expected_revision,
                            player_a_candidate_sha256, player_b_candidate_sha256,
                            created_at_ms, updated_at_ms, detail_json
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (key, *fields),
                    )
                else:
                    cursor = connection.execute(
                        """
                        UPDATE pair_save_commit_journals SET state=?, player_a_save_id=?,
                            player_b_save_id=?, player_a_expected_revision=?,
                            player_b_expected_revision=?, player_a_candidate_sha256=?,
                            player_b_candidate_sha256=?, created_at_ms=?, updated_at_ms=?,
                            detail_json=?, row_version=row_version+1
                        WHERE session_id=? AND row_version=?
                        """,
                        (*fields, key, int(expected_row_version)),
                    )
                    self._require_cas(cursor.rowcount, "Pair-save journal")

    def _load_events(self) -> dict[str, Any]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                "SELECT * FROM session_events WHERE server_id = ? ORDER BY event_id",
                (self.server_id,),
            ).fetchall()
        result = {}
        for row in rows:
            value = {
                "id": str(row["event_id"]), "session_id": row["session_id"],
                "event_type": row["event_type"],
                "payload": self._json(row["payload_json"]),
                "created_at": self._iso(row["last_created_at_ms"]),
            }
            if row["coalesce_key"]:
                value["first_created_at"] = self._iso(row["first_created_at_ms"])
                value["occurrences"] = int(row["occurrences"])
            result[str(row["event_id"])] = value
        return result

    def _load_rom_slots(self) -> dict[str, Any]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT slot.*, rom.sha256, rom.sha1, rom.game_type,
                       rom.rom_header_title
                FROM rom_slots AS slot
                LEFT JOIN rom_registrations AS rom ON rom.rom_id = slot.rom_id
                WHERE slot.server_id = ?
                ORDER BY slot.user_id, slot.slot
                """,
                (self.server_id,),
            ).fetchall()
        result = {}
        for row in rows:
            key = f"{row['user_id']}:slot{row['slot']}"
            result[key] = {
                "user_id": row["user_id"], "slot": int(row["slot"]),
                "rom_id": row["rom_id"], "save_id": row["save_id"],
                "filename": row["filename"], "sha256": row["sha256"],
                "sha1": row["sha1"], "game_type": row["game_type"],
                "rom_header_title": row["rom_header_title"],
                "updated_at": self._iso(row["updated_at_ms"]),
                "__row_version": int(row["row_version"]),
            }
        return result

    def _get_rom_slot(self, user_id: str, slot: int) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT slot.*, rom.sha256, rom.sha1, rom.game_type,
                       rom.rom_header_title
                FROM rom_slots AS slot
                LEFT JOIN rom_registrations AS rom ON rom.rom_id = slot.rom_id
                WHERE slot.server_id=? AND slot.user_id=? AND slot.slot=?
                """,
                (self.server_id, user_id, int(slot)),
            ).fetchone()
        if row is None:
            return None
        return {
            "user_id": row["user_id"], "slot": int(row["slot"]),
            "rom_id": row["rom_id"], "save_id": row["save_id"],
            "filename": row["filename"], "sha256": row["sha256"],
            "sha1": row["sha1"], "game_type": row["game_type"],
            "rom_header_title": row["rom_header_title"],
            "updated_at": self._iso(row["updated_at_ms"]),
            "__row_version": int(row["row_version"]),
        }

    def _write_rom_slot(
        self, raw: dict[str, Any], expected_row_version: int | None
    ) -> None:
        with self.database.transaction(write=True) as connection:
                value = self._clean(raw)
                if expected_row_version is None:
                    connection.execute(
                        """
                        INSERT INTO rom_slots (
                            server_id, user_id, slot, rom_id, save_id, filename,
                            updated_at_ms
                        ) VALUES (?, ?, ?, ?, ?, ?, ?)
                        """,
                        (
                            self.server_id, value["user_id"], int(value["slot"]),
                            value.get("rom_id"), value.get("save_id"),
                            value.get("filename"), self._ms(value["updated_at"]),
                        ),
                    )
                else:
                    cursor = connection.execute(
                        """
                        UPDATE rom_slots SET rom_id=?, save_id=?, filename=?, updated_at_ms=?,
                            row_version=row_version+1
                        WHERE server_id=? AND user_id=? AND slot=? AND row_version=?
                        """,
                        (
                            value.get("rom_id"), value.get("save_id"),
                            value.get("filename"), self._ms(value["updated_at"]),
                            self.server_id, value["user_id"], int(value["slot"]),
                            int(expected_row_version),
                        ),
                    )
                    self._require_cas(cursor.rowcount, "ROM slot")

    def _insert_or_coalesce_event(self, record: dict[str, Any], *, coalesce: bool) -> None:
        value = self._clean(record)
        payload = value.get("payload") or {}
        timestamp_ms = self._ms(value["created_at"])
        coalesce_key = None
        if coalesce:
            coalesce_key = hashlib.sha256(
                (f"{self.server_id}\0{value['session_id']}\0{value['event_type']}\0"
                 f"{payload.get('user_id', '')}").encode("utf-8")
            ).hexdigest()
        with self.database.transaction(write=True) as connection:
            if coalesce_key is not None:
                connection.execute(
                    """
                    INSERT INTO session_events (
                        server_id, session_id, event_type, payload_json,
                        coalesce_key, first_created_at_ms,
                        last_created_at_ms, occurrences
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, 1)
                    ON CONFLICT(coalesce_key) WHERE coalesce_key IS NOT NULL DO UPDATE SET
                        payload_json=excluded.payload_json,
                        last_created_at_ms=excluded.last_created_at_ms,
                        occurrences=session_events.occurrences+1
                    """,
                    (
                        self.server_id, value["session_id"], value["event_type"],
                        self._bounded(payload, 8192), coalesce_key,
                        timestamp_ms, timestamp_ms,
                    ),
                )
                return
            connection.execute(
                """
                INSERT INTO session_events (
                    server_id, session_id, event_type, payload_json, coalesce_key,
                    first_created_at_ms, last_created_at_ms, occurrences
                ) VALUES (?, ?, ?, ?, ?, ?, ?, 1)
                """,
                (
                    self.server_id, value["session_id"], value["event_type"],
                    self._bounded(payload, 8192), coalesce_key,
                    timestamp_ms, timestamp_ms,
                ),
            )

    @staticmethod
    def _clean(value: dict[str, Any]) -> dict[str, Any]:
        return {key: item for key, item in value.items() if not key.startswith("__")}

    @staticmethod
    def _without(value: dict[str, Any], *keys: str) -> dict[str, Any]:
        omitted = set(keys)
        return {key: item for key, item in value.items() if key not in omitted}

    @staticmethod
    def _optional_int(value: Any) -> int | None:
        return int(value) if value is not None else None

    @staticmethod
    def _metadata(row: dict[str, Any]) -> dict[str, Any]:
        return SQLiteRuntimeRepositories._json(row.get("metadata_json"))

    @staticmethod
    def _json(value: Any) -> dict[str, Any]:
        if value is None:
            return {}
        if isinstance(value, dict):
            return dict(value)
        decoded = json.loads(value)
        return dict(decoded) if isinstance(decoded, dict) else {}

    @staticmethod
    def _bounded(value: Any, maximum: int) -> str:
        encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
        if len(encoded.encode("utf-8")) > maximum:
            raise ValidationError("SQLite runtime metadata is too large")
        return encoded

    @staticmethod
    def _ms(value: str) -> int:
        parsed = datetime.fromisoformat(value)
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return int(parsed.timestamp() * 1000)

    @classmethod
    def _optional_ms(cls, value: str | None) -> int | None:
        return cls._ms(value) if value else None

    @classmethod
    def _link_due_ms(cls, value: dict[str, Any]) -> int | None:
        expires_at = value.get("expires_at")
        return cls._ms(expires_at) if expires_at else None

    @staticmethod
    def _iso(value: int) -> str:
        return datetime.fromtimestamp(int(value) / 1000, timezone.utc).isoformat()

    @classmethod
    def _optional_iso(cls, value: int | None) -> str | None:
        return cls._iso(value) if value is not None else None

    @staticmethod
    def _require_cas(rowcount: int, label: str) -> None:
        if rowcount != 1:
            raise ValidationError(f"{label} was changed concurrently")

    def _delete_row(
        self, table: str, key: str, row_version: int,
        id_column: str = "session_id",
    ) -> None:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                f"DELETE FROM {table} WHERE {id_column}=? AND row_version=?",
                (key, int(row_version)),
            )
            self._require_cas(cursor.rowcount, table)
