from __future__ import annotations

import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
from pathlib import Path

from integral_emulator.database import AuthorityDatabase
from integral_emulator.errors import GameSessionFenceError, ValidationError
from integral_emulator.sqlite_game_sessions import SQLiteGameSessionAuthority
from integral_emulator.sqlite_repositories import SQLiteGameSessionRepository


class SQLiteGameSessionAuthorityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.database = AuthorityDatabase(Path(self.temporary.name))
        self.database.initialize()
        with self.database.transaction(write=True) as connection:
            for user_id in ("a", "b"):
                connection.execute(
                    "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, 1, 1)",
                    (user_id, user_id, user_id),
                )
                connection.execute(
                    """
                    INSERT INTO auth_sessions (
                        token_digest, user_id, server_id, created_at_ms,
                        expires_at_ms, last_access_at_ms
                    ) VALUES (?, ?, 'primary', 1, 9999999999999, 1)
                    """,
                    (f"token-{user_id}", user_id),
                )
        self.repository = SQLiteGameSessionRepository(self.database)
        self.primary = SQLiteGameSessionAuthority(self.repository, "primary")
        self.secondary = SQLiteGameSessionAuthority(self.repository, "secondary")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def expiry(minutes: int = 5) -> str:
        return (datetime.now(timezone.utc) + timedelta(minutes=minutes)).isoformat()

    def test_single_lock_renew_and_release_require_full_fence(self) -> None:
        lock = self.primary.acquire_single(
            user_id="a", auth_session_id="token-a", execution_mode="LOCAL_CLIENT",
            lease_expires_at=self.expiry(), expires_at=self.expiry(30),
            save_bindings=[{"save_id": "save-a", "revision": 1, "sha256": "a" * 64}],
        )
        with self.assertRaises(GameSessionFenceError):
            self.primary.renew(
                user_id="a", game_run_id=lock.game_run_id,
                game_session_id=lock.game_session_id,
                auth_session_id="token-a", fencing_token=lock.fencing_token + 1,
                lease_expires_at=self.expiry(),
            )
        renewed = self.primary.renew(
            user_id="a", game_run_id=lock.game_run_id,
            game_session_id=lock.game_session_id,
            auth_session_id="token-a", fencing_token=lock.fencing_token,
            lease_expires_at=self.expiry(),
        )
        self.assertEqual(renewed.game_session_id, lock.game_session_id)
        released = self.primary.release(
            user_id="a", game_session_id=lock.game_session_id,
            auth_session_id="token-a", fencing_token=lock.fencing_token,
        )
        self.assertEqual(released.game_run_id, lock.game_run_id)
        self.assertIsNone(self.primary.active_for_user("a"))

    def test_same_user_two_environments_has_one_winner(self) -> None:
        def acquire(authority):
            try:
                authority.acquire_single(
                    user_id="a", auth_session_id="token-a",
                    execution_mode="LOCAL_CLIENT", lease_expires_at=self.expiry(),
                    expires_at=self.expiry(30),
                )
                return True
            except ValidationError:
                return False

        with ThreadPoolExecutor(max_workers=2) as executor:
            results = list(executor.map(acquire, (self.primary, self.secondary)))
        self.assertEqual(sum(results), 1)

    def test_pair_acquire_rolls_back_both_users_on_conflict(self) -> None:
        self.primary.acquire_single(
            user_id="b", auth_session_id="token-b", execution_mode="LOCAL_CLIENT",
            lease_expires_at=self.expiry(), expires_at=self.expiry(30),
        )
        with self.assertRaises(ValidationError):
            self.primary.acquire_pair(
                game_run_id="link-1", first_user_id="a",
                first_auth_session_id="token-a", first_save_bindings=[],
                second_user_id="b", second_auth_session_id="token-b",
                second_save_bindings=[], lease_expires_at=self.expiry(),
                expires_at=self.expiry(30),
            )
        self.assertIsNone(self.primary.active_for_user("a"))

    def test_save_binding_query_rejects_active_use(self) -> None:
        self.primary.acquire_single(
            user_id="a", auth_session_id="token-a", execution_mode="LOCAL_CLIENT",
            lease_expires_at=self.expiry(), expires_at=self.expiry(30),
            save_bindings=[{"save_id": "save-a", "revision": 1, "sha256": "a" * 64}],
        )
        with self.assertRaisesRegex(ValidationError, "active game"):
            self.primary.require_save_available("save-a")
        self.primary.require_save_available("save-b")

    def test_same_account_pair_uses_one_lock_with_both_saves(self) -> None:
        first, second = self.primary.acquire_pair(
            game_run_id="self-link", first_user_id="a",
            first_auth_session_id="token-a",
            first_save_bindings=[{"save_id": "save-1"}],
            second_user_id="a", second_auth_session_id="token-a",
            second_save_bindings=[{"save_id": "save-2"}],
            lease_expires_at=self.expiry(), expires_at=self.expiry(30),
        )
        self.assertEqual(first, second)
        self.assertEqual(
            {item["save_id"] for item in first.save_bindings}, {"save-1", "save-2"}
        )

    def test_two_media_participants_share_one_game_run(self) -> None:
        first = self.primary.acquire_single(
            user_id="a", auth_session_id="token-a",
            execution_mode="N64_RUNTIME_NOSAVE", lease_expires_at=self.expiry(),
            expires_at=self.expiry(30), game_run_id="media-1",
        )
        second = self.primary.acquire_single(
            user_id="b", auth_session_id="token-b",
            execution_mode="N64_RUNTIME_NOSAVE", lease_expires_at=self.expiry(),
            expires_at=self.expiry(30), game_run_id="media-1",
        )
        self.assertEqual(first.game_run_id, second.game_run_id)
        self.assertNotEqual(first.game_session_id, second.game_session_id)


if __name__ == "__main__":
    unittest.main()
