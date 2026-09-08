from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import tempfile
import unittest
from pathlib import Path

from integral_emulator.database import AuthorityDatabase
from integral_emulator.sqlite_repositories import (
    RepositoryConflictError,
    SQLiteAssetRepository,
    SQLiteAuthRepository,
    SQLiteGameSessionRepository,
    SQLiteSessionAuthorityRepository,
)


class SQLiteAuthorityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.database = AuthorityDatabase(Path(self.temporary.name))
        self.database.initialize()
        self.games = SQLiteGameSessionRepository(self.database)
        self.auth = SQLiteAuthRepository(self.database)
        self.assets = SQLiteAssetRepository(self.database)
        self.session_authority = SQLiteSessionAuthorityRepository(self.database)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def seed_user(self, user_id: str, *, token: str | None = None) -> None:
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, 1, 1)",
                (user_id, user_id, user_id),
            )
            if token:
                connection.execute(
                """
                INSERT INTO auth_sessions (
                    token_digest, user_id, server_id, created_at_ms,
                    expires_at_ms, last_access_at_ms
                ) VALUES (?, ?, 'primary', 1, 999999, 1)
                """,
                    (token, user_id),
                )

    def seed_mobile_authority(self, user_id: str = "mobile-user") -> None:
        self.seed_user(user_id, token=f"token-{user_id}")
        with self.database.transaction(write=True) as connection:
            connection.execute(
                """
                INSERT INTO game_runs (
                    game_run_id, server_id, execution_mode, status,
                    auth_session_id, created_at_ms, expires_at_ms
                ) VALUES (?, 'primary', 'MOBILE_CLIENT', 'RUNNING', ?, 1, 1000)
                """,
                (f"run-{user_id}", f"token-{user_id}"),
            )

    def seed_mobile_assets(self, user_id: str = "mobile-user") -> None:
        with self.database.transaction(write=True) as connection:
            connection.execute(
                """
                INSERT INTO rom_registrations (
                    rom_id, server_id, user_id, sha256, game_type, title,
                    region, rom_header_title, size_bytes, created_at_ms,
                    updated_at_ms
                ) VALUES ('rom-mobile', 'primary', ?, ?, 'sample_gamma',
                    'Synthetic Sample', 'JP', 'SAMPLE GAMMA', 1024, 1, 1)
                """,
                (user_id, "c" * 64),
            )
            connection.execute(
                """
                INSERT INTO save_records (
                    save_id, server_id, user_id, rom_id, game_type,
                    relative_path, size_bytes, sha256, revision,
                    created_at_ms, updated_at_ms
                ) VALUES ('save-mobile', 'primary', ?, 'rom-mobile', 'sample_gamma',
                    'saves/mobile/current.sav', 32768, ?, 1, 1, 1)
                """,
                (user_id, "b" * 64),
            )

    @staticmethod
    def mobile_record(user_id: str = "mobile-user") -> dict:
        return {
            "session_id": "mobile-1", "server_id": "primary", "user_id": user_id,
            "save_id": None, "rom_id": None,
            "auth_session_id": f"token-{user_id}", "package_id": "sample_gamma",
            "release_id": "release-1", "package_digest": "a" * 64,
            "game_run_id": f"run-{user_id}", "fencing_token": 1,
            "game_session_id": f"game-{user_id}",
            "scenario_id": "default", "status": "RUNNING",
            "created_at_ms": 1, "updated_at_ms": 1, "lease_expires_at_ms": 10,
        }

    def test_mobile_create_journal_replays_committed_session(self) -> None:
        self.seed_mobile_authority()
        self.seed_mobile_assets()
        request = {
            "request_digest": "d" * 64,
            "user_id": "mobile-user", "request_id": "mobile-create:one",
            "save_id": "save-mobile", "rom_id": "rom-mobile",
            "rom_header_title": "SAMPLE GAMMA", "package_id": "sample_gamma",
            "release_id": "release-1", "package_digest": "a" * 64,
            "scenario_id": "default", "auth_session_id": "token-mobile-user",
            "created_at_ms": 1,
        }
        _prepared, is_new = self.session_authority.prepare_mobile_create(request)
        self.assertTrue(is_new)
        record = dict(
            self.mobile_record(), save_id="save-mobile", rom_id="rom-mobile"
        )
        first = self.session_authority.commit_mobile_create("d" * 64, record)
        replay, is_new = self.session_authority.prepare_mobile_create(request)
        second = self.session_authority.commit_mobile_create("d" * 64, record)
        self.assertFalse(is_new)
        self.assertEqual(replay["state"], "CREATED")
        self.assertEqual(first["session_id"], second["session_id"])
        self.assertEqual(first["game_session_id"], "game-mobile-user")

    def test_mobile_create_journal_rejects_mismatched_reuse(self) -> None:
        self.seed_mobile_authority()
        self.seed_mobile_assets()
        request = {
            "request_digest": "e" * 64,
            "user_id": "mobile-user", "request_id": "mobile-create:one",
            "save_id": "save-mobile", "rom_id": "rom-mobile",
            "rom_header_title": "SAMPLE GAMMA", "package_id": "sample_gamma",
            "release_id": "release-1", "package_digest": "a" * 64,
            "scenario_id": "default", "auth_session_id": "token-mobile-user",
            "created_at_ms": 1,
        }
        self.session_authority.prepare_mobile_create(request)
        with self.assertRaises(RepositoryConflictError):
            self.session_authority.prepare_mobile_create(
                dict(request, scenario_id="different")
            )


    def test_pair_lock_conflict_rolls_back_both_users(self) -> None:
        for user, token in (("a", "tok-a"), ("b", "tok-b"), ("c", "tok-c")):
            self.seed_user(user, token=token)
        first_run = {
            "game_run_id": "run-1", "server_id": "primary", "execution_mode": "ROOM",
            "status": "RUNNING", "auth_session_id": "tok-b", "created_at_ms": 1,
            "expires_at_ms": 1000,
        }
        self.games.acquire_pair(
            first_run,
            {"user_id": "b", "auth_session_id": "tok-b", "lease_expires_at_ms": 900},
            {"user_id": "c", "auth_session_id": "tok-c", "lease_expires_at_ms": 900},
            now_ms=10,
        )
        second_run = dict(first_run, game_run_id="run-2", auth_session_id="tok-a")
        with self.assertRaises(RepositoryConflictError):
            self.games.acquire_pair(
                second_run,
                {"user_id": "a", "auth_session_id": "tok-a", "lease_expires_at_ms": 900},
                {"user_id": "b", "auth_session_id": "tok-b", "lease_expires_at_ms": 900},
                now_ms=10,
            )
        with self.database.connect() as connection:
            self.assertIsNone(
                connection.execute("SELECT 1 FROM game_runs WHERE game_run_id = 'run-2'").fetchone()
            )
            self.assertIsNone(
                connection.execute(
                    "SELECT 1 FROM game_session_fence_counters WHERE user_id = 'a'"
                ).fetchone()
            )

    def test_stale_fence_and_auth_session_cannot_renew(self) -> None:
        self.seed_user("a", token="tok-a")
        self.seed_user("b", token="tok-b")
        run = {
            "game_run_id": "run", "server_id": "primary", "execution_mode": "ROOM",
            "status": "RUNNING", "auth_session_id": "tok-a", "created_at_ms": 1,
            "expires_at_ms": 1000,
        }
        fence_a, _fence_b = self.games.acquire_pair(
            run,
            {"user_id": "a", "auth_session_id": "tok-a", "lease_expires_at_ms": 900},
            {"user_id": "b", "auth_session_id": "tok-b", "lease_expires_at_ms": 900},
            now_ms=10,
        )
        self.assertFalse(self.games.renew_lock("a", "run", "tok-a", fence_a + 1, 950))
        self.assertFalse(self.games.renew_lock("a", "run", "tok-b", fence_a, 950))
        self.assertTrue(self.games.renew_lock("a", "run", "tok-a", fence_a, 950))

    def test_repeated_lease_event_is_coalesced(self) -> None:
        self.games.record_event(
            "primary", "run", "LOCK_RENEWED", {"lease": 10}, 10,
            coalesce_key="primary:run:a:lease",
        )
        self.games.record_event(
            "primary", "run", "LOCK_RENEWED", {"lease": 20}, 20,
            coalesce_key="primary:run:a:lease",
        )
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT occurrences, last_created_at_ms, payload_json FROM session_events"
            ).fetchone()
        self.assertEqual((row[0], row[1]), (2, 20))
        self.assertEqual(row[2], '{"lease":20}')

    def test_concurrent_duplicate_username_has_one_winner(self) -> None:
        def register(index: int) -> bool:
            try:
                self.auth.create_user(
                    {
                        "user_id": f"duplicate-{index}", "username": "Same-Name",
                        "username_normalized": "same-name",
                        "password_hash": "hash", "created_at_ms": index + 1,
                        "updated_at_ms": index + 1,
                    }
                )
                return True
            except RepositoryConflictError:
                return False

        with ThreadPoolExecutor(max_workers=16) as executor:
            results = list(executor.map(register, range(32)))
        self.assertEqual(sum(results), 1)

    def test_duplicate_email_preserves_current_auth_contract(self) -> None:
        for index in range(2):
            self.auth.create_user(
                {
                    "user_id": f"email-user-{index}",
                    "username": f"email-user-{index}",
                    "username_normalized": f"email-user-{index}",
                    "email": "shared@example.com", "password_hash": "hash",
                    "created_at_ms": index + 1, "updated_at_ms": index + 1,
                }
            )
        with self.database.connect() as connection:
            count = connection.execute(
                "SELECT COUNT(*) FROM users WHERE email = 'shared@example.com'"
            ).fetchone()[0]
        self.assertEqual(count, 2)

    def test_token_expiry_and_compare_and_set_touch_fail_closed(self) -> None:
        self.seed_user("identity")
        self.auth.create_session(
            {
                "token_digest": "digest", "user_id": "identity", "server_id": "primary",
                "created_at_ms": 10, "expires_at_ms": 100, "last_access_at_ms": 10,
            }
        )
        self.assertIsNotNone(self.auth.find_identity_by_token_digest("digest", 99))
        self.assertIsNone(self.auth.find_identity_by_token_digest("digest", 100))
        self.assertTrue(self.auth.touch_session("digest", 10, 20))
        self.assertFalse(self.auth.touch_session("digest", 10, 30))

    def test_asset_reads_slots_and_expired_lock_cleanup_are_row_scoped(self) -> None:
        self.seed_user("owner", token="owner-token")
        self.assets.register_rom(
            {
                "rom_id": "rom", "server_id": "primary", "user_id": "owner",
                "sha256": "a" * 64, "game_type": "sample_gamma", "title": "SAMPLE GAMMA",
                "region": "JP", "created_at_ms": 1, "updated_at_ms": 1,
            }
        )
        self.assets.create_save(
            {
                "save_id": "save", "server_id": "primary", "user_id": "owner",
                "rom_id": "rom", "game_type": "sample_gamma",
                "relative_path": "saves/owner/save.sav", "size_bytes": 32,
                "sha256": "b" * 64, "revision": 1,
                "created_at_ms": 1, "updated_at_ms": 1,
            }
        )
        self.assets.upsert_slot(
            {
                "server_id": "primary", "user_id": "owner", "slot": 7,
                "rom_id": "rom", "save_id": "save", "filename": "sample_gamma.gbc",
                "updated_at_ms": 2,
            }
        )
        slots = self.assets.list_slots("primary", "owner")
        self.assertEqual((slots[0]["slot"], slots[0]["sha256"]), (7, "a" * 64))
        self.assertEqual(self.assets.get_save("save", "owner")["revision"], 1)
        self.assertEqual(len(self.assets.list_saves("primary", "owner")), 1)

        self.assertTrue(
            self.assets.acquire_save_lock(
                {
                    "save_id": "save", "user_id": "owner", "owner_id": "manual",
                    "auth_session_id": "owner-token", "fencing_token": 1,
                    "lease_expires_at_ms": 20,
                },
                now_ms=10,
            )
        )
        self.assertFalse(self.assets.delete_save("save", "owner", now_ms=19))
        with self.assertRaisesRegex(RepositoryConflictError, "still referenced"):
            self.assets.delete_save("save", "owner", now_ms=20)
        self.assertTrue(self.assets.delete_slot("primary", "owner", 7))
        self.assertTrue(self.assets.delete_save("save", "owner", now_ms=20))
        self.assertIsNone(self.assets.get_save("save", "owner"))

    def test_save_lock_renew_rejects_stale_authority(self) -> None:
        self.seed_user("owner", token="owner-token")
        self.assets.create_save(
            {
                "save_id": "save", "server_id": "primary", "user_id": "owner",
                "rom_id": None, "game_type": "sample_gamma",
                "relative_path": "saves/owner/save.sav", "size_bytes": 32,
                "sha256": "b" * 64, "revision": 1,
                "created_at_ms": 1, "updated_at_ms": 1,
            }
        )
        lock = {
            "save_id": "save", "user_id": "owner", "owner_id": "manual",
            "auth_session_id": "owner-token", "fencing_token": 4,
            "lease_expires_at_ms": 20,
        }
        self.assertTrue(self.assets.acquire_save_lock(lock, now_ms=10))
        self.assertFalse(
            self.assets.renew_save_lock(
                "save", "owner", "manual", "owner-token", 3, 30, 11
            )
        )
        self.assertTrue(
            self.assets.renew_save_lock(
                "save", "owner", "manual", "owner-token", 4, 30, 11
            )
        )
        self.assertTrue(self.assets.release_save_lock("save", "owner", "manual", 4))

    def test_save_upload_prepare_and_metadata_commit_are_idempotent(self) -> None:
        self.seed_user("owner")
        self.assets.create_save(
            {
                "save_id": "save", "server_id": "primary", "user_id": "owner",
                "rom_id": None, "game_type": "sample_gamma",
                "relative_path": "saves/owner/save.sav", "size_bytes": 3,
                "sha256": "a" * 64, "revision": 7,
                "created_at_ms": 1, "updated_at_ms": 1,
            }
        )
        upload = {
            "request_id": "request-digest", "save_id": "save", "user_id": "owner",
            "expected_revision": 7, "base_sha256": "a" * 64,
            "candidate_sha256": "b" * 64, "candidate_size": 4,
            "authority_mode": "UNLOCKED", "game_run_id": None,
            "fencing_token": 0, "mobile_session_id": None, "lock_owner": None,
            "auth_session_id": None,
            "candidate_relative_path": "save_upload_candidates/request-digest.sav",
            "backup_relative_path": "save_backups/owner/save/backup_0007.sav",
            "created_at_ms": 2, "updated_at_ms": 2,
        }
        prepared, created = self.assets.prepare_save_upload(upload, now_ms=2)
        replay, replay_created = self.assets.prepare_save_upload(upload, now_ms=3)
        self.assertTrue(created)
        self.assertFalse(replay_created)
        self.assertEqual(replay["request_id"], prepared["request_id"])

        committed = self.assets.commit_prepared_save_upload("request-digest", 4)
        committed_replay = self.assets.commit_prepared_save_upload("request-digest", 5)
        self.assertEqual((committed["revision"], committed["sha256"]), (8, "b" * 64))
        self.assertEqual(committed_replay["revision"], 8)
        self.assertEqual(self.assets.list_prepared_save_uploads(), [])

    def test_locked_save_upload_prepare_rejects_stale_auth_and_fence(self) -> None:
        self.seed_user("owner", token="owner-token")
        with self.database.transaction(write=True) as connection:
            connection.execute(
                """
                INSERT INTO game_runs (
                    game_run_id, server_id, execution_mode, status,
                    auth_session_id, created_at_ms, expires_at_ms
                ) VALUES ('run', 'primary', 'LOCAL_CLIENT', 'RUNNING', 'owner-token', 1, 100)
                """
            )
        self.assets.create_save(
            {
                "save_id": "save", "server_id": "primary", "user_id": "owner",
                "rom_id": None, "game_type": "sample_gamma",
                "relative_path": "saves/owner/save.sav", "size_bytes": 3,
                "sha256": "a" * 64, "revision": 1,
                "created_at_ms": 1, "updated_at_ms": 1,
            }
        )
        self.assertTrue(
            self.assets.acquire_save_lock(
                {
                    "save_id": "save", "user_id": "owner", "owner_id": "mobile",
                    "game_run_id": "run", "auth_session_id": "owner-token",
                    "fencing_token": 9, "lease_expires_at_ms": 50,
                },
                now_ms=2,
            )
        )
        upload = {
            "request_id": "locked-request", "save_id": "save", "user_id": "owner",
            "expected_revision": 1, "base_sha256": "a" * 64,
            "candidate_sha256": "b" * 64, "candidate_size": 4,
            "authority_mode": "MOBILE", "game_run_id": "run", "fencing_token": 8,
            "mobile_session_id": "mobile", "lock_owner": "mobile",
            "auth_session_id": "owner-token",
            "candidate_relative_path": "save_upload_candidates/locked-request.sav",
            "backup_relative_path": "save_backups/owner/save/backup_0001.sav",
            "created_at_ms": 2, "updated_at_ms": 2,
        }
        with self.assertRaisesRegex(RepositoryConflictError, "authority is stale"):
            self.assets.prepare_save_upload(upload, now_ms=2)
        upload["fencing_token"] = 9
        _prepared, created = self.assets.prepare_save_upload(upload, now_ms=2)
        self.assertTrue(created)

    def test_session_transition_is_versioned_and_rejects_wrong_from_state(self) -> None:
        self.seed_user("a")
        self.seed_user("b")
        self.session_authority.insert(
            "link",
            {
                "session_id": "link-1", "server_id": "primary", "room_number": 1,
                "player_a_user_id": "a", "player_b_user_id": "b",
                "status": "RUNNING", "requested_link_mode": "trade",
                "protocol_id": "gb_runtime-v2", "created_at_ms": 1,
                "updated_at_ms": 1, "lease_expires_at_ms": 20,
                "metadata_json": {"bounded": True},
            },
        )
        self.assertFalse(
            self.session_authority.transition(
                "link", "link-1", from_states={"CREATED"}, to_state="COMPLETED",
                expected_version=1, updated_at_ms=2,
            )
        )
        self.assertTrue(
            self.session_authority.transition(
                "link", "link-1", from_states={"RUNNING"}, to_state="FINALIZING",
                expected_version=1, updated_at_ms=2, due_at_ms=30,
            )
        )
        self.assertFalse(
            self.session_authority.transition(
                "link", "link-1", from_states={"FINALIZING"}, to_state="COMPLETED",
                expected_version=1, updated_at_ms=3,
            )
        )
        record = self.session_authority.get("link", "link-1")
        self.assertEqual((record["status"], record["row_version"]), ("FINALIZING", 2))
        self.assertEqual(record["metadata_json"], {"bounded": True})

    def test_due_session_query_is_repeatable_and_read_only(self) -> None:
        self.seed_mobile_authority()
        self.session_authority.insert("mobile", self.mobile_record())
        first = self.session_authority.list_due(
            "mobile", server_id="primary", active_states={"RUNNING"}, now_ms=10,
        )
        second = self.session_authority.list_due(
            "mobile", server_id="primary", active_states={"RUNNING"}, now_ms=10,
        )
        self.assertEqual([row["session_id"] for row in first], ["mobile-1"])
        self.assertEqual([row["session_id"] for row in second], ["mobile-1"])

    def test_due_query_is_scoped_to_server_environment(self) -> None:
        self.seed_user("link-a")
        self.seed_user("link-b")
        common = {
            "room_number": 1,
            "player_a_user_id": "link-a",
            "player_b_user_id": "link-b",
            "status": "RUNNING",
            "requested_link_mode": "battle",
            "protocol_id": "v2",
            "created_at_ms": 1,
            "updated_at_ms": 1,
            "lease_expires_at_ms": 10,
        }
        self.session_authority.insert(
            "link", {"session_id": "link-primary", "server_id": "primary", **common}
        )
        self.session_authority.insert(
            "link", {"session_id": "link-secondary", "server_id": "secondary", **common}
        )
        due = self.session_authority.list_due(
            "link", server_id="primary", active_states={"RUNNING"}, now_ms=10,
        )
        self.assertEqual([row["session_id"] for row in due], ["link-primary"])

    def test_mobile_heartbeat_updates_only_lease_authority_columns(self) -> None:
        self.seed_mobile_authority()
        record = self.mobile_record()
        record.update(
            save_id=None,
            rom_id=None,
            auth_session_id="token-mobile-user",
            game_run_id="run-mobile-user",
            rom_header_title="SAMPLE GAMMA",
            scenario_display_name="SYNTHETIC SCENARIO",
        )
        self.session_authority.create_mobile(record)
        before = self.session_authority.find_mobile("mobile-1", "mobile-user")
        self.assertTrue(
            self.session_authority.renew_mobile(
                "mobile-1", user_id="mobile-user",
                auth_session_id="token-mobile-user", game_run_id="run-mobile-user",
                fencing_token=1, expected_version=1,
                lease_expires_at_ms=50, updated_at_ms=5,
            )
        )
        self.assertFalse(
            self.session_authority.renew_mobile(
                "mobile-1", user_id="mobile-user",
                auth_session_id="token-mobile-user", game_run_id="run-mobile-user",
                fencing_token=1, expected_version=1,
                lease_expires_at_ms=60, updated_at_ms=6,
            )
        )
        after = self.session_authority.find_mobile("mobile-1", "mobile-user")
        self.assertEqual(after["lease_expires_at_ms"], 50)
        self.assertEqual(after["row_version"], 2)
        self.assertEqual(after["metadata_json"], before["metadata_json"])

    def test_mobile_terminal_transition_rejects_stale_fence(self) -> None:
        self.seed_mobile_authority()
        record = self.mobile_record()
        record.update(
            save_id=None, rom_id=None, auth_session_id="token-mobile-user",
            game_run_id="run-mobile-user",
        )
        self.session_authority.create_mobile(record)
        self.assertFalse(
            self.session_authority.complete_mobile(
                "mobile-1", user_id="mobile-user",
                auth_session_id="token-mobile-user", game_run_id="run-mobile-user",
                fencing_token=2, expected_version=1, status="COMPLETED",
                updated_at_ms=20,
            )
        )
        self.assertTrue(
            self.session_authority.complete_mobile(
                "mobile-1", user_id="mobile-user",
                auth_session_id="token-mobile-user", game_run_id="run-mobile-user",
                fencing_token=1, expected_version=1, status="COMPLETED",
                updated_at_ms=20,
            )
        )

    def test_session_metadata_limit_is_enforced_before_database_write(self) -> None:
        self.seed_mobile_authority()
        with self.assertRaisesRegex(ValueError, "too large"):
            record = self.mobile_record()
            record.update(session_id="too-large", metadata_json={"value": "x" * 70000})
            self.session_authority.insert("mobile", record)


if __name__ == "__main__":
    unittest.main()
