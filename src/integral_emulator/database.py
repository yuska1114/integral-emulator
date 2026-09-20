# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SQLite authority database with one pre-release baseline schema."""

from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
import os
from pathlib import Path
import sqlite3
import time
from typing import Iterator


DATABASE_APPLICATION_ID = 0x49454D55  # "IEMU"
SCHEMA_BASELINE_VERSION = 2


class DatabaseBaselineError(RuntimeError):
    """The database is not empty and does not use the current baseline."""


BASELINE_SCHEMA_SQL = r"""
CREATE TABLE auth_sessions (
            token_digest TEXT PRIMARY KEY,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            server_id TEXT NOT NULL,
            created_at_ms INTEGER NOT NULL,
            expires_at_ms INTEGER NOT NULL,
            last_access_at_ms INTEGER NOT NULL
        , revoked_at_ms INTEGER);

CREATE TABLE gb_runtime_fixed_host_sessions (
            session_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            room_number INTEGER NOT NULL,
            state TEXT NOT NULL,
            manifest_digest TEXT NOT NULL CHECK (length(manifest_digest) = 64),
            manifest_json TEXT NOT NULL CHECK (length(manifest_json) <= 65536),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            due_at_ms INTEGER,
            termination_reason TEXT,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            control_json TEXT CHECK (control_json IS NULL OR length(control_json) <= 65536)
        );

CREATE TABLE game_runs (
            game_run_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            execution_mode TEXT NOT NULL,
            status TEXT NOT NULL,
            auth_session_id TEXT NOT NULL REFERENCES auth_sessions(token_digest),
            created_at_ms INTEGER NOT NULL,
            expires_at_ms INTEGER NOT NULL,
            termination_reason TEXT,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1)
        );

CREATE TABLE game_session_fence_counters (
            user_id TEXT PRIMARY KEY REFERENCES users(user_id) ON DELETE CASCADE,
            last_fencing_token INTEGER NOT NULL CHECK (last_fencing_token >= 0)
        );

CREATE TABLE game_session_locks (
            user_id TEXT PRIMARY KEY REFERENCES users(user_id) ON DELETE CASCADE,
            game_run_id TEXT NOT NULL REFERENCES game_runs(game_run_id) ON DELETE CASCADE,
            server_id TEXT NOT NULL,
            auth_session_id TEXT NOT NULL REFERENCES auth_sessions(token_digest),
            fencing_token INTEGER NOT NULL,
            lease_expires_at_ms INTEGER NOT NULL,
            save_binding_json TEXT, game_session_id TEXT, last_heartbeat_at_ms INTEGER,
            UNIQUE (user_id, fencing_token)
        );

CREATE TABLE host_processes (
            process_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            session_id TEXT NOT NULL,
            status TEXT NOT NULL,
            pid INTEGER,
            exit_code INTEGER,
            dry_run INTEGER NOT NULL CHECK (dry_run IN (0, 1)),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            metadata_json TEXT CHECK (
                metadata_json IS NULL OR length(metadata_json) <= 16384
            )
        );

CREATE TABLE link_sessions (
            session_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            room_number INTEGER,
            player_a_user_id TEXT NOT NULL REFERENCES users(user_id),
            player_b_user_id TEXT NOT NULL REFERENCES users(user_id),
            save_a_id TEXT REFERENCES save_records(save_id),
            save_b_id TEXT REFERENCES save_records(save_id),
            status TEXT NOT NULL,
            requested_link_mode TEXT NOT NULL,
            protocol_id TEXT NOT NULL,
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            lease_expires_at_ms INTEGER,
            expires_at_ms INTEGER,
            termination_reason TEXT,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            metadata_json TEXT CHECK (metadata_json IS NULL OR length(metadata_json) <= 65536)
        );

CREATE TABLE mobile_create_requests (
            request_digest TEXT PRIMARY KEY CHECK (length(request_digest) = 64),
            user_id TEXT NOT NULL REFERENCES users(user_id),
            request_id TEXT NOT NULL,
            save_id TEXT NOT NULL REFERENCES save_records(save_id),
            rom_id TEXT NOT NULL REFERENCES rom_registrations(rom_id),
            rom_header_title TEXT NOT NULL,
            package_id TEXT NOT NULL,
            release_id TEXT NOT NULL,
            package_digest TEXT NOT NULL CHECK (length(package_digest) = 64),
            scenario_id TEXT NOT NULL,
            reserved_session_id TEXT NOT NULL,
            mobile_session_id TEXT REFERENCES mobile_sessions(session_id),
            state TEXT NOT NULL CHECK (state IN ('PREPARED', 'CREATED', 'FAILED')),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            failure_reason TEXT
        , auth_session_id TEXT
            REFERENCES auth_sessions(token_digest));

CREATE TABLE mobile_sessions (
            session_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id),
            save_id TEXT REFERENCES save_records(save_id),
            rom_id TEXT REFERENCES rom_registrations(rom_id),
            auth_session_id TEXT NOT NULL REFERENCES auth_sessions(token_digest),
            package_id TEXT NOT NULL,
            release_id TEXT NOT NULL,
            package_digest TEXT NOT NULL CHECK (length(package_digest) = 64),
            game_run_id TEXT NOT NULL REFERENCES game_runs(game_run_id),
            fencing_token INTEGER NOT NULL,
            scenario_id TEXT NOT NULL,
            status TEXT NOT NULL,
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            lease_expires_at_ms INTEGER NOT NULL,
            hard_expires_at_ms INTEGER,
            failure_reason TEXT,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            metadata_json TEXT CHECK (metadata_json IS NULL OR length(metadata_json) <= 65536)
        , game_session_id TEXT);

CREATE TABLE n64_runtime_media_sessions (
            session_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            room_number INTEGER NOT NULL,
            host_user_id TEXT NOT NULL REFERENCES users(user_id),
            remote_user_id TEXT NOT NULL REFERENCES users(user_id),
            status TEXT NOT NULL,
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            expires_at_ms INTEGER,
            termination_reason TEXT,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            metadata_json TEXT CHECK (metadata_json IS NULL OR length(metadata_json) <= 65536)
        );

CREATE TABLE pair_save_commit_journals (
            session_id TEXT PRIMARY KEY REFERENCES link_sessions(session_id) ON DELETE CASCADE,
            state TEXT NOT NULL,
            player_a_save_id TEXT REFERENCES save_records(save_id),
            player_b_save_id TEXT REFERENCES save_records(save_id),
            player_a_expected_revision INTEGER,
            player_b_expected_revision INTEGER,
            player_a_candidate_sha256 TEXT CHECK (
                player_a_candidate_sha256 IS NULL OR length(player_a_candidate_sha256) = 64
            ),
            player_b_candidate_sha256 TEXT CHECK (
                player_b_candidate_sha256 IS NULL OR length(player_b_candidate_sha256) = 64
            ),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            detail_json TEXT CHECK (detail_json IS NULL OR length(detail_json) <= 32768)
        );

CREATE TABLE rate_limit_buckets (
            scope_key TEXT PRIMARY KEY,
            window_started_at_ms INTEGER NOT NULL,
            attempt_count INTEGER NOT NULL CHECK (attempt_count >= 0),
            expires_at_ms INTEGER NOT NULL
        );

CREATE TABLE rom_registrations (
            rom_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            sha256 TEXT NOT NULL CHECK (length(sha256) = 64),
            sha1 TEXT CHECK (sha1 IS NULL OR length(sha1) = 40),
            game_type TEXT NOT NULL,
            title TEXT NOT NULL,
            region TEXT NOT NULL,
            verified_name TEXT,
            rom_header_title TEXT,
            size_bytes INTEGER CHECK (size_bytes IS NULL OR size_bytes > 0),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            UNIQUE (server_id, user_id, sha256)
        );

CREATE TABLE rom_slots (
            server_id TEXT NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            slot INTEGER NOT NULL CHECK (slot BETWEEN 1 AND 8),
            rom_id TEXT REFERENCES rom_registrations(rom_id),
            save_id TEXT REFERENCES save_records(save_id),
            filename TEXT,
            updated_at_ms INTEGER NOT NULL,
            row_version INTEGER NOT NULL DEFAULT 1 CHECK (row_version >= 1),
            PRIMARY KEY (server_id, user_id, slot)
        );

CREATE TABLE room_chat (
            message_id INTEGER PRIMARY KEY AUTOINCREMENT,
            server_id TEXT NOT NULL,
            room_number INTEGER NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id),
            username_snapshot TEXT NOT NULL,
            message TEXT NOT NULL CHECK (length(message) BETWEEN 1 AND 512),
            created_at_ms INTEGER NOT NULL,
            FOREIGN KEY (server_id, room_number)
                REFERENCES rooms(server_id, room_number) ON DELETE CASCADE
        );

CREATE TABLE room_code_tombstones (
            server_id TEXT NOT NULL,
            room_code TEXT NOT NULL,
            invalidated_at_ms INTEGER NOT NULL,
            reuse_after_ms INTEGER NOT NULL,
            PRIMARY KEY (server_id, room_code)
        );

CREATE TABLE room_members (
            server_id TEXT NOT NULL,
            room_number INTEGER NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id),
            seat INTEGER NOT NULL CHECK (seat IN (1, 2)),
            username_snapshot TEXT NOT NULL,
            slot INTEGER,
            n64_slot INTEGER,
            ready INTEGER NOT NULL DEFAULT 0 CHECK (ready IN (0, 1)),
            joined_at_ms INTEGER NOT NULL,
            last_seen_at_ms INTEGER NOT NULL,
            last_activity_at_ms INTEGER NOT NULL,
            ready_at_ms INTEGER, auth_session_id TEXT
            REFERENCES auth_sessions(token_digest),
            PRIMARY KEY (server_id, room_number, user_id),
            UNIQUE (server_id, user_id),
            UNIQUE (server_id, room_number, seat),
            FOREIGN KEY (server_id, room_number)
                REFERENCES rooms(server_id, room_number) ON DELETE CASCADE
        );

CREATE TABLE room_termination_notices (
            server_id TEXT NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            room_number INTEGER NOT NULL,
            status TEXT NOT NULL,
            reason TEXT NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            expires_at_ms INTEGER NOT NULL,
            PRIMARY KEY (server_id, user_id)
        );

CREATE TABLE rooms (
            server_id TEXT NOT NULL,
            room_number INTEGER NOT NULL,
            room_type TEXT NOT NULL CHECK (room_type IN ('link_cable', 'n64')),
            room_code TEXT,
            creator_user_id TEXT NOT NULL REFERENCES users(user_id),
            link_mode TEXT,
            game_started INTEGER NOT NULL DEFAULT 0 CHECK (game_started IN (0, 1)),
            link_session_id TEXT,
            room_code_created_at_ms INTEGER,
            updated_at_ms INTEGER NOT NULL,
            post_game_at_ms INTEGER,
            PRIMARY KEY (server_id, room_number),
            UNIQUE (server_id, room_code),
            CHECK (
                (room_type = 'link_cable' AND room_number BETWEEN 1 AND 64) OR
                (room_type = 'n64' AND room_number BETWEEN 65 AND 128)
            )
        );

CREATE TABLE save_locks (
            save_id TEXT PRIMARY KEY REFERENCES save_records(save_id) ON DELETE CASCADE,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            owner_id TEXT NOT NULL,
            game_run_id TEXT REFERENCES game_runs(game_run_id) ON DELETE CASCADE,
            auth_session_id TEXT NOT NULL REFERENCES auth_sessions(token_digest),
            fencing_token INTEGER NOT NULL,
            lease_expires_at_ms INTEGER NOT NULL
        );

CREATE TABLE save_records (
            save_id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            rom_id TEXT REFERENCES rom_registrations(rom_id),
            game_type TEXT NOT NULL,
            relative_path TEXT NOT NULL,
            size_bytes INTEGER NOT NULL CHECK (size_bytes >= 0),
            sha256 TEXT NOT NULL CHECK (length(sha256) = 64),
            revision INTEGER NOT NULL CHECK (revision >= 0),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL,
            UNIQUE (server_id, user_id, relative_path),
            CHECK (relative_path NOT LIKE '/%' AND relative_path NOT LIKE '%..%')
        );

CREATE TABLE save_upload_requests (
            request_id TEXT PRIMARY KEY,
            save_id TEXT NOT NULL REFERENCES save_records(save_id) ON DELETE CASCADE,
            user_id TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
            expected_revision INTEGER NOT NULL,
            candidate_sha256 TEXT NOT NULL CHECK (length(candidate_sha256) = 64),
            candidate_size INTEGER NOT NULL CHECK (candidate_size >= 0),
            state TEXT NOT NULL,
            game_run_id TEXT,
            fencing_token INTEGER,
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL
        , base_sha256 TEXT
            CHECK (base_sha256 IS NULL OR length(base_sha256) = 64), authority_mode TEXT NOT NULL
            DEFAULT 'UNLOCKED', mobile_session_id TEXT, lock_owner TEXT, auth_session_id TEXT
            REFERENCES auth_sessions(token_digest), candidate_relative_path TEXT
            CHECK (candidate_relative_path IS NULL OR (
                candidate_relative_path NOT LIKE '/%'
                AND candidate_relative_path NOT LIKE '%..%'
            )), backup_relative_path TEXT
            CHECK (backup_relative_path IS NULL OR (
                backup_relative_path NOT LIKE '/%'
                AND backup_relative_path NOT LIKE '%..%'
            )), committed_at_ms INTEGER, game_session_id TEXT);

CREATE TABLE session_events (
            event_id INTEGER PRIMARY KEY AUTOINCREMENT,
            server_id TEXT NOT NULL,
            session_id TEXT NOT NULL,
            event_type TEXT NOT NULL,
            payload_json TEXT,
            coalesce_key TEXT,
            first_created_at_ms INTEGER NOT NULL,
            last_created_at_ms INTEGER NOT NULL,
            occurrences INTEGER NOT NULL DEFAULT 1 CHECK (occurrences >= 1)
        );

CREATE TABLE users (
            user_id TEXT PRIMARY KEY,
            username TEXT NOT NULL,
            username_normalized TEXT NOT NULL UNIQUE,
            email TEXT,
            password_hash TEXT NOT NULL,
            status TEXT NOT NULL,
            must_change_password INTEGER NOT NULL CHECK (must_change_password IN (0, 1)),
            created_at_ms INTEGER NOT NULL,
            updated_at_ms INTEGER NOT NULL
        );

CREATE INDEX auth_sessions_expiry_idx ON auth_sessions(expires_at_ms);

CREATE INDEX auth_sessions_live_idx
            ON auth_sessions(token_digest, expires_at_ms)
            WHERE revoked_at_ms IS NULL;

CREATE INDEX auth_sessions_user_idx ON auth_sessions(user_id);

CREATE UNIQUE INDEX gb_runtime_fixed_host_sessions_active_room_idx
            ON gb_runtime_fixed_host_sessions(server_id, room_number)
            WHERE state IN ('PREFLIGHT', 'READY', 'WAITING_PEER', 'RUNNING',
                'PAUSED_REMOTE', 'FINALIZING');

CREATE INDEX gb_runtime_fixed_host_sessions_due_idx
            ON gb_runtime_fixed_host_sessions(state, due_at_ms);

CREATE INDEX game_runs_due_idx ON game_runs(status, expires_at_ms);

CREATE UNIQUE INDEX game_session_locks_client_id_idx
            ON game_session_locks(game_session_id)
            WHERE game_session_id IS NOT NULL;

CREATE INDEX game_session_locks_expiry_idx
            ON game_session_locks(lease_expires_at_ms);

CREATE INDEX game_session_locks_run_idx
            ON game_session_locks(game_run_id);

CREATE INDEX host_processes_session_idx
            ON host_processes(server_id, session_id, status);

CREATE UNIQUE INDEX link_sessions_active_room_idx
            ON link_sessions(server_id, room_number)
            WHERE status IN ('CREATED', 'WAITING_PLAYER_A', 'WAITING_PLAYER_B',
                'PREPARING', 'RUNNING', 'FINALIZING', 'RECOVERING');

CREATE INDEX link_sessions_due_idx
            ON link_sessions(status, lease_expires_at_ms, expires_at_ms);

CREATE INDEX link_sessions_users_idx
            ON link_sessions(server_id, player_a_user_id, player_b_user_id, status);

CREATE INDEX mobile_create_requests_session_idx
            ON mobile_create_requests(mobile_session_id);

CREATE UNIQUE INDEX mobile_sessions_active_user_idx
            ON mobile_sessions(server_id, user_id)
            WHERE status IN ('CREATED', 'RUNNING', 'FINALIZING');

CREATE INDEX mobile_sessions_due_idx
            ON mobile_sessions(status, lease_expires_at_ms, hard_expires_at_ms);

CREATE UNIQUE INDEX n64_runtime_media_sessions_active_room_idx
            ON n64_runtime_media_sessions(server_id, room_number)
            WHERE status IN ('CREATED', 'WAITING_PEER', 'READY', 'RUNNING');

CREATE INDEX n64_runtime_media_sessions_due_idx
            ON n64_runtime_media_sessions(status, expires_at_ms);

CREATE INDEX pair_save_journals_state_idx
            ON pair_save_commit_journals(state);

CREATE INDEX rate_limit_buckets_expiry_idx ON rate_limit_buckets(expires_at_ms);

CREATE INDEX room_chat_recent_idx
            ON room_chat(server_id, room_number, message_id DESC);

CREATE INDEX room_code_tombstones_reuse_idx
            ON room_code_tombstones(reuse_after_ms);

CREATE INDEX room_members_auth_session_idx
            ON room_members(auth_session_id);

CREATE INDEX room_members_seen_idx
            ON room_members(server_id, last_seen_at_ms);

CREATE INDEX room_termination_notices_expiry_idx
            ON room_termination_notices(expires_at_ms);

CREATE INDEX save_locks_expiry_idx ON save_locks(lease_expires_at_ms);

CREATE INDEX save_upload_requests_state_idx
            ON save_upload_requests(state, updated_at_ms);

CREATE UNIQUE INDEX session_events_coalesce_idx
            ON session_events(coalesce_key) WHERE coalesce_key IS NOT NULL;

CREATE INDEX session_events_session_idx
            ON session_events(server_id, session_id, event_id DESC);
"""


