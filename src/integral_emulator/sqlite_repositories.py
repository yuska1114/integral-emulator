# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Normalized SQLite repositories for ROOM and game-session authority."""

from __future__ import annotations

import json
import sqlite3
from collections.abc import Callable
from typing import Any

from .database import AuthorityDatabase


class RepositoryConflictError(RuntimeError):
    pass


class SQLiteRateLimitRepository:
    def __init__(self, database: AuthorityDatabase):
        self.database = database

    def consume_many(
        self, rules: list[tuple[str, int, int]], now_ms: int
    ) -> bool:
        if not rules:
            return True
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "DELETE FROM rate_limit_buckets WHERE expires_at_ms <= ?", (now_ms,)
            )
            current: dict[str, int] = {}
            for key, limit, _window_ms in rules:
                row = connection.execute(
                    "SELECT attempt_count FROM rate_limit_buckets WHERE scope_key = ?",
                    (key,),
                ).fetchone()
                count = int(row[0]) if row is not None else 0
                if count >= limit:
                    return False
                current[key] = count
            for key, _limit, window_ms in rules:
                if current[key] == 0:
                    connection.execute(
                        """
                        INSERT INTO rate_limit_buckets (
                            scope_key, window_started_at_ms, attempt_count,
                            expires_at_ms
                        ) VALUES (?, ?, 1, ?)
                        """,
                        (key, now_ms, now_ms + window_ms),
                    )
                else:
                    connection.execute(
                        """
                        UPDATE rate_limit_buckets
                        SET attempt_count = attempt_count + 1
                        WHERE scope_key = ?
                        """,
                        (key,),
                    )
        return True


