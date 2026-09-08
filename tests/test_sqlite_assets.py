from __future__ import annotations

import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
from pathlib import Path

from integral_emulator.database import AuthorityDatabase
from integral_emulator.errors import (
    NotFoundError,
    RevisionConflictError,
    SaveLockedError,
    ValidationError,
)
from integral_emulator.save_contract import SaveUploadAuthority
from integral_emulator.sqlite_assets import (
    SQLiteRomRegistry,
    SQLiteSaveManager,
    SaveCommitService,
)
from integral_emulator.sqlite_repositories import SQLiteAssetRepository
from integral_emulator.storage import LeagueStorage


class SQLiteRomRegistryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.database = AuthorityDatabase(Path(self.temporary.name))
        self.database.initialize()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, 1, 1)",
                ("owner", "owner", "owner"),
            )
        self.repository = SQLiteAssetRepository(self.database)
        self.registry = SQLiteRomRegistry(self.repository, "primary")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_register_list_and_get_preserve_rom_registry_contract(self) -> None:
        registration = self.registry.register(
            "owner", "a" * 64, "sample.z64", "n64", "jp", "b" * 40,
            "SAMPLE N64"
        )

        self.assertEqual(registration.region, "JP")
        self.assertEqual(self.registry.get(registration.id, "owner"), registration)
        self.assertEqual(self.registry.list_for_user("owner"), [registration])
        with self.assertRaises(NotFoundError):
            self.registry.get(registration.id, "another-user")

    def test_duplicate_registration_maps_repository_conflict_to_validation(self) -> None:
        self.registry.register("owner", "a" * 64, "sample.z64", "n64", "JP", rom_header_title="SAMPLE N64")

        with self.assertRaisesRegex(ValidationError, "already registered"):
            self.registry.register(
                "owner", "a" * 64, "copy.z64", "n64", "JP", rom_header_title="SAMPLE COPY"
            )

    def test_register_or_get_rejects_metadata_change(self) -> None:
        first = self.registry.register(
            "owner", "a" * 64, "sample.z64", "n64", "JP",
            rom_header_title="SAMPLE N64"
        )
        with self.assertRaisesRegex(ValidationError, "server-derived metadata"):
            self.registry.register_or_get(
                "owner", "a" * 64, "sample.z64", "n64", "JP",
                rom_header_title="CHANGED HEADER",
            )
        self.assertEqual(self.registry.get(first.id, "owner"), first)
        self.assertEqual(len(self.registry.list_for_user("owner")), 1)

    def test_repository_header_update_is_compare_and_set(self) -> None:
        registration = self.registry.register(
            "owner", "a" * 64, "sample.z64", "n64", "JP",
            rom_header_title="SAMPLE N64"
        )

        self.assertTrue(
            self.repository.update_rom_header_title(
                registration.id, "owner", "SAMPLE N64", "FIRST", 2
            )
        )
        self.assertFalse(
            self.repository.update_rom_header_title(
                registration.id, "owner", "SAMPLE N64", "STALE", 3
            )
        )
        self.assertEqual(
            self.repository.get_rom(registration.id, "owner")["rom_header_title"],
            "FIRST",
        )


class SaveCommitServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.storage = LeagueStorage(self.root)
        self.database = AuthorityDatabase(self.root)
        self.database.initialize()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, 1, 1)",
                ("owner", "owner", "owner"),
            )
        self.repository = SQLiteAssetRepository(self.database)
        self.save_path = "saves/owner/sample_alpha/current.sav"
        self.storage.atomic_write_bytes(
            self.storage.resolve_relative(self.save_path), b"revision-one"
        )
        import hashlib

        self.repository.create_save(
            {
                "save_id": "save", "server_id": "primary", "user_id": "owner",
                "rom_id": None, "game_type": "sample_alpha", "relative_path": self.save_path,
                "size_bytes": len(b"revision-one"),
                "sha256": hashlib.sha256(b"revision-one").hexdigest(), "revision": 1,
                "created_at_ms": 1, "updated_at_ms": 1,
            }
        )
        self.service = SaveCommitService(self.storage, self.repository)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def commit(self, request_id: str = "request-1"):
        return self.service.commit(
            user_id="owner", save_id="save", request_id=request_id,
            expected_revision=1, save_bytes=b"revision-two",
            authority=SaveUploadAuthority.unlocked(),
        )

    def test_commit_and_response_loss_replay_advance_revision_once(self) -> None:
        saved, replayed = self.commit()
        replay, was_replayed = self.commit()

        self.assertFalse(replayed)
        self.assertTrue(was_replayed)
        self.assertEqual((saved.revision, replay.revision), (2, 2))
        self.assertEqual(
            self.storage.resolve_relative(self.save_path).read_bytes(), b"revision-two"
        )
        backup = self.root / "save_backups/owner/save/backup_0001.sav"
        self.assertEqual(backup.read_bytes(), b"revision-one")

    def test_fault_after_replace_rolls_forward_on_retry(self) -> None:
        raised = False

        def fail_once(point):
            nonlocal raised
            if point == "after_atomic_replace" and not raised:
                raised = True
                raise RuntimeError("injected after replace")

        self.service.fault_injector = fail_once
        with self.assertRaisesRegex(RuntimeError, "after replace"):
            self.commit("replace-fault")
        self.service = SaveCommitService(self.storage, self.repository)

        saved, replayed = self.commit("replace-fault")
        self.assertFalse(replayed)
        self.assertEqual(saved.revision, 2)
        self.assertEqual(
            self.repository.get_save_upload(
                self._request_key("replace-fault")
            )["state"],
            "COMMITTED",
        )

    def test_prepared_candidate_is_recovered_without_request_replay(self) -> None:
        raised = False

        def fail_once(point):
            nonlocal raised
            if point == "after_atomic_replace" and not raised:
                raised = True
                raise RuntimeError("injected")

        self.service.fault_injector = fail_once
        with self.assertRaises(RuntimeError):
            self.commit("startup-recovery")
        recovered = SaveCommitService(
            self.storage, self.repository
        ).reconcile_prepared()

        self.assertEqual(recovered, [self._request_key("startup-recovery")])
        self.assertEqual(self.repository.get_save("save", "owner")["revision"], 2)

    def test_request_identity_and_path_escape_fail_closed(self) -> None:
        self.commit("same-id")
        with self.assertRaisesRegex(ValidationError, "different save upload"):
            self.service.commit(
                user_id="owner", save_id="save", request_id="same-id",
                expected_revision=1, save_bytes=b"different",
                authority=SaveUploadAuthority.unlocked(),
            )
        with self.assertRaises(ValueError):
            self.repository.prepare_save_upload(
                {
                    "request_id": "bad-path", "save_id": "save", "user_id": "owner",
                    "expected_revision": 2, "base_sha256": "a" * 64,
                    "candidate_sha256": "b" * 64, "candidate_size": 1,
                    "candidate_relative_path": "../escape.sav",
                    "backup_relative_path": "backup.sav",
                },
                1,
            )

    def test_parallel_same_request_has_one_commit_and_replays(self) -> None:
        with ThreadPoolExecutor(max_workers=16) as executor:
            results = list(executor.map(lambda _index: self.commit("parallel"), range(16)))

        self.assertEqual(sum(not replayed for _save, replayed in results), 1)
        self.assertEqual({save.revision for save, _replayed in results}, {2})
        self.assertEqual(self.repository.get_save("save", "owner")["revision"], 2)

    def test_request_replay_returns_its_committed_result(self) -> None:
        first, _replayed = self.commit("first")
        second, _replayed = self.service.commit(
            user_id="owner", save_id="save", request_id="second",
            expected_revision=2, save_bytes=b"revision-three",
            authority=SaveUploadAuthority.unlocked(),
        )
        replay, was_replayed = self.commit("first")

        self.assertEqual((first.revision, second.revision), (2, 3))
        self.assertTrue(was_replayed)
        self.assertEqual((replay.revision, replay.sha256), (first.revision, first.sha256))
        self.assertEqual(self.repository.get_save("save", "owner")["revision"], 3)

    @staticmethod
    def _request_key(request_id: str) -> str:
        import hashlib

        return hashlib.sha256(f"owner\0{request_id}".encode("utf-8")).hexdigest()


class SQLiteSaveManagerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.storage = LeagueStorage(self.root)
        self.database = AuthorityDatabase(self.root)
        self.database.initialize()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, 1, 1)",
                ("owner", "owner", "owner"),
            )
            connection.execute(
                """
                INSERT INTO auth_sessions (
                    token_digest, user_id, server_id, created_at_ms,
                    expires_at_ms, last_access_at_ms
                ) VALUES (?, ?, 'primary', 1, 9999999999999, 1)
                """,
                ("owner-token", "owner"),
            )
        self.repository = SQLiteAssetRepository(self.database)
        self.manager = SQLiteSaveManager(
            self.storage, self.repository, "primary", backup_generations=2
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_create_download_list_and_delete_use_sqlite_metadata(self) -> None:
        created = self.manager.create_save("owner", "sample_alpha", b"save-data")
        downloaded, payload = self.manager.download(created.id, "owner")

        self.assertEqual(payload, b"save-data")
        self.assertEqual(downloaded, created)
        self.assertEqual(self.manager.list_for_user("owner"), [created])
        with self.assertRaisesRegex(ValidationError, "already exists"):
            self.manager.create_save("owner", "sample_alpha", b"another")
        deleted = self.manager.delete_save(created.id, "owner")
        self.assertEqual(deleted.id, created.id)
        self.assertFalse(self.storage.resolve_relative(created.storage_path).exists())

    def test_slot_saves_of_same_game_remain_independent(self) -> None:
        first = self.manager.create_save_for_slot("owner", 1, "sample_alpha", b"one")
        second = self.manager.create_save_for_slot("owner", 2, "sample_alpha", b"two")

        self.assertNotEqual(first.id, second.id)
        self.assertNotEqual(first.storage_path, second.storage_path)
        self.assertEqual(len(self.manager.list_for_user("owner")), 2)

    def test_parallel_same_game_create_has_one_winner(self) -> None:
        def create(index: int) -> bool:
            try:
                self.manager.create_save("owner", "sample_alpha", f"save-{index}".encode())
                return True
            except ValidationError:
                return False

        with ThreadPoolExecutor(max_workers=16) as executor:
            results = list(executor.map(create, range(16)))

        self.assertEqual(sum(results), 1)
        self.assertEqual(len(self.manager.list_for_user("owner")), 1)

    def test_lock_requires_auth_and_fence_and_rejects_stale_renew(self) -> None:
        save = self.manager.create_save("owner", "sample_alpha", b"save-data")
        expires = (datetime.now(timezone.utc) + timedelta(minutes=5)).isoformat()
        with self.assertRaisesRegex(ValidationError, "requires auth session"):
            self.manager.lock_save(save.id, "owner", "run", expires)
        locked = self.manager.lock_save(
            save.id,
            "owner",
            "run",
            expires,
            auth_session_id="owner-token",
            fencing_token=7,
        )
        self.assertEqual(locked.lock_owner, "run")
        with self.assertRaisesRegex(SaveLockedError, "stale"):
            self.manager.renew_lock(
                save.id,
                "owner",
                "run",
                expires,
                auth_session_id="owner-token",
                fencing_token=6,
            )
        unlocked = self.manager.unlock_save(
            save.id, "owner", "run", fencing_token=7
        )
        self.assertIsNone(unlocked.lock_owner)

    def test_download_detects_file_metadata_drift(self) -> None:
        save = self.manager.create_save("owner", "sample_alpha", b"save-data")
        self.storage.resolve_relative(save.storage_path).write_bytes(b"tampered")

        with self.assertRaisesRegex(RevisionConflictError, "hash"):
            self.manager.download(save.id, "owner")


if __name__ == "__main__":
    unittest.main()
