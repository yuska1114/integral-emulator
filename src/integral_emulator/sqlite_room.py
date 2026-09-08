# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SQLite-backed ROOM authority."""

from __future__ import annotations

from datetime import datetime, timedelta, timezone
import secrets
import sqlite3
from typing import Any

from .database import AuthorityDatabase
from .errors import (
    AlreadyInRoomError,
    InvalidRoomCodeError,
    InvalidRoomModeError,
    RoomCodeUnavailableError,
    RoomPoolFullError,
    ValidationError,
)
from .gb_runtime_link_modes import GBRuntimeLinkMode, normalize_link_mode
from .room import (
    DEFAULT_LINK_ROOM_NUMBERS,
    DEFAULT_N64_ROOM_NUMBERS,
    LINK_ROOM_FIRST,
    LINK_ROOM_LAST,
    ROOM_CAPACITY,
    ROOM_STALE_AFTER_SECONDS,
    N64_ROOM_FIRST,
    N64_ROOM_LAST,
    ROOM_CODE_GENERATION_ATTEMPTS,
    ROOM_CODE_REUSE_COOLDOWN_SECONDS,
    Room,
)
from .room_session_policy import RoomSessionPolicy


class SQLiteRoomManager:
    """Implements ROOM operations as short row-scoped SQLite transactions."""

    def __init__(
        self,
        database: AuthorityDatabase,
        server_id: str,
        *,
        enabled_link_rooms: tuple[int, ...] = DEFAULT_LINK_ROOM_NUMBERS,
        enabled_n64_rooms: tuple[int, ...] = DEFAULT_N64_ROOM_NUMBERS,
    ):
        self.database = database
        self.server_id = server_id
        self.enabled_link_rooms = self._validated_pool(
            enabled_link_rooms, LINK_ROOM_FIRST, LINK_ROOM_LAST, "Link Cable"
        )
        self.enabled_n64_rooms = self._validated_pool(
            enabled_n64_rooms, N64_ROOM_FIRST, N64_ROOM_LAST, "N64"
        )
        self.enabled_room_numbers = self.enabled_link_rooms + self.enabled_n64_rooms
        with self.database.transaction() as connection:
            active_numbers = {
                int(row[0])
                for row in connection.execute(
                    "SELECT room_number FROM rooms WHERE server_id = ?", (self.server_id,)
                )
            }
        disabled_active = sorted(active_numbers.difference(self.enabled_room_numbers))
        if disabled_active:
            raise ValidationError(
                "active ROOMs are disabled by server configuration: "
                + ", ".join(str(number) for number in disabled_active)
            )
        self.stale_after_seconds = ROOM_STALE_AFTER_SECONDS
        self.code_reuse_cooldown_seconds = ROOM_CODE_REUSE_COOLDOWN_SECONDS

    def list_rooms(self) -> list[Room]:
        with self.database.transaction() as connection:
            records = self._load_rooms(connection)
        return [
            self._room_from_record(number, records.get(number))
            for number in self.enabled_room_numbers
        ]

    def create_room(
        self, mode: str, user_id: str, username: str, auth_session_id: str = ""
    ) -> Room:
        normalized_mode = str(mode).strip().lower()
        if normalized_mode not in {"link_cable", "n64"}:
            raise InvalidRoomModeError("invalid ROOM mode")
        pool = self.enabled_n64_rooms if normalized_mode == "n64" else self.enabled_link_rooms
        now_ms = self._now_ms()
        try:
            with self.database.transaction(write=True) as connection:
                auth_session_id = self._resolve_auth_session(
                    connection, user_id, auth_session_id, now_ms
                )
                self._require_live_auth(connection, user_id, auth_session_id, now_ms)
                if self._member_room_number(connection, user_id) is not None:
                    raise AlreadyInRoomError("already in a ROOM")
                connection.execute(
                    "DELETE FROM room_code_tombstones WHERE reuse_after_ms <= ?", (now_ms,)
                )
                occupied = {
                    int(row[0])
                    for row in connection.execute(
                        "SELECT room_number FROM rooms WHERE server_id = ?", (self.server_id,)
                    )
                }
                free = [number for number in pool if number not in occupied]
                if not free:
                    raise RoomPoolFullError("no ROOM is available")
                room_number = secrets.choice(free)
                room_code = self._generate_room_code(connection)
                connection.execute(
                    """
                    INSERT INTO rooms (
                        server_id, room_number, room_type, room_code,
                        creator_user_id, link_mode, game_started,
                        link_session_id, room_code_created_at_ms, updated_at_ms,
                        post_game_at_ms
                    ) VALUES (?, ?, ?, ?, ?, ?, 0, NULL, ?, ?, NULL)
                    """,
                    (
                        self.server_id, room_number, normalized_mode, room_code,
                        user_id, GBRuntimeLinkMode.BATTLE.value, now_ms, now_ms,
                    ),
                )
                self._insert_member(
                    connection, room_number, user_id, username, 1, auth_session_id, now_ms
                )
                connection.execute(
                    "DELETE FROM room_termination_notices WHERE server_id = ? AND user_id = ?",
                    (self.server_id, user_id),
                )
                record = self._load_room(connection, room_number)
        except sqlite3.IntegrityError as error:
            raise AlreadyInRoomError("already in a ROOM") from error
        return self._room_from_record(room_number, record)

    def join_room_by_code(
        self, room_code: str, user_id: str, username: str, auth_session_id: str = ""
    ) -> Room:
        if not self._valid_room_code(room_code):
            raise InvalidRoomCodeError("ROOM code must be exactly 5 ASCII digits")
        if not auth_session_id:
            raise ValidationError("authenticated ROOM session is required")
        now_ms = self._now_ms()
        try:
            with self.database.transaction(write=True) as connection:
                self._require_live_auth(connection, user_id, auth_session_id, now_ms)
                target = connection.execute(
                    """
                    SELECT room_number, creator_user_id FROM rooms
                    WHERE server_id = ? AND room_code = ?
                      AND game_started = 0 AND link_session_id IS NULL
                    """,
                    (self.server_id, room_code),
                ).fetchone()
                if target is None:
                    raise RoomCodeUnavailableError("ROOM is not available")
                current = self._member_room_number(connection, user_id)
                room_number = int(target["room_number"])
                self._validate_room_number(room_number)
                if current is not None:
                    if current == room_number:
                        return self._room_from_record(
                            room_number, self._load_room(connection, room_number)
                        )
                    raise AlreadyInRoomError("already in a ROOM")
                members = connection.execute(
                    """
                    SELECT seat FROM room_members
                    WHERE server_id = ? AND room_number = ? ORDER BY seat
                    """,
                    (self.server_id, room_number),
                ).fetchall()
                if len(members) != 1 or int(members[0]["seat"]) != 1:
                    raise RoomCodeUnavailableError("ROOM is not available")
                connection.execute(
                    """
                    UPDATE room_members SET ready = 0, ready_at_ms = NULL
                    WHERE server_id = ? AND room_number = ?
                    """,
                    (self.server_id, room_number),
                )
                self._insert_member(
                    connection, room_number, user_id, username, 2, auth_session_id, now_ms
                )
                connection.execute(
                    "UPDATE rooms SET updated_at_ms = ?, post_game_at_ms = NULL WHERE server_id = ? AND room_number = ?",
                    (now_ms, self.server_id, room_number),
                )
                connection.execute(
                    "DELETE FROM room_termination_notices WHERE server_id = ? AND user_id = ?",
                    (self.server_id, user_id),
                )
                record = self._load_room(connection, room_number)
        except sqlite3.IntegrityError as error:
            raise AlreadyInRoomError("already in a ROOM") from error
        return self._room_from_record(room_number, record)

    def current_room(self, user_id: str) -> Room | None:
        with self.database.transaction() as connection:
            number = self._member_room_number(connection, user_id)
            record = self._load_room(connection, number) if number is not None else None
        return self._room_from_record(number, record) if number is not None else None

    def update_user_state(
        self,
        room_number: int,
        user_id: str,
        slot: str | None = None,
        n64_slot: str | None = None,
        ready: bool | None = None,
        link_mode: str | None = None,
    ) -> Room:
        self._validate_room_number(room_number)
        now_ms = self._now_ms()
        with self.database.transaction(write=True) as connection:
            record = self._load_room(connection, room_number)
            member = self._require_member(record, user_id)
            current_mode = normalize_link_mode(record.get("link_mode"))
            if link_mode is not None and int(member["seat"]) != 1:
                raise ValidationError("only USER1 can change link mode")
            next_mode = normalize_link_mode(link_mode) if link_mode is not None else current_mode
            selection_changed = (
                slot is not None and str(member.get("slot") or "") != slot[:32]
            ) or (
                n64_slot is not None and str(member.get("n64_slot") or "") != n64_slot[:32]
            )
            if n64_slot and self._is_n64_room(room_number) and int(member["seat"]) != 1:
                raise ValidationError("USER2 cannot select an N64 ROM")
            clear_ready = next_mode != current_mode or (
                self._is_n64_room(room_number) and selection_changed
            )
            if clear_ready:
                connection.execute(
                    "UPDATE room_members SET ready = 0, ready_at_ms = NULL WHERE server_id = ? AND room_number = ?",
                    (self.server_id, room_number),
                )
            assignments = ["last_seen_at_ms = ?", "last_activity_at_ms = ?"]
            values: list[Any] = [now_ms, now_ms]
            if slot is not None:
                assignments.append("slot = ?")
                values.append(slot[:32])
            if n64_slot is not None:
                assignments.append("n64_slot = ?")
                values.append(n64_slot[:32])
            if ready is not None and not clear_ready:
                assignments.extend(["ready = ?", "ready_at_ms = ?"])
                values.extend([int(bool(ready)), now_ms if ready else None])
            values.extend([self.server_id, room_number, user_id])
            connection.execute(
                f"UPDATE room_members SET {', '.join(assignments)} WHERE server_id = ? AND room_number = ? AND user_id = ?",
                tuple(values),
            )
            connection.execute(
                "UPDATE rooms SET link_mode = ?, updated_at_ms = ?, post_game_at_ms = NULL WHERE server_id = ? AND room_number = ?",
                (next_mode, now_ms, self.server_id, room_number),
            )
            record = self._load_room(connection, room_number)
        return self._room_from_record(room_number, record)

    def heartbeat(self, user_id: str, auth_session_id: str = "") -> bool:
        now_ms = self._now_ms()
        with self.database.transaction(write=True) as connection:
            number = self._member_room_number(connection, user_id)
            if number is None:
                return False
            if auth_session_id:
                self._require_live_auth(connection, user_id, auth_session_id, now_ms)
                cursor = connection.execute(
                    """
                    UPDATE room_members SET last_seen_at_ms = ?, auth_session_id = ?
                    WHERE server_id = ? AND room_number = ? AND user_id = ?
                    """,
                    (now_ms, auth_session_id, self.server_id, number, user_id),
                )
            else:
                cursor = connection.execute(
                    "UPDATE room_members SET last_seen_at_ms = ? WHERE server_id = ? AND room_number = ? AND user_id = ?",
                    (now_ms, self.server_id, number, user_id),
                )
        return cursor.rowcount == 1

    def add_chat(self, room_number: int, user_id: str, username: str, message: str) -> Room:
        text = message.strip()
        if not text:
            raise ValidationError("message is required")
        if len(text) > 160:
            raise ValidationError("message must be 160 characters or fewer")
        now_ms = self._now_ms()
        with self.database.transaction(write=True) as connection:
            self._require_member(self._load_room(connection, room_number), user_id)
            connection.execute(
                """
                INSERT INTO room_chat
                    (server_id, room_number, user_id, username_snapshot, message, created_at_ms)
                VALUES (?, ?, ?, ?, ?, ?)
                """,
                (self.server_id, room_number, user_id, username, text, now_ms),
            )
            connection.execute(
                "UPDATE room_members SET last_seen_at_ms = ?, last_activity_at_ms = ? WHERE server_id = ? AND room_number = ? AND user_id = ?",
                (now_ms, now_ms, self.server_id, room_number, user_id),
            )
            connection.execute(
                "UPDATE rooms SET updated_at_ms = ? WHERE server_id = ? AND room_number = ?",
                (now_ms, self.server_id, room_number),
            )
            connection.execute(
                """
                DELETE FROM room_chat WHERE message_id IN (
                    SELECT message_id FROM room_chat
                    WHERE server_id = ? AND room_number = ?
                    ORDER BY message_id DESC LIMIT -1 OFFSET 32
                )
                """,
                (self.server_id, room_number),
            )
            record = self._load_room(connection, room_number)
        return self._room_from_record(room_number, record)

    def set_link_session(self, room_number: int, link_session_id: str | None) -> Room:
        return self._set_game(room_number, link_session_id, bool(link_session_id), None)

    def set_game_started(self, room_number: int, game_started: bool) -> Room:
        room = self.room(room_number)
        return self._set_game(room_number, room.link_session_id, game_started, None)

    def end_game_for_link_session(self, link_session_id: str) -> Room | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                "SELECT room_number FROM rooms WHERE server_id = ? AND link_session_id = ?",
                (self.server_id, link_session_id),
            ).fetchone()
        return self.end_game_for_room(int(row[0])) if row is not None else None

    def end_game_for_room(self, room_number: int) -> Room | None:
        self._validate_room_number(room_number)
        with self.database.transaction() as connection:
            exists = connection.execute(
                "SELECT 1 FROM rooms WHERE server_id = ? AND room_number = ?",
                (self.server_id, room_number),
            ).fetchone()
        if exists is None:
            return None
        return self._set_game(room_number, None, False, self._now_ms())

    def leave_room(self, user_id: str) -> None:
        now_ms = self._now_ms()
        with self.database.transaction(write=True) as connection:
            room_number = self._member_room_number(connection, user_id)
            if room_number is None:
                return
            room = self._load_room(connection, room_number)
            if room.get("room_code") and room["creator_user_id"] == user_id:
                self._close_room(connection, room, "room_closed_by_creator", "CLOSED", now_ms)
                return
            connection.execute(
                "DELETE FROM room_members WHERE server_id = ? AND room_number = ? AND user_id = ?",
                (self.server_id, room_number, user_id),
            )
            connection.execute(
                """
                UPDATE room_members SET ready = 0, ready_at_ms = NULL
                WHERE server_id = ? AND room_number = ?
                """,
                (self.server_id, room_number),
            )
            connection.execute(
                """
                UPDATE rooms SET link_session_id = NULL, game_started = 0,
                    updated_at_ms = ? WHERE server_id = ? AND room_number = ?
                """,
                (now_ms, self.server_id, room_number),
            )
            remaining = connection.execute(
                "SELECT COUNT(*) FROM room_members WHERE server_id = ? AND room_number = ?",
                (self.server_id, room_number),
            ).fetchone()[0]
            if not remaining:
                connection.execute(
                    "DELETE FROM rooms WHERE server_id = ? AND room_number = ?",
                    (self.server_id, room_number),
                )

    def link_session_ids_for_user(self, user_id: str) -> list[str]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT room.link_session_id FROM rooms AS room
                JOIN room_members AS member
                  ON member.server_id = room.server_id AND member.room_number = room.room_number
                WHERE room.server_id = ? AND member.user_id = ? AND room.link_session_id IS NOT NULL
                """,
                (self.server_id, user_id),
            ).fetchall()
        return [str(row[0]) for row in rows]

    def prune_stale_users(self) -> list[str]:
        now_ms = self._now_ms()
        cutoff = now_ms - self.stale_after_seconds * 1000
        pruned: list[str] = []
        with self.database.transaction(write=True) as connection:
            rooms = self._load_rooms(connection)
            for room in rooms.values():
                stale = [m for m in room["members"] if int(m["last_seen_at_ms"]) < cutoff]
                if not stale:
                    continue
                if room.get("link_session_id") and room["link_session_id"] not in pruned:
                    pruned.append(str(room["link_session_id"]))
                if any(m["user_id"] == room["creator_user_id"] for m in stale):
                    self._close_room(connection, room, "room_closed_by_creator", "CLOSED", now_ms)
                    continue
                for member in stale:
                    connection.execute(
                        "DELETE FROM room_members WHERE server_id = ? AND room_number = ? AND user_id = ?",
                        (self.server_id, room["room_number"], member["user_id"]),
                    )
                remaining = len(room["members"]) - len(stale)
                if remaining:
                    connection.execute(
                        "UPDATE room_members SET ready = 0, ready_at_ms = NULL WHERE server_id = ? AND room_number = ?",
                        (self.server_id, room["room_number"]),
                    )
                    connection.execute(
                        "UPDATE rooms SET link_session_id = NULL, game_started = 0, updated_at_ms = ? WHERE server_id = ? AND room_number = ?",
                        (now_ms, self.server_id, room["room_number"]),
                    )
                else:
                    self._close_room(connection, room, "room_stale", "EXPIRED", now_ms)
        return pruned

    def prune_idle_rooms(
        self, policy: RoomSessionPolicy, now: datetime | None = None
    ) -> list[dict[str, Any]]:
        current_ms = int((now or datetime.now(timezone.utc)).timestamp() * 1000)
        actions: list[dict[str, Any]] = []
        with self.database.transaction(write=True) as connection:
            for room in self._load_rooms(connection).values():
                members = room["members"]
                if not members or room.get("link_session_id") or room.get("game_started"):
                    continue
                activity = [int(m["last_activity_at_ms"]) for m in members]
                if room.get("post_game_at_ms") is not None:
                    activity.append(int(room["post_game_at_ms"]))
                base = max(activity)
                if base + policy.waiting_room_idle_seconds * 1000 > current_ms:
                    continue
                self._close_room(connection, room, "room_idle_timeout", "EXPIRED", current_ms)
                actions.append(
                    {"room_number": int(room["room_number"]), "status": "EXPIRED", "reason": "room_idle_timeout"}
                )
        return actions

    def recent_termination_notice(
        self, user_id: str, max_age_seconds: int = 120
    ) -> dict[str, Any] | None:
        now_ms = self._now_ms()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "DELETE FROM room_termination_notices WHERE expires_at_ms <= ?", (now_ms,)
            )
            row = connection.execute(
                """
                SELECT * FROM room_termination_notices
                WHERE server_id = ? AND user_id = ? AND updated_at_ms >= ?
                """,
                (self.server_id, user_id, now_ms - max_age_seconds * 1000),
            ).fetchone()
        if row is None:
            return None
        return {
            "kind": "room", "room_number": int(row["room_number"]),
            "status": str(row["status"]), "termination_reason": str(row["reason"]),
            "updated_at": self._iso(int(row["updated_at_ms"])),
        }

    def room(self, room_number: int) -> Room:
        self._validate_room_number(room_number)
        with self.database.transaction() as connection:
            record = self._load_room(connection, room_number)
        return self._room_from_record(room_number, record)

    def _set_game(
        self,
        room_number: int,
        link_session_id: str | None,
        game_started: bool,
        post_game_at_ms: int | None,
    ) -> Room:
        self._validate_room_number(room_number)
        now_ms = self._now_ms()
        with self.database.transaction(write=True) as connection:
            if self._load_room(connection, room_number) is None:
                raise ValidationError("room not joined")
            connection.execute(
                """
                UPDATE rooms SET link_session_id = ?, game_started = ?,
                    updated_at_ms = ?, post_game_at_ms = ?
                WHERE server_id = ? AND room_number = ?
                """,
                (
                    link_session_id, int(game_started), now_ms, post_game_at_ms,
                    self.server_id, room_number,
                ),
            )
            if not game_started:
                connection.execute(
                    "UPDATE room_members SET ready = 0, ready_at_ms = NULL WHERE server_id = ? AND room_number = ?",
                    (self.server_id, room_number),
                )
            record = self._load_room(connection, room_number)
        return self._room_from_record(room_number, record)

    def _load_rooms(self, connection: sqlite3.Connection) -> dict[int, dict[str, Any]]:
        records: dict[int, dict[str, Any]] = {}
        rows = connection.execute(
            "SELECT * FROM rooms WHERE server_id = ? ORDER BY room_number", (self.server_id,)
        ).fetchall()
        for row in rows:
            record = self._load_room(connection, int(row["room_number"]))
            if record is not None:
                records[int(row["room_number"])] = record
        return records

    def _load_room(
        self, connection: sqlite3.Connection, room_number: int | None
    ) -> dict[str, Any] | None:
        if room_number is None:
            return None
        row = connection.execute(
            "SELECT * FROM rooms WHERE server_id = ? AND room_number = ?",
            (self.server_id, room_number),
        ).fetchone()
        if row is None:
            return None
        result = dict(row)
        result["members"] = [
            dict(member)
            for member in connection.execute(
                "SELECT * FROM room_members WHERE server_id = ? AND room_number = ? ORDER BY seat",
                (self.server_id, room_number),
            ).fetchall()
        ]
        result["chat"] = [
            dict(message)
            for message in connection.execute(
                "SELECT * FROM room_chat WHERE server_id = ? AND room_number = ? ORDER BY message_id DESC LIMIT 32",
                (self.server_id, room_number),
            ).fetchall()[::-1]
        ]
        return result

    def _room_from_record(
        self, room_number: int, record: dict[str, Any] | None
    ) -> Room:
        if record is None:
            return Room(room_number=room_number, users=[], chat=[])
        users = [
            {
                "user_id": str(member["user_id"]),
                "username": str(member["username_snapshot"]),
                "slot": str(member.get("slot") or ""),
                "n64_slot": str(member.get("n64_slot") or ""),
                "ready": bool(member["ready"]),
                "last_seen_at": self._iso(int(member["last_seen_at_ms"])),
                "joined_at": self._iso(int(member["joined_at_ms"])),
                "last_activity_at": self._iso(int(member["last_activity_at_ms"])),
                "ready_at": self._optional_iso(member.get("ready_at_ms")),
                "auth_session_id_digest": str(member.get("auth_session_id") or ""),
            }
            for member in record["members"][:ROOM_CAPACITY]
        ]
        chat = [
            {
                "username": str(message["username_snapshot"]),
                "message": str(message["message"]),
                "created_at": self._iso(int(message["created_at_ms"])),
            }
            for message in record["chat"][-32:]
        ]
        creator = next(
            (m for m in record["members"] if m["user_id"] == record["creator_user_id"]), None
        )
        return Room(
            room_number=room_number,
            users=users,
            chat=chat,
            link_session_id=record.get("link_session_id"),
            link_mode=normalize_link_mode(record.get("link_mode")),
            game_started=(
                bool(record.get("game_started"))
                if self._is_n64_room(room_number)
                else bool(record.get("link_session_id"))
            ),
            updated_at=self._optional_iso(record.get("updated_at_ms")),
            post_game_at=self._optional_iso(record.get("post_game_at_ms")),
            room_code=record.get("room_code"),
            room_code_created_at=self._optional_iso(record.get("room_code_created_at_ms")),
            creator_user_id=record.get("creator_user_id"),
            creator_username=(str(creator["username_snapshot"]) if creator else None),
        )

    def _close_room(
        self,
        connection: sqlite3.Connection,
        room: dict[str, Any],
        reason: str,
        status: str,
        now_ms: int,
    ) -> None:
        for member in room["members"]:
            connection.execute(
                """
                INSERT INTO room_termination_notices (
                    server_id, user_id, room_number, status, reason,
                    updated_at_ms, expires_at_ms
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(server_id, user_id) DO UPDATE SET
                    room_number = excluded.room_number, status = excluded.status,
                    reason = excluded.reason, updated_at_ms = excluded.updated_at_ms,
                    expires_at_ms = excluded.expires_at_ms
                """,
                (
                    self.server_id, member["user_id"], room["room_number"], status,
                    reason, now_ms, now_ms + 120_000,
                ),
            )
        if room.get("room_code"):
            connection.execute(
                """
                INSERT INTO room_code_tombstones
                    (server_id, room_code, invalidated_at_ms, reuse_after_ms)
                VALUES (?, ?, ?, ?)
                ON CONFLICT(server_id, room_code) DO UPDATE SET
                    invalidated_at_ms = excluded.invalidated_at_ms,
                    reuse_after_ms = excluded.reuse_after_ms
                """,
                (
                    self.server_id, room["room_code"], now_ms,
                    now_ms + self.code_reuse_cooldown_seconds * 1000,
                ),
            )
        connection.execute(
            "DELETE FROM rooms WHERE server_id = ? AND room_number = ?",
            (self.server_id, room["room_number"]),
        )

    def _generate_room_code(self, connection: sqlite3.Connection) -> str:
        for _ in range(ROOM_CODE_GENERATION_ATTEMPTS):
            code = str(secrets.randbelow(90000) + 10000)
            unavailable = connection.execute(
                """
                SELECT 1 FROM rooms WHERE server_id = ? AND room_code = ?
                UNION ALL
                SELECT 1 FROM room_code_tombstones WHERE server_id = ? AND room_code = ?
                LIMIT 1
                """,
                (self.server_id, code, self.server_id, code),
            ).fetchone()
            if unavailable is None:
                return code
        raise RoomPoolFullError("ROOM code pool is temporarily unavailable")

    def _member_room_number(
        self, connection: sqlite3.Connection, user_id: str
    ) -> int | None:
        row = connection.execute(
            "SELECT room_number FROM room_members WHERE server_id = ? AND user_id = ?",
            (self.server_id, user_id),
        ).fetchone()
        return int(row[0]) if row is not None else None

    def _require_live_auth(
        self,
        connection: sqlite3.Connection,
        user_id: str,
        auth_session_id: str,
        now_ms: int,
    ) -> None:
        row = connection.execute(
            """
            SELECT 1 FROM auth_sessions
            WHERE token_digest = ? AND user_id = ? AND expires_at_ms > ?
              AND revoked_at_ms IS NULL
            """,
            (auth_session_id, user_id, now_ms),
        ).fetchone()
        if row is None:
            raise ValidationError("authenticated ROOM session is stale")

    def _resolve_auth_session(
        self,
        connection: sqlite3.Connection,
        user_id: str,
        auth_session_id: str,
        now_ms: int,
    ) -> str:
        if auth_session_id:
            return auth_session_id
        row = connection.execute(
            """
            SELECT token_digest FROM auth_sessions
            WHERE user_id = ? AND server_id = ? AND expires_at_ms > ?
              AND revoked_at_ms IS NULL
            ORDER BY created_at_ms DESC LIMIT 1
            """,
            (user_id, self.server_id, now_ms),
        ).fetchone()
        if row is None:
            raise ValidationError("authenticated ROOM session is required")
        return str(row[0])

    def _insert_member(
        self,
        connection: sqlite3.Connection,
        room_number: int,
        user_id: str,
        username: str,
        seat: int,
        auth_session_id: str,
        now_ms: int,
    ) -> None:
        connection.execute(
            """
            INSERT INTO room_members (
                server_id, room_number, user_id, seat, username_snapshot,
                slot, n64_slot, ready, joined_at_ms, last_seen_at_ms,
                last_activity_at_ms, ready_at_ms, auth_session_id
            ) VALUES (?, ?, ?, ?, ?, NULL, NULL, 0, ?, ?, ?, NULL, ?)
            """,
            (
                self.server_id, room_number, user_id, seat, username,
                now_ms, now_ms, now_ms, auth_session_id,
            ),
        )

    @staticmethod
    def _require_member(record: dict[str, Any] | None, user_id: str) -> dict[str, Any]:
        if record is None:
            raise ValidationError("room not joined")
        member = next((m for m in record["members"] if m["user_id"] == user_id), None)
        if member is None:
            raise ValidationError("room not joined")
        return member

    @staticmethod
    def _valid_room_code(value: str) -> bool:
        return len(value) == 5 and value.isascii() and value.isdigit()

    @staticmethod
    def _now_ms() -> int:
        return int(datetime.now(timezone.utc).timestamp() * 1000)

    @staticmethod
    def _iso(value: int) -> str:
        return datetime.fromtimestamp(value / 1000, timezone.utc).isoformat()

    @classmethod
    def _optional_iso(cls, value: Any) -> str | None:
        return cls._iso(int(value)) if value is not None else None

    @staticmethod
    def _is_n64_room(room_number: int) -> bool:
        return N64_ROOM_FIRST <= room_number <= N64_ROOM_LAST

    def _validate_room_number(self, room_number: int) -> None:
        if room_number not in self.enabled_room_numbers:
            raise ValidationError("ROOM is disabled by server configuration")

    @staticmethod
    def _validated_pool(
        room_numbers: tuple[int, ...], minimum: int, maximum: int, label: str
    ) -> tuple[int, ...]:
        normalized = tuple(sorted(room_numbers))
        if len(normalized) != len(set(normalized)):
            raise ValidationError(f"{label} ROOM configuration contains duplicates")
        if any(number < minimum or number > maximum for number in normalized):
            raise ValidationError(
                f"{label} ROOM configuration must be between {minimum} and {maximum}"
            )
        return normalized