class SQLiteAuthRepository:
    def __init__(self, database: AuthorityDatabase):
        self.database = database

    def create_user(self, record: dict[str, Any]) -> None:
        try:
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    """
                    INSERT INTO users (
                        user_id, username, username_normalized, email, password_hash, status,
                        must_change_password, created_at_ms, updated_at_ms
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        record["user_id"], record["username"],
                        record["username_normalized"],
                        record.get("email") or None, record["password_hash"],
                        record.get("status", "active"),
                        int(bool(record.get("must_change_password"))),
                        record["created_at_ms"], record["updated_at_ms"],
                    ),
                )
        except sqlite3.IntegrityError as error:
            raise RepositoryConflictError("username already exists") from error

    def create_session(self, record: dict[str, Any]) -> None:
        with self.database.transaction(write=True) as connection:
            connection.execute(
                """
                INSERT INTO auth_sessions (
                    token_digest, user_id, server_id, created_at_ms,
                    expires_at_ms, last_access_at_ms
                ) VALUES (?, ?, ?, ?, ?, ?)
                """,
                (
                    record["token_digest"], record["user_id"], record["server_id"],
                    record["created_at_ms"], record["expires_at_ms"],
                    record["last_access_at_ms"],
                ),
            )

    def find_user_by_username(self, username: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                "SELECT * FROM users WHERE username_normalized = ?", (username,)
            ).fetchone()
        return dict(row) if row is not None else None

    def list_users(self) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                "SELECT * FROM users ORDER BY username_normalized"
            ).fetchall()
        return [dict(row) for row in rows]

    def update_password(
        self,
        user_id: str,
        password_hash: str,
        *,
        must_change_password: bool,
        updated_at_ms: int,
    ) -> dict[str, Any] | None:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE users SET password_hash = ?, must_change_password = ?,
                    updated_at_ms = ? WHERE user_id = ?
                """,
                (
                    password_hash, int(must_change_password), updated_at_ms, user_id,
                ),
            )
            if cursor.rowcount != 1:
                return None
            row = connection.execute(
                "SELECT * FROM users WHERE user_id = ?", (user_id,)
            ).fetchone()
        return dict(row)

    def reset_password_if_idle(
        self,
        username: str,
        password_hash_factory: Callable[[], str],
        *,
        updated_at_ms: int,
    ) -> tuple[str, dict[str, Any] | None, int]:
        """Atomically guard, reset, and revoke sessions for one user."""
        with self.database.transaction(write=True) as connection:
            row = connection.execute(
                "SELECT * FROM users WHERE username_normalized = ?", (username,)
            ).fetchone()
            if row is None:
                return "not_found", None, 0
            user_id = str(row["user_id"])
            if connection.execute(
                "SELECT 1 FROM room_members WHERE user_id = ? LIMIT 1", (user_id,)
            ).fetchone() is not None:
                return "in_room", dict(row), 0
            if connection.execute(
                """
                SELECT 1
                FROM game_session_locks AS lock
                JOIN game_runs AS run ON run.game_run_id = lock.game_run_id
                WHERE lock.user_id = ? AND lock.lease_expires_at_ms > ?
                  AND run.status IN ('RUNNING', 'FINALIZING', 'RECOVERING')
                  AND run.expires_at_ms > ?
                LIMIT 1
                """,
                (user_id, updated_at_ms, updated_at_ms),
            ).fetchone() is not None:
                return "game_running", dict(row), 0

            password_hash = password_hash_factory()
            connection.execute(
                """
                UPDATE users SET password_hash = ?, must_change_password = 1,
                    updated_at_ms = ? WHERE user_id = ?
                """,
                (password_hash, updated_at_ms, user_id),
            )
            revoked = connection.execute(
                """
                UPDATE auth_sessions SET revoked_at_ms = ?
                WHERE user_id = ? AND revoked_at_ms IS NULL
                """,
                (updated_at_ms, user_id),
            ).rowcount
            updated = connection.execute(
                "SELECT * FROM users WHERE user_id = ?", (user_id,)
            ).fetchone()
        return "updated", dict(updated), revoked

    def find_session_and_user(
        self, token_digest: str
    ) -> tuple[dict[str, Any], dict[str, Any]] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT
                    s.token_digest AS session_token_digest,
                    s.user_id AS session_user_id,
                    s.server_id AS session_server_id,
                    s.created_at_ms AS session_created_at_ms,
                    s.expires_at_ms AS session_expires_at_ms,
                    s.last_access_at_ms AS session_last_access_at_ms,
                    u.*
                FROM auth_sessions s
                JOIN users u ON u.user_id = s.user_id
                WHERE s.token_digest = ? AND s.revoked_at_ms IS NULL
                """,
                (token_digest,),
            ).fetchone()
        if row is None:
            return None
        session = {
            "token_digest": row["session_token_digest"],
            "user_id": row["session_user_id"],
            "server_id": row["session_server_id"],
            "created_at_ms": row["session_created_at_ms"],
            "expires_at_ms": row["session_expires_at_ms"],
            "last_access_at_ms": row["session_last_access_at_ms"],
        }
        user = {
            key: row[key]
            for key in (
                "user_id", "username", "email", "password_hash", "status",
                "must_change_password", "created_at_ms", "updated_at_ms",
            )
        }
        return session, user

    def find_identity_by_token_digest(
        self, token_digest: str, now_ms: int
    ) -> tuple[dict[str, Any], dict[str, Any]] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT s.*, u.* FROM auth_sessions s
                JOIN users u ON u.user_id = s.user_id
                WHERE s.token_digest = ? AND s.expires_at_ms > ? AND u.status = 'active'
                """,
                (token_digest, now_ms),
            ).fetchone()
            if row is None:
                return None
            session = {key: row[key] for key in (
                "token_digest", "user_id", "server_id", "created_at_ms",
                "expires_at_ms", "last_access_at_ms",
            )}
            user = {key: row[key] for key in (
                "user_id", "username", "email", "password_hash", "status",
                "must_change_password", "updated_at_ms",
            )}
            return session, user

    def touch_session(
        self, token_digest: str, previous_access_ms: int, access_ms: int
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE auth_sessions SET last_access_at_ms = ?
                WHERE token_digest = ? AND last_access_at_ms = ?
                  AND expires_at_ms > ? AND revoked_at_ms IS NULL
                """,
                (access_ms, token_digest, previous_access_ms, access_ms),
            )
            return cursor.rowcount == 1

    def delete_session(self, token_digest: str, revoked_at_ms: int | None = None) -> bool:
        with self.database.transaction(write=True) as connection:
            return connection.execute(
                """
                UPDATE auth_sessions SET revoked_at_ms = ?
                WHERE token_digest = ? AND revoked_at_ms IS NULL
                """,
                (revoked_at_ms or 1, token_digest),
            ).rowcount == 1

    def delete_sessions_for_user(self, user_id: str, revoked_at_ms: int | None = None) -> int:
        with self.database.transaction(write=True) as connection:
            return connection.execute(
                """
                UPDATE auth_sessions SET revoked_at_ms = ?
                WHERE user_id = ? AND revoked_at_ms IS NULL
                """,
                (revoked_at_ms or 1, user_id),
            ).rowcount

    def delete_expired_sessions(self, now_ms: int) -> int:
        with self.database.transaction(write=True) as connection:
            return connection.execute(
                """
                UPDATE auth_sessions SET revoked_at_ms = ?
                WHERE expires_at_ms <= ? AND revoked_at_ms IS NULL
                """,
                (now_ms, now_ms),
            ).rowcount


class SQLiteAssetRepository:
    def __init__(self, database: AuthorityDatabase):
        self.database = database

    def register_rom(self, record: dict[str, Any]) -> None:
        try:
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    """
                    INSERT INTO rom_registrations (
                        rom_id, server_id, user_id, sha256, sha1, game_type,
                        title, region, verified_name, rom_header_title, size_bytes,
                        created_at_ms, updated_at_ms
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    tuple(record.get(key) for key in (
                        "rom_id", "server_id", "user_id", "sha256", "sha1",
                        "game_type", "title", "region", "verified_name",
                        "rom_header_title", "size_bytes", "created_at_ms", "updated_at_ms",
                    )),
                )
        except sqlite3.IntegrityError as error:
            raise RepositoryConflictError("ROM registration conflicts") from error

    def get_rom(self, rom_id: str, user_id: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT * FROM rom_registrations
                WHERE rom_id = ? AND user_id = ?
                """,
                (rom_id, user_id),
            ).fetchone()
        return dict(row) if row is not None else None

    def find_rom_by_hash(
        self, server_id: str, user_id: str, sha256: str
    ) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT * FROM rom_registrations
                WHERE server_id = ? AND user_id = ? AND sha256 = ?
                """,
                (server_id, user_id, sha256),
            ).fetchone()
        return dict(row) if row is not None else None

    def list_roms(self, server_id: str, user_id: str) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT * FROM rom_registrations
                WHERE server_id = ? AND user_id = ?
                ORDER BY created_at_ms, rom_id
                """,
                (server_id, user_id),
            ).fetchall()
        return [dict(row) for row in rows]

    def update_rom_header_title(
        self,
        rom_id: str,
        user_id: str,
        expected_header_title: str | None,
        header_title: str,
        updated_at_ms: int,
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE rom_registrations
                SET rom_header_title = ?, updated_at_ms = ?
                WHERE rom_id = ? AND user_id = ?
                  AND rom_header_title IS ?
                """,
                (
                    header_title,
                    updated_at_ms,
                    rom_id,
                    user_id,
                    expected_header_title,
                ),
            )
        return cursor.rowcount == 1

    def create_save(self, record: dict[str, Any]) -> None:
        if record["relative_path"].startswith("/") or ".." in record["relative_path"].split("/"):
            raise ValueError("save path escapes storage root")
        with self.database.transaction(write=True) as connection:
            connection.execute(
                """
                INSERT INTO save_records (
                    save_id, server_id, user_id, rom_id, game_type, relative_path,
                    size_bytes, sha256, revision, created_at_ms, updated_at_ms
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                tuple(record.get(key) for key in (
                    "save_id", "server_id", "user_id", "rom_id", "game_type", "relative_path",
                    "size_bytes", "sha256", "revision", "created_at_ms", "updated_at_ms",
                )),
            )

    def get_save(self, save_id: str, user_id: str | None = None) -> dict[str, Any] | None:
        parameters: list[Any] = [save_id]
        owner_filter = ""
        if user_id is not None:
            owner_filter = " AND s.user_id = ?"
            parameters.append(user_id)
        with self.database.transaction() as connection:
            row = connection.execute(
                f"""
                SELECT s.*, l.owner_id AS lock_owner_id,
                       l.game_run_id AS lock_game_run_id,
                       l.auth_session_id AS lock_auth_session_id,
                       l.fencing_token AS lock_fencing_token,
                       l.lease_expires_at_ms AS lock_expires_at_ms
                FROM save_records AS s
                LEFT JOIN save_locks AS l ON l.save_id = s.save_id
                WHERE s.save_id = ?{owner_filter}
                """,
                tuple(parameters),
            ).fetchone()
        return dict(row) if row is not None else None

    def list_saves(self, server_id: str, user_id: str) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT s.*, l.owner_id AS lock_owner_id,
                       l.game_run_id AS lock_game_run_id,
                       l.auth_session_id AS lock_auth_session_id,
                       l.fencing_token AS lock_fencing_token,
                       l.lease_expires_at_ms AS lock_expires_at_ms
                FROM save_records AS s
                LEFT JOIN save_locks AS l ON l.save_id = s.save_id
                WHERE s.server_id = ? AND s.user_id = ?
                ORDER BY s.game_type, s.created_at_ms, s.save_id
                """,
                (server_id, user_id),
            ).fetchall()
        return [dict(row) for row in rows]

    def find_save_by_game(
        self, server_id: str, user_id: str, game_type: str
    ) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT * FROM save_records
                WHERE server_id = ? AND user_id = ? AND game_type = ?
                  AND relative_path NOT LIKE '%/slots/%'
                ORDER BY created_at_ms, save_id
                LIMIT 1
                """,
                (server_id, user_id, game_type),
            ).fetchone()
        return dict(row) if row is not None else None

    def delete_save(self, save_id: str, user_id: str, now_ms: int) -> bool:
        try:
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    """
                    DELETE FROM save_locks
                    WHERE save_id = ? AND user_id = ? AND lease_expires_at_ms <= ?
                    """,
                    (save_id, user_id, now_ms),
                )
                cursor = connection.execute(
                    """
                    DELETE FROM save_records
                    WHERE save_id = ? AND user_id = ?
                      AND NOT EXISTS (
                          SELECT 1 FROM save_locks WHERE save_locks.save_id = save_records.save_id
                      )
                    """,
                    (save_id, user_id),
                )
        except sqlite3.IntegrityError as error:
            raise RepositoryConflictError("SAV is still referenced") from error
        return cursor.rowcount == 1

    def list_slots(self, server_id: str, user_id: str) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT slot.*, rom.sha256, rom.sha1, rom.game_type,
                       rom.rom_header_title
                FROM rom_slots AS slot
                LEFT JOIN rom_registrations AS rom ON rom.rom_id = slot.rom_id
                WHERE slot.server_id = ? AND slot.user_id = ?
                ORDER BY slot.slot
                """,
                (server_id, user_id),
            ).fetchall()
        return [dict(row) for row in rows]

    def upsert_slot(self, record: dict[str, Any]) -> None:
        with self.database.transaction(write=True) as connection:
            connection.execute(
                """
                INSERT INTO rom_slots (
                    server_id, user_id, slot, rom_id, save_id, filename, updated_at_ms
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(server_id, user_id, slot) DO UPDATE SET
                    rom_id = excluded.rom_id,
                    save_id = excluded.save_id,
                    filename = excluded.filename,
                    updated_at_ms = excluded.updated_at_ms
                """,
                tuple(record.get(key) for key in (
                    "server_id", "user_id", "slot", "rom_id", "save_id",
                    "filename", "updated_at_ms",
                )),
            )

    def delete_slot(self, server_id: str, user_id: str, slot: int) -> bool:
        with self.database.transaction(write=True) as connection:
            return connection.execute(
                "DELETE FROM rom_slots WHERE server_id = ? AND user_id = ? AND slot = ?",
                (server_id, user_id, slot),
            ).rowcount == 1

    def acquire_save_lock(self, record: dict[str, Any], now_ms: int) -> bool:
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "DELETE FROM save_locks WHERE save_id = ? AND lease_expires_at_ms <= ?",
                (record["save_id"], now_ms),
            )
            cursor = connection.execute(
                """
                INSERT INTO save_locks (
                    save_id, user_id, owner_id, game_run_id, auth_session_id,
                    fencing_token, lease_expires_at_ms
                )
                SELECT ?, ?, ?, ?, ?, ?, ?
                WHERE EXISTS (
                    SELECT 1 FROM save_records WHERE save_id = ? AND user_id = ?
                )
                ON CONFLICT(save_id) DO UPDATE SET
                    lease_expires_at_ms = excluded.lease_expires_at_ms
                WHERE save_locks.user_id = excluded.user_id
                  AND save_locks.owner_id = excluded.owner_id
                  AND save_locks.game_run_id IS excluded.game_run_id
                  AND save_locks.auth_session_id = excluded.auth_session_id
                  AND save_locks.fencing_token = excluded.fencing_token
                """,
                (
                    record["save_id"], record["user_id"], record["owner_id"],
                    record.get("game_run_id"), record["auth_session_id"],
                    record["fencing_token"], record["lease_expires_at_ms"],
                    record["save_id"], record["user_id"],
                ),
            )
        return cursor.rowcount == 1

    def renew_save_lock(
        self,
        save_id: str,
        user_id: str,
        owner_id: str,
        auth_session_id: str,
        fencing_token: int,
        lease_expires_at_ms: int,
        now_ms: int,
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE save_locks SET lease_expires_at_ms = ?
                WHERE save_id = ? AND user_id = ? AND owner_id = ?
                  AND auth_session_id = ? AND fencing_token = ?
                  AND lease_expires_at_ms > ?
                """,
                (
                    lease_expires_at_ms, save_id, user_id, owner_id,
                    auth_session_id, fencing_token, now_ms,
                ),
            )
        return cursor.rowcount == 1

    def release_save_lock(
        self, save_id: str, user_id: str, owner_id: str, fencing_token: int
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            return connection.execute(
                """
                DELETE FROM save_locks
                WHERE save_id = ? AND user_id = ? AND owner_id = ? AND fencing_token = ?
                """,
                (save_id, user_id, owner_id, fencing_token),
            ).rowcount == 1

    def prepare_save_upload(
        self, record: dict[str, Any], now_ms: int
    ) -> tuple[dict[str, Any], bool]:
        self._validate_relative_path(str(record["candidate_relative_path"]))
        self._validate_relative_path(str(record["backup_relative_path"]))
        with self.database.transaction(write=True) as connection:
            previous = connection.execute(
                "SELECT * FROM save_upload_requests WHERE request_id = ?",
                (record["request_id"],),
            ).fetchone()
            if previous is not None:
                return dict(previous), False
            save = connection.execute(
                """
                SELECT revision, sha256 FROM save_records
                WHERE save_id = ? AND user_id = ?
                """,
                (record["save_id"], record["user_id"]),
            ).fetchone()
            if save is None:
                raise RepositoryConflictError("SAV is not owned by this user")
            if (
                int(save["revision"]) != int(record["expected_revision"])
                or save["sha256"] != record["base_sha256"]
            ):
                raise RepositoryConflictError("SAV revision changed")
            active_lock = connection.execute(
                """
                SELECT * FROM save_locks
                WHERE save_id = ? AND lease_expires_at_ms > ?
                """,
                (record["save_id"], now_ms),
            ).fetchone()
            if record.get("lock_owner"):
                if active_lock is None or not self._same_upload_lock(active_lock, record):
                    raise RepositoryConflictError("SAV upload authority is stale")
            elif active_lock is not None:
                raise RepositoryConflictError("SAV is locked")
            connection.execute(
                """
                INSERT INTO save_upload_requests (
                    request_id, save_id, user_id, expected_revision,
                    candidate_sha256, candidate_size, state, game_run_id,
                    fencing_token, created_at_ms, updated_at_ms, base_sha256,
                    authority_mode, mobile_session_id, lock_owner,
                    auth_session_id, game_session_id, candidate_relative_path,
                    backup_relative_path, committed_at_ms
                ) VALUES (?, ?, ?, ?, ?, ?, 'PREPARED', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)
                """,
                tuple(record.get(key) for key in (
                    "request_id", "save_id", "user_id", "expected_revision",
                    "candidate_sha256", "candidate_size", "game_run_id",
                    "fencing_token", "created_at_ms", "updated_at_ms", "base_sha256",
                    "authority_mode", "mobile_session_id", "lock_owner",
                    "auth_session_id", "game_session_id", "candidate_relative_path",
                    "backup_relative_path",
                )),
            )
            inserted = connection.execute(
                "SELECT * FROM save_upload_requests WHERE request_id = ?",
                (record["request_id"],),
            ).fetchone()
        return dict(inserted), True

    def get_save_upload(self, request_id: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                "SELECT * FROM save_upload_requests WHERE request_id = ?",
                (request_id,),
            ).fetchone()
        return dict(row) if row is not None else None

    def commit_prepared_save_upload(
        self, request_id: str, updated_at_ms: int
    ) -> dict[str, Any] | None:
        with self.database.transaction(write=True) as connection:
            journal = connection.execute(
                "SELECT * FROM save_upload_requests WHERE request_id = ?",
                (request_id,),
            ).fetchone()
            if journal is None:
                return None
            if journal["state"] == "COMMITTED":
                save = connection.execute(
                    "SELECT * FROM save_records WHERE save_id = ?",
                    (journal["save_id"],),
                ).fetchone()
                return dict(save) if save is not None else None
            if journal["state"] != "PREPARED":
                raise RepositoryConflictError("SAV upload journal is not prepared")
            already_committed = connection.execute(
                """
                SELECT * FROM save_records
                WHERE save_id = ? AND user_id = ? AND revision = ?
                  AND sha256 = ? AND size_bytes = ?
                """,
                (
                    journal["save_id"], journal["user_id"],
                    int(journal["expected_revision"]) + 1,
                    journal["candidate_sha256"], journal["candidate_size"],
                ),
            ).fetchone()
            if already_committed is not None:
                connection.execute(
                    """
                    UPDATE save_upload_requests
                    SET state = 'COMMITTED', updated_at_ms = ?, committed_at_ms = ?
                    WHERE request_id = ? AND state = 'PREPARED'
                    """,
                    (updated_at_ms, updated_at_ms, request_id),
                )
                return dict(already_committed)
            cursor = connection.execute(
                """
                UPDATE save_records
                SET size_bytes = ?, sha256 = ?, revision = revision + 1,
                    updated_at_ms = ?
                WHERE save_id = ? AND user_id = ? AND revision = ? AND sha256 = ?
                """,
                (
                    journal["candidate_size"], journal["candidate_sha256"],
                    updated_at_ms, journal["save_id"], journal["user_id"],
                    journal["expected_revision"], journal["base_sha256"],
                ),
            )
            if cursor.rowcount != 1:
                raise RepositoryConflictError("SAV metadata no longer matches prepared upload")
            connection.execute(
                """
                UPDATE save_upload_requests
                SET state = 'COMMITTED', updated_at_ms = ?, committed_at_ms = ?
                WHERE request_id = ? AND state = 'PREPARED'
                """,
                (updated_at_ms, updated_at_ms, request_id),
            )
            save = connection.execute(
                "SELECT * FROM save_records WHERE save_id = ?",
                (journal["save_id"],),
            ).fetchone()
        return dict(save)

    def list_prepared_save_uploads(self, limit: int = 100, *, save_ids=None) -> list[dict[str, Any]]:
        bounded_limit = max(1, min(int(limit), 1000))
        parameters = []
        selection = ""
        if save_ids is not None:
            if not save_ids:
                return []
            parameters.extend(sorted(save_ids))
            selection = " AND save_id IN (" + ",".join("?" for _ in parameters) + ")"
        parameters.append(bounded_limit)
        with self.database.transaction() as connection:
            rows = connection.execute(
                f"""
                SELECT * FROM save_upload_requests
                WHERE state = 'PREPARED' {selection}
                ORDER BY updated_at_ms, request_id
                LIMIT ?
                """,
                parameters,
            ).fetchall()
        return [dict(row) for row in rows]

    @staticmethod
    def _same_upload_lock(lock: sqlite3.Row, record: dict[str, Any]) -> bool:
        return (
            lock["user_id"] == record["user_id"]
            and lock["owner_id"] == record["lock_owner"]
            and lock["game_run_id"] == record.get("game_run_id")
            and lock["auth_session_id"] == record.get("auth_session_id")
            and int(lock["fencing_token"]) == int(record.get("fencing_token") or 0)
        )

    @staticmethod
    def _validate_relative_path(value: str) -> None:
        if not value or value.startswith("/") or ".." in value.split("/"):
            raise ValueError("path escapes storage root")

class SQLiteGameSessionRepository:
    def __init__(self, database: AuthorityDatabase):
        self.database = database

    @staticmethod
    def _require_current_bindings(connection, game_run, lock):
        # Serialize launch against ROM replacement's metadata transaction.
        for binding in lock.get("save_binding") or []:
            if not connection.execute(
                "SELECT 1 FROM save_records WHERE save_id=? AND user_id=? AND server_id=? "
                "AND revision=? AND sha256=?",
                (binding.get("save_id"), lock["user_id"], game_run["server_id"],
                 binding.get("revision"), binding.get("sha256")),
            ).fetchone():
                raise RepositoryConflictError("SAV binding changed; reload ROM slots")

    def acquire_pair(
        self,
        game_run: dict[str, Any],
        first_lock: dict[str, Any],
        second_lock: dict[str, Any],
        *,
        now_ms: int,
    ) -> tuple[int, int]:
        if first_lock["user_id"] == second_lock["user_id"]:
            raise RepositoryConflictError("game pair requires two users")
        try:
            with self.database.transaction(write=True) as connection:
                self._require_current_bindings(connection, game_run, first_lock)
                self._require_current_bindings(connection, game_run, second_lock)
                connection.execute(
                    "DELETE FROM game_session_locks WHERE lease_expires_at_ms <= ?",
                    (now_ms,),
                )
                connection.execute(
                    """
                    INSERT INTO game_runs (
                        game_run_id, server_id, execution_mode, status,
                        auth_session_id, created_at_ms, expires_at_ms,
                        termination_reason, row_version
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1)
                    """,
                    (
                        game_run["game_run_id"], game_run["server_id"],
                        game_run["execution_mode"], game_run["status"],
                        game_run["auth_session_id"], game_run["created_at_ms"],
                        game_run["expires_at_ms"],
                        game_run.get("termination_reason"),
                    ),
                )
                tokens = []
                for lock in (first_lock, second_lock):
                    connection.execute(
                        """
                        INSERT INTO game_session_fence_counters (user_id, last_fencing_token)
                        VALUES (?, 1)
                        ON CONFLICT(user_id) DO UPDATE SET
                            last_fencing_token = last_fencing_token + 1
                        """,
                        (lock["user_id"],),
                    )
                    token = int(
                        connection.execute(
                            "SELECT last_fencing_token FROM game_session_fence_counters WHERE user_id = ?",
                            (lock["user_id"],),
                        ).fetchone()[0]
                    )
                    connection.execute(
                        """
                        INSERT INTO game_session_locks (
                            user_id, game_run_id, server_id, auth_session_id,
                            fencing_token, lease_expires_at_ms, save_binding_json,
                            game_session_id, last_heartbeat_at_ms
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (
                            lock["user_id"], game_run["game_run_id"],
                            game_run["server_id"], lock["auth_session_id"], token,
                            lock["lease_expires_at_ms"],
                            self._bounded_json(lock.get("save_binding"), 4096),
                            lock.get("game_session_id"),
                            lock.get("last_heartbeat_at_ms", now_ms),
                        ),
                    )
                    tokens.append(token)
                return tokens[0], tokens[1]
        except sqlite3.IntegrityError as error:
            raise RepositoryConflictError("game session lock conflict") from error

    def acquire_single(
        self,
        game_run: dict[str, Any],
        lock: dict[str, Any],
        *,
        now_ms: int,
    ) -> int:
        try:
            with self.database.transaction(write=True) as connection:
                self._require_current_bindings(connection, game_run, lock)
                connection.execute(
                    "DELETE FROM game_session_locks WHERE lease_expires_at_ms <= ?",
                    (now_ms,),
                )
                existing_run = connection.execute(
                    "SELECT * FROM game_runs WHERE game_run_id = ?",
                    (game_run["game_run_id"],),
                ).fetchone()
                if existing_run is None:
                    connection.execute(
                        """
                        INSERT INTO game_runs (
                            game_run_id, server_id, execution_mode, status,
                            auth_session_id, created_at_ms, expires_at_ms,
                            termination_reason, row_version
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1)
                        """,
                        (
                            game_run["game_run_id"], game_run["server_id"],
                            game_run["execution_mode"], game_run["status"],
                            game_run["auth_session_id"], game_run["created_at_ms"],
                            game_run["expires_at_ms"],
                            game_run.get("termination_reason"),
                        ),
                    )
                elif not (
                    game_run.get("reuse_existing_run")
                    and existing_run["server_id"] == game_run["server_id"]
                    and existing_run["execution_mode"] == game_run["execution_mode"]
                    and existing_run["status"] == "RUNNING"
                ):
                    raise RepositoryConflictError("game run already exists")
                connection.execute(
                    """
                    INSERT INTO game_session_fence_counters (user_id, last_fencing_token)
                    VALUES (?, 1)
                    ON CONFLICT(user_id) DO UPDATE SET
                        last_fencing_token = last_fencing_token + 1
                    """,
                    (lock["user_id"],),
                )
                token = int(
                    connection.execute(
                        "SELECT last_fencing_token FROM game_session_fence_counters WHERE user_id = ?",
                        (lock["user_id"],),
                    ).fetchone()[0]
                )
                connection.execute(
                    """
                    INSERT INTO game_session_locks (
                        user_id, game_run_id, server_id, auth_session_id,
                        fencing_token, lease_expires_at_ms, save_binding_json,
                        game_session_id, last_heartbeat_at_ms
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        lock["user_id"], game_run["game_run_id"],
                        game_run["server_id"], lock["auth_session_id"], token,
                        lock["lease_expires_at_ms"],
                        self._bounded_json(lock.get("save_binding"), 4096),
                        lock["game_session_id"],
                        lock.get("last_heartbeat_at_ms", now_ms),
                    ),
                )
                return token
        except sqlite3.IntegrityError as error:
            raise RepositoryConflictError("game session lock conflict") from error

    def get_active_lock(self, user_id: str, now_ms: int) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT lock.*, run.execution_mode, run.status,
                       run.created_at_ms, run.expires_at_ms,
                       run.termination_reason
                FROM game_session_locks AS lock
                JOIN game_runs AS run ON run.game_run_id = lock.game_run_id
                WHERE lock.user_id = ? AND lock.lease_expires_at_ms > ?
                  AND run.status IN ('RUNNING', 'FINALIZING', 'RECOVERING')
                  AND run.expires_at_ms > ?
                """,
                (user_id, now_ms, now_ms),
            ).fetchone()
        return self._decode_game_lock(row) if row is not None else None

    def get_lock(self, user_id: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT lock.*, run.execution_mode, run.status,
                       run.created_at_ms, run.expires_at_ms,
                       run.termination_reason
                FROM game_session_locks AS lock
                JOIN game_runs AS run ON run.game_run_id = lock.game_run_id
                WHERE lock.user_id = ?
                """,
                (user_id,),
            ).fetchone()
        return self._decode_game_lock(row) if row is not None else None

    def renew_lock(
        self,
        user_id: str,
        game_run_id: str,
        auth_session_id: str,
        fencing_token: int,
        lease_expires_at_ms: int,
        now_ms: int = 0,
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE game_session_locks
                SET lease_expires_at_ms = ?, last_heartbeat_at_ms = ?
                WHERE user_id = ? AND game_run_id = ? AND auth_session_id = ?
                    AND fencing_token = ? AND lease_expires_at_ms > ?
                """,
                (
                    lease_expires_at_ms, now_ms, user_id, game_run_id,
                    auth_session_id, fencing_token, now_ms,
                ),
            )
            return cursor.rowcount == 1

    def release_lock(
        self,
        user_id: str,
        game_session_id: str,
        auth_session_id: str,
        fencing_token: int,
        *,
        status: str = "COMPLETED",
        reason: str | None = None,
    ) -> dict[str, Any] | None:
        with self.database.transaction(write=True) as connection:
            row = connection.execute(
                """
                SELECT * FROM game_session_locks
                WHERE user_id = ? AND game_session_id = ?
                  AND auth_session_id = ? AND fencing_token = ?
                """,
                (user_id, game_session_id, auth_session_id, fencing_token),
            ).fetchone()
            if row is None:
                return None
            connection.execute(
                "DELETE FROM game_session_locks WHERE user_id = ?",
                (user_id,),
            )
            remaining = int(
                connection.execute(
                    "SELECT COUNT(*) FROM game_session_locks WHERE game_run_id = ?",
                    (row["game_run_id"],),
                ).fetchone()[0]
            )
            if remaining == 0:
                connection.execute(
                    """
                    UPDATE game_runs
                    SET status = ?, termination_reason = ?, row_version = row_version + 1
                    WHERE game_run_id = ?
                    """,
                    (status, reason, row["game_run_id"]),
                )
        return dict(row)

    def active_locks_for_save(self, save_id: str, now_ms: int) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT lock.*, run.execution_mode, run.status,
                       run.created_at_ms, run.expires_at_ms,
                       run.termination_reason
                FROM game_session_locks AS lock
                JOIN game_runs AS run ON run.game_run_id = lock.game_run_id
                WHERE lock.lease_expires_at_ms > ? AND run.expires_at_ms > ?
                  AND run.status IN ('RUNNING', 'FINALIZING', 'RECOVERING')
                """,
                (now_ms, now_ms),
            ).fetchall()
        matches = []
        for row in rows:
            decoded = self._decode_game_lock(row)
            if any(
                binding.get("save_id") == save_id
                for binding in decoded.get("save_binding", [])
            ):
                matches.append(decoded)
        return matches

    def active_locks_for_run(
        self, game_run_id: str, now_ms: int
    ) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT lock.*, run.execution_mode, run.status,
                       run.created_at_ms, run.expires_at_ms,
                       run.termination_reason
                FROM game_session_locks AS lock
                JOIN game_runs AS run ON run.game_run_id = lock.game_run_id
                WHERE lock.game_run_id = ? AND lock.lease_expires_at_ms > ?
                  AND run.expires_at_ms > ?
                  AND run.status IN ('RUNNING', 'FINALIZING', 'RECOVERING')
                ORDER BY lock.user_id
                """,
                (game_run_id, now_ms, now_ms),
            ).fetchall()
        return [self._decode_game_lock(row) for row in rows]

    def update_run_deadline(
        self,
        game_run_id: str,
        *,
        expires_at_ms: int,
        status: str = "RUNNING",
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE game_runs
                SET expires_at_ms = ?, status = ?, row_version = row_version + 1
                WHERE game_run_id = ?
                  AND status IN ('RUNNING', 'FINALIZING', 'RECOVERING')
                """,
                (
                    expires_at_ms, status, game_run_id,
                ),
            )
        return cursor.rowcount == 1

    def extend_run_locks(self, game_run_id: str, lease_expires_at_ms: int) -> int:
        with self.database.transaction(write=True) as connection:
            return connection.execute(
                """
                UPDATE game_session_locks SET lease_expires_at_ms = ?
                WHERE game_run_id = ?
                """,
                (lease_expires_at_ms, game_run_id),
            ).rowcount

    def prune_expired_locks(self, now_ms: int) -> list[dict[str, Any]]:
        with self.database.transaction(write=True) as connection:
            rows = connection.execute(
                """
                SELECT lock.*, run.execution_mode, run.status,
                       run.created_at_ms, run.expires_at_ms,
                       run.termination_reason
                FROM game_session_locks AS lock
                JOIN game_runs AS run ON run.game_run_id = lock.game_run_id
                WHERE lock.lease_expires_at_ms <= ? OR run.expires_at_ms <= ?
                """,
                (now_ms, now_ms),
            ).fetchall()
            run_ids = {str(row["game_run_id"]) for row in rows}
            if rows:
                connection.executemany(
                    "DELETE FROM game_session_locks WHERE user_id = ?",
                    [(row["user_id"],) for row in rows],
                )
            for run_id in run_ids:
                remaining = connection.execute(
                    "SELECT 1 FROM game_session_locks WHERE game_run_id = ? LIMIT 1",
                    (run_id,),
                ).fetchone()
                if remaining is None:
                    connection.execute(
                        """
                        UPDATE game_runs SET status = 'EXPIRED',
                            termination_reason = 'participant_lease_expired',
                            row_version = row_version + 1
                        WHERE game_run_id = ? AND status IN ('RUNNING', 'FINALIZING', 'RECOVERING')
                        """,
                        (run_id,),
                    )
        return [self._decode_game_lock(row) for row in rows]

    def release_run(self, game_run_id: str, *, status: str, reason: str | None = None) -> int:
        with self.database.transaction(write=True) as connection:
            removed = connection.execute(
                "DELETE FROM game_session_locks WHERE game_run_id = ?",
                (game_run_id,),
            ).rowcount
            connection.execute(
                """
                UPDATE game_runs SET status = ?, termination_reason = ?, row_version = row_version + 1
                WHERE game_run_id = ?
                """,
                (status, reason, game_run_id),
            )
            return removed

    @staticmethod
    def _decode_game_lock(row: sqlite3.Row) -> dict[str, Any]:
        result = dict(row)
        encoded = result.pop("save_binding_json", None)
        result["save_binding"] = json.loads(encoded) if encoded else []
        return result

    def record_event(
        self,
        server_id: str,
        session_id: str,
        event_type: str,
        payload: dict[str, Any] | None,
        now_ms: int,
        *,
        coalesce_key: str | None = None,
    ) -> None:
        encoded = self._bounded_json(payload, 8192)
        with self.database.transaction(write=True) as connection:
            if coalesce_key is None:
                connection.execute(
                    """
                    INSERT INTO session_events (
                        server_id, session_id, event_type, payload_json,
                        coalesce_key, first_created_at_ms, last_created_at_ms, occurrences
                    ) VALUES (?, ?, ?, ?, NULL, ?, ?, 1)
                    """,
                    (server_id, session_id, event_type, encoded, now_ms, now_ms),
                )
            else:
                connection.execute(
                    """
                    INSERT INTO session_events (
                        server_id, session_id, event_type, payload_json,
                        coalesce_key, first_created_at_ms, last_created_at_ms, occurrences
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, 1)
                    ON CONFLICT(coalesce_key) WHERE coalesce_key IS NOT NULL DO UPDATE SET
                        payload_json = excluded.payload_json,
                        last_created_at_ms = excluded.last_created_at_ms,
                        occurrences = session_events.occurrences + 1
                    """,
                    (server_id, session_id, event_type, encoded, coalesce_key, now_ms, now_ms),
                )

    @staticmethod
    def _bounded_json(value: Any, maximum: int) -> str | None:
        if value is None:
            return None
        encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
        if len(encoded.encode("utf-8")) > maximum:
            raise ValueError("bounded metadata is too large")
        return encoded


class SQLiteSessionAuthorityRepository:
    """Transactional state and recovery operations shared by runtime sessions."""

    TABLES = {
        "link": ("link_sessions", "status"),
        "gb_runtime_fixed_host": ("gb_runtime_fixed_host_sessions", "state"),
        "n64": ("n64_runtime_media_sessions", "status"),
        "mobile": ("mobile_sessions", "status"),
    }

    def __init__(self, database: AuthorityDatabase):
        self.database = database

    def insert(self, kind: str, record: dict[str, Any]) -> None:
        table, _status_column = self._table(kind)
        if not record:
            raise ValueError("session record must not be empty")
        columns = tuple(record)
        allowed = self._columns(table)
        if any(column not in allowed for column in columns):
            raise ValueError("unknown session column")
        values = [self._encode_value(column, record[column]) for column in columns]
        placeholders = ", ".join("?" for _ in columns)
        names = ", ".join(columns)
        try:
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    f"INSERT INTO {table} ({names}) VALUES ({placeholders})",
                    values,
                )
        except sqlite3.IntegrityError as error:
            raise RepositoryConflictError("session authority conflict") from error

    def get(self, kind: str, session_id: str) -> dict[str, Any] | None:
        table, _status_column = self._table(kind)
        with self.database.transaction() as connection:
            row = connection.execute(
                f"SELECT * FROM {table} WHERE session_id = ?", (session_id,)
            ).fetchone()
        return self._decode_row(row) if row is not None else None

    def create_mobile(self, record: dict[str, Any]) -> None:
        metadata = {
            "rom_header_title": record.get("rom_header_title", ""),
            "scenario_display_name": record.get("scenario_display_name", "DEFAULT"),
        }
        self.insert(
            "mobile",
            {
                "session_id": record["session_id"],
                "server_id": record["server_id"],
                "user_id": record["user_id"],
                "save_id": record["save_id"],
                "rom_id": record["rom_id"],
                "auth_session_id": record["auth_session_id"],
                "package_id": record["package_id"],
                "release_id": record["release_id"],
                "package_digest": record["package_digest"],
                "game_run_id": record["game_run_id"],
                "game_session_id": record.get("game_session_id"),
                "fencing_token": record["fencing_token"],
                "scenario_id": record.get("scenario_id", "default"),
                "status": record["status"],
                "created_at_ms": record["created_at_ms"],
                "updated_at_ms": record["updated_at_ms"],
                "lease_expires_at_ms": record["lease_expires_at_ms"],
                "metadata_json": metadata,
            },
        )

    def prepare_mobile_create(
        self, request: dict[str, Any]
    ) -> tuple[dict[str, Any], bool]:
        normalized = dict(request)
        normalized.setdefault(
            "reserved_session_id", f"mobile_{request['request_digest'][:48]}"
        )
        columns = (
            "request_digest", "user_id", "request_id", "save_id", "rom_id",
            "rom_header_title", "package_id", "release_id", "package_digest",
            "scenario_id", "reserved_session_id",
        )
        with self.database.transaction(write=True) as connection:
            previous = connection.execute(
                "SELECT * FROM mobile_create_requests WHERE request_digest = ?",
                (normalized["request_digest"],),
            ).fetchone()
            if previous is not None:
                record = dict(previous)
                if any(record[column] != normalized[column] for column in columns):
                    raise RepositoryConflictError(
                        "Mobile create request was reused for different inputs"
                    )
                return record, False
            connection.execute(
                """
                INSERT INTO mobile_create_requests (
                    request_digest, user_id, request_id, save_id, rom_id,
                    rom_header_title, package_id, release_id, package_digest,
                    scenario_id, reserved_session_id, auth_session_id,
                    mobile_session_id, state, created_at_ms,
                    updated_at_ms, failure_reason
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, 'PREPARED', ?, ?, NULL)
                """,
                tuple(normalized[column] for column in columns)
                + (normalized["auth_session_id"],)
                + (normalized["created_at_ms"], normalized["created_at_ms"]),
            )
            row = connection.execute(
                "SELECT * FROM mobile_create_requests WHERE request_digest = ?",
                (normalized["request_digest"],),
            ).fetchone()
        return dict(row), True

    def commit_mobile_create(
        self, request_digest: str, record: dict[str, Any]
    ) -> dict[str, Any]:
        metadata = self._bounded_json(
            {
                "rom_header_title": record.get("rom_header_title", ""),
                "scenario_display_name": record.get("scenario_display_name", "DEFAULT"),
            },
            65536,
        )
        with self.database.transaction(write=True) as connection:
            request = connection.execute(
                "SELECT * FROM mobile_create_requests WHERE request_digest = ?",
                (request_digest,),
            ).fetchone()
            if request is None:
                raise RepositoryConflictError("Mobile create request is missing")
            if request["state"] == "CREATED":
                existing = connection.execute(
                    "SELECT * FROM mobile_sessions WHERE session_id = ?",
                    (request["mobile_session_id"],),
                ).fetchone()
                if existing is None:
                    raise RepositoryConflictError("Mobile create journal is inconsistent")
                return self._decode_row(existing)
            if request["state"] != "PREPARED":
                raise RepositoryConflictError("Mobile create request is not prepared")
            connection.execute(
                """
                INSERT INTO mobile_sessions (
                    session_id, server_id, user_id, save_id, rom_id,
                    auth_session_id, package_id, release_id, package_digest,
                    game_run_id, game_session_id, fencing_token,
                    scenario_id, status,
                    created_at_ms, updated_at_ms, lease_expires_at_ms,
                    metadata_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    record["session_id"], record["server_id"], record["user_id"],
                    record["save_id"], record["rom_id"], record["auth_session_id"],
                    record["package_id"], record["release_id"], record["package_digest"],
                    record["game_run_id"],
                    record.get("game_session_id"), record["fencing_token"],
                    record.get("scenario_id", "default"),
                    record["status"], record["created_at_ms"], record["updated_at_ms"],
                    record["lease_expires_at_ms"], metadata,
                ),
            )
            connection.execute(
                """
                UPDATE mobile_create_requests
                SET mobile_session_id = ?, state = 'CREATED', updated_at_ms = ?
                WHERE request_digest = ? AND state = 'PREPARED'
                """,
                (record["session_id"], record["updated_at_ms"], request_digest),
            )
            row = connection.execute(
                "SELECT * FROM mobile_sessions WHERE session_id = ?",
                (record["session_id"],),
            ).fetchone()
        return self._decode_row(row)

    def list_mobile(self, server_id: str) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                "SELECT * FROM mobile_sessions WHERE server_id = ? ORDER BY created_at_ms",
                (server_id,),
            ).fetchall()
        return [self._decode_row(row) for row in rows]

    def find_mobile(self, session_id: str, user_id: str) -> dict[str, Any] | None:
        with self.database.transaction() as connection:
            row = connection.execute(
                """
                SELECT * FROM mobile_sessions
                WHERE session_id = ? AND user_id = ?
                """,
                (session_id, user_id),
            ).fetchone()
        return self._decode_row(row) if row is not None else None

    def find_running_mobile_by_save(
        self, server_id: str, user_id: str, save_id: str
    ) -> list[dict[str, Any]]:
        with self.database.transaction() as connection:
            rows = connection.execute(
                """
                SELECT * FROM mobile_sessions
                WHERE server_id = ? AND user_id = ? AND save_id = ?
                    AND status = 'RUNNING'
                """,
                (server_id, user_id, save_id),
            ).fetchall()
        return [self._decode_row(row) for row in rows]

    def renew_mobile(
        self,
        session_id: str,
        *,
        user_id: str,
        auth_session_id: str,
        game_run_id: str,
        fencing_token: int,
        expected_version: int,
        lease_expires_at_ms: int,
        updated_at_ms: int,
    ) -> bool:
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE mobile_sessions
                SET lease_expires_at_ms = ?, updated_at_ms = ?,
                    row_version = row_version + 1
                WHERE session_id = ? AND user_id = ? AND auth_session_id = ?
                    AND game_run_id = ? AND fencing_token = ?
                    AND row_version = ? AND status = 'RUNNING'
                """,
                (
                    lease_expires_at_ms, updated_at_ms, session_id, user_id,
                    auth_session_id, game_run_id, fencing_token, expected_version,
                ),
            )
            return cursor.rowcount == 1

    def complete_mobile(
        self,
        session_id: str,
        *,
        user_id: str,
        auth_session_id: str,
        game_run_id: str,
        fencing_token: int,
        expected_version: int,
        status: str,
        updated_at_ms: int,
        failure_reason: str | None = None,
    ) -> bool:
        if status not in {"COMPLETED", "CANCELLED", "EXPIRED", "FAILED"}:
            raise ValueError("invalid Mobile terminal state")
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                """
                UPDATE mobile_sessions
                SET status = ?, updated_at_ms = ?, failure_reason = ?,
                    row_version = row_version + 1
                WHERE session_id = ? AND user_id = ? AND auth_session_id = ?
                    AND game_run_id = ? AND fencing_token = ?
                    AND row_version = ? AND status = 'RUNNING'
                """,
                (
                    status, updated_at_ms, failure_reason,
                    session_id, user_id, auth_session_id, game_run_id,
                    fencing_token, expected_version,
                ),
            )
            return cursor.rowcount == 1

    def transition(
        self,
        kind: str,
        session_id: str,
        *,
        from_states: set[str],
        to_state: str,
        expected_version: int,
        updated_at_ms: int,
        due_at_ms: int | None = None,
        termination_reason: str | None = None,
    ) -> bool:
        table, status_column = self._table(kind)
        if not from_states:
            return False
        due_column = {
            "gb_runtime_fixed_host": "due_at_ms",
            "n64": "expires_at_ms",
        }.get(kind, "lease_expires_at_ms")
        placeholders = ", ".join("?" for _ in from_states)
        arguments: list[Any] = [
            to_state, updated_at_ms, due_at_ms, termination_reason,
            session_id, expected_version, *sorted(from_states),
        ]
        with self.database.transaction(write=True) as connection:
            cursor = connection.execute(
                f"""
                UPDATE {table}
                SET {status_column} = ?, updated_at_ms = ?, {due_column} = ?,
                    termination_reason = ?, row_version = row_version + 1
                WHERE session_id = ? AND row_version = ?
                    AND {status_column} IN ({placeholders})
                """,
                arguments,
            )
            return cursor.rowcount == 1

    def list_due(
        self,
        kind: str,
        *,
        server_id: str,
        active_states: set[str],
        now_ms: int,
        include_missing_game_locks: bool = False,
        limit: int = 32,
    ) -> list[dict[str, Any]]:
        table, status_column = self._table(kind)
        if not active_states or not 1 <= limit <= 256:
            return []
        if include_missing_game_locks and kind not in {"link", "n64"}:
            raise ValueError("missing-lock query is supported only for Link and N64")
        due_column = {
            "gb_runtime_fixed_host": "due_at_ms",
            "n64": "expires_at_ms",
        }.get(kind, "lease_expires_at_ms")
        placeholders = ", ".join("?" for _ in active_states)
        states = sorted(active_states)
        if not include_missing_game_locks:
            predicate = f"{due_column} IS NOT NULL AND {due_column} <= ?"
            arguments = (server_id, *states, now_ms, limit)
        else:
            expected = (
                "CASE WHEN player_a_user_id = player_b_user_id THEN 1 ELSE 2 END"
                if kind == "link" else
                "CASE WHEN host_user_id = remote_user_id THEN 1 ELSE 2 END"
            )
            predicate = f"""
                (({due_column} IS NOT NULL AND {due_column} <= ?)
                 OR (SELECT COUNT(*) FROM game_session_locks AS lock
                     WHERE lock.game_run_id = session.session_id
                       AND lock.lease_expires_at_ms > ?) < {expected})
            """
            arguments = (server_id, *states, now_ms, now_ms, limit)
        with self.database.transaction() as connection:
            rows = connection.execute(
                f"""
                SELECT session.* FROM {table} AS session
                WHERE session.server_id = ? AND {status_column} IN ({placeholders})
                  AND {predicate}
                ORDER BY COALESCE({due_column}, 9223372036854775807), session_id
                LIMIT ?
                """,
                arguments,
            ).fetchall()
        return [self._decode_row(row) for row in rows]

    def _table(self, kind: str) -> tuple[str, str]:
        try:
            return self.TABLES[kind]
        except KeyError as error:
            raise ValueError("unknown session kind") from error

    def _columns(self, table: str) -> set[str]:
        with self.database.transaction() as connection:
            return {str(row[1]) for row in connection.execute(f"PRAGMA table_info({table})")}

    @classmethod
    def _encode_value(cls, column: str, value: Any) -> Any:
        if column.endswith("_json"):
            maximum = 32768 if column == "detail_json" else 65536
            return cls._bounded_json(value, maximum)
        return value

    @staticmethod
    def _decode_row(row: sqlite3.Row) -> dict[str, Any]:
        result = dict(row)
        for key, value in tuple(result.items()):
            if key.endswith("_json") and value is not None:
                result[key] = json.loads(value)
        return result

    @staticmethod
    def _bounded_json(value: Any, maximum: int) -> str | None:
        if value is None:
            return None
        encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
        if len(encoded.encode("utf-8")) > maximum:
            raise ValueError("bounded metadata is too large")
        return encoded