REQUIRED_TABLES = frozenset({
    "auth_sessions", "game_runs", "game_session_fence_counters",
    "game_session_locks", "gb_runtime_fixed_host_sessions", "host_processes",
    "link_sessions", "mobile_create_requests", "mobile_sessions",
    "n64_runtime_media_sessions", "pair_save_commit_journals",
    "rate_limit_buckets", "rom_registrations", "rom_slots", "room_chat",
    "room_code_tombstones", "room_members", "room_termination_notices",
    "rooms", "save_locks", "save_records", "save_upload_requests",
    "session_events", "users",
})

class AuthorityDatabase:
    """Own the current SQLite baseline and per-operation connections."""

    def __init__(self, storage_root: Path | str, *, metrics=None):
        self.storage_root = Path(storage_root)
        self.data_dir = self.storage_root / "data"
        self.path = self.data_dir / "integral_emulator.sqlite3"
        self.metrics = metrics
        self._unit_connection = ContextVar("authority_unit_connection", default=None)

    @contextmanager
    def write_unit(self):
        """Explicitly compose repository metadata writes in one transaction.

        Filesystem commits must be completed separately before entering this
        scope; a SQLite rollback cannot undo a file replacement.
        """
        if self._unit_connection.get() is not None:
            raise RuntimeError("nested write unit")
        with self.transaction(write=True) as connection:
            token = self._unit_connection.set(connection)
            try:
                yield connection
            finally:
                self._unit_connection.reset(token)

    def initialize(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self._restrict_mode(self.data_dir, 0o700)
        with self.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            tables = self._table_names(connection)
            application_id = int(connection.execute("PRAGMA application_id").fetchone()[0])
            version = int(connection.execute("PRAGMA user_version").fetchone()[0])
            if not tables and application_id == 0 and version == 0:
                try:
                    for statement in BASELINE_SCHEMA_SQL.split(";"):
                        if statement.strip():
                            connection.execute(statement)
                    connection.execute(f"PRAGMA application_id = {DATABASE_APPLICATION_ID}")
                    connection.execute(f"PRAGMA user_version = {SCHEMA_BASELINE_VERSION}")
                    connection.commit()
                except sqlite3.Error as error:
                    connection.rollback()
                    raise DatabaseBaselineError("baseline schema creation failed") from error
            else:
                if (
                    application_id != DATABASE_APPLICATION_ID
                    or version != SCHEMA_BASELINE_VERSION
                    or tables != REQUIRED_TABLES
                ):
                    connection.rollback()
                    raise DatabaseBaselineError(
                        "database is not the current baseline; back it up and recreate it explicitly"
                    )
                connection.commit()
        self._restrict_mode(self.path, 0o600)

    def connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(
            self.path,
            timeout=5.0,
            isolation_level=None,
        )
        self._restrict_mode(self.path, 0o600)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA busy_timeout = 5000")
        connection.execute("PRAGMA foreign_keys = ON")
        for attempt in range(50):
            try:
                connection.execute("PRAGMA journal_mode = WAL")
                break
            except sqlite3.OperationalError as error:
                if "locked" not in str(error).lower() or attempt == 49:
                    connection.close()
                    raise
                time.sleep(0.1)
        connection.execute("PRAGMA synchronous = FULL")
        connection.execute("PRAGMA wal_autocheckpoint = 1000")
        connection.execute("PRAGMA journal_size_limit = 67108864")
        self._restrict_mode(Path(f"{self.path}-wal"), 0o600)
        self._restrict_mode(Path(f"{self.path}-shm"), 0o600)
        return connection

    @contextmanager
    def transaction(self, *, write: bool = False) -> Iterator[sqlite3.Connection]:
        shared = self._unit_connection.get()
        if shared is not None:
            yield shared
            return
        started = time.perf_counter()
        try:
            with self.connect() as connection:
                connection.execute("BEGIN IMMEDIATE" if write else "BEGIN")
                try:
                    yield connection
                except Exception:
                    connection.rollback()
                    raise
                else:
                    connection.commit()
        finally:
            if self.metrics is not None:
                self.metrics.record(
                    "db_transaction",
                    "write" if write else "read",
                    (time.perf_counter() - started) * 1000.0,
                )

    def integrity_report(self) -> dict[str, object]:
        with self.connect() as connection:
            integrity = [row[0] for row in connection.execute("PRAGMA integrity_check")]
            foreign_keys = [tuple(row) for row in connection.execute("PRAGMA foreign_key_check")]
            application_id = int(connection.execute("PRAGMA application_id").fetchone()[0])
            version = int(connection.execute("PRAGMA user_version").fetchone()[0])
            tables = self._table_names(connection)
        baseline_ok = (
            application_id == DATABASE_APPLICATION_ID
            and version == SCHEMA_BASELINE_VERSION
            and tables == REQUIRED_TABLES
        )
        return {
            "schema_version": version,
            "integrity_check": integrity,
            "foreign_key_violations": foreign_keys,
            "ok": baseline_ok and integrity == ["ok"] and not foreign_keys,
        }

    def backup(self, destination: Path | str) -> Path:
        target = Path(destination)
        target.parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as source, sqlite3.connect(target) as output:
            source.backup(output)
        self._restrict_mode(target, 0o600)
        return target

    @staticmethod
    def _table_names(connection: sqlite3.Connection) -> frozenset[str]:
        return frozenset(
            str(row[0])
            for row in connection.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
            )
        )

    @staticmethod
    def _restrict_mode(path: Path, mode: int) -> None:
        if os.name != "nt" and path.exists():
            path.chmod(mode)
