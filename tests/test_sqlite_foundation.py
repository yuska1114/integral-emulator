from __future__ import annotations

import sqlite3
import tempfile
import unittest
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from integral_emulator.database import (
    AuthorityDatabase,
    DATABASE_APPLICATION_ID,
    DatabaseBaselineError,
    REQUIRED_TABLES,
    SCHEMA_BASELINE_VERSION,
)


class AuthorityDatabaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.database = AuthorityDatabase(self.root)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_initialize_is_idempotent_and_enables_required_pragmas(self) -> None:
        self.database.initialize()
        self.database.initialize()

        with self.database.connect() as connection:
            self.assertEqual(connection.execute("PRAGMA foreign_keys").fetchone()[0], 1)
            self.assertEqual(connection.execute("PRAGMA journal_mode").fetchone()[0], "wal")
            self.assertEqual(connection.execute("PRAGMA synchronous").fetchone()[0], 2)
            self.assertEqual(connection.execute("PRAGMA busy_timeout").fetchone()[0], 5000)
            self.assertEqual(
                connection.execute("PRAGMA application_id").fetchone()[0],
                DATABASE_APPLICATION_ID,
            )
            self.assertEqual(
                connection.execute("PRAGMA user_version").fetchone()[0],
                SCHEMA_BASELINE_VERSION,
            )
            tables = {
                row[0]
                for row in connection.execute(
                    "SELECT name FROM sqlite_master "
                    "WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"
                )
            }
            if os.name != "nt":
                self.assertEqual(self.database.data_dir.stat().st_mode & 0o777, 0o700)
                self.assertEqual(self.database.path.stat().st_mode & 0o777, 0o600)
                for suffix in ("-wal", "-shm"):
                    sidecar = Path(f"{self.database.path}{suffix}")
                    if sidecar.exists():
                        self.assertEqual(sidecar.stat().st_mode & 0o777, 0o600)
        self.assertEqual(tables, REQUIRED_TABLES)
        self.assertEqual(
            self.database.integrity_report(),
            {
                "schema_version": SCHEMA_BASELINE_VERSION,
                "integrity_check": ["ok"],
                "foreign_key_violations": [],
                "ok": True,
            },
        )

    def test_parallel_initializers_create_one_baseline(self) -> None:
        with ThreadPoolExecutor(max_workers=2) as executor:
            list(
                executor.map(
                    lambda _worker: AuthorityDatabase(self.root).initialize(),
                    ("api", "relay"),
                )
            )
        self.assertEqual(
            self.database.integrity_report()["schema_version"],
            SCHEMA_BASELINE_VERSION,
        )

    def test_existing_nonbaseline_database_is_rejected_without_changes(self) -> None:
        self.database.data_dir.mkdir(parents=True)
        with sqlite3.connect(self.database.path) as connection:
            connection.execute("CREATE TABLE unrelated_data (value TEXT)")
            connection.execute("INSERT INTO unrelated_data VALUES ('preserve-me')")
        with self.assertRaisesRegex(DatabaseBaselineError, "back it up and recreate"):
            self.database.initialize()
        with sqlite3.connect(self.database.path) as connection:
            self.assertEqual(
                connection.execute("SELECT value FROM unrelated_data").fetchone()[0],
                "preserve-me",
            )
            self.assertEqual(connection.execute("PRAGMA application_id").fetchone()[0], 0)

    def test_room_session_expiry_schema_has_one_deadline(self) -> None:
        self.database.initialize()
        with self.database.connect() as connection:
            for table in (
                "game_runs",
                "link_sessions",
                "n64_runtime_media_sessions",
            ):
                columns = {
                    row[1]
                    for row in connection.execute(
                        f"PRAGMA table_info({table})"
                    ).fetchall()
                }
                self.assertIn("expires_at_ms", columns)

    def test_write_transaction_rolls_back_as_a_unit(self) -> None:
        self.database.initialize()
        with self.assertRaises(RuntimeError):
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    """
                    INSERT INTO users VALUES
                        ('user-1', 'One', 'one', NULL, 'hash', 'active', 0, 1, 1)
                    """
                )
                raise RuntimeError("stop")
        with self.database.connect() as connection:
            count = connection.execute("SELECT COUNT(*) FROM users").fetchone()[0]
        self.assertEqual(count, 0)

    def test_room_constraints_reject_a_third_or_duplicate_seat(self) -> None:
        self.database.initialize()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("owner", "owner", "owner", None, "hash", "active", 0, 1, 1),
            )
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("user-a", "user-a", "user-a", None, "hash", "active", 0, 1, 1),
            )
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("user-b", "user-b", "user-b", None, "hash", "active", 0, 1, 1),
            )
            connection.execute(
                "INSERT INTO rooms VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("primary", 1, "link_cable", "ABC123", "owner", "trade", 0, None, 1, 1, None),
            )
            connection.execute(
                "INSERT INTO room_members VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("primary", 1, "user-a", 1, "a", 1, None, 0, 1, 1, 1, None, None),
            )
        with self.assertRaises(sqlite3.IntegrityError):
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    "INSERT INTO room_members VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    ("primary", 1, "user-b", 1, "b", 2, None, 0, 1, 1, 1, None, None),
                )

    def test_room_constraints_cover_full_product_room_namespace(self) -> None:
        self.database.initialize()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("owner", "owner", "owner", None, "hash", "active", 0, 1, 1),
            )
            for room_number, room_type in ((64, "link_cable"), (65, "n64"), (128, "n64")):
                connection.execute(
                    "INSERT INTO rooms VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        "primary", room_number, room_type, None, "owner", "battle",
                        0, None, 1, 1, None,
                    ),
                )
            for room_number, room_type in ((0, "link_cable"), (65, "link_cable"), (129, "n64")):
                with self.assertRaises(sqlite3.IntegrityError):
                    connection.execute(
                        "INSERT INTO rooms VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (
                            "primary", room_number, room_type, None, "owner", "battle",
                            0, None, 1, 1, None,
                        ),
                    )

    def test_online_backup_passes_integrity_check(self) -> None:
        self.database.initialize()
        backup_path = self.database.backup(self.root / "backup" / "authority.sqlite3")
        with sqlite3.connect(backup_path) as connection:
            self.assertEqual(connection.execute("PRAGMA integrity_check").fetchone()[0], "ok")


if __name__ == "__main__":
    unittest.main()
