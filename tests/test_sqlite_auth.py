from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
from pathlib import Path
import tempfile
import unittest

from integral_emulator.database import AuthorityDatabase
from integral_emulator.errors import AuthenticationError, DuplicateUserError
from integral_emulator.security import verify_password
from integral_emulator.sqlite_auth import SQLiteAuthService
from integral_emulator.sqlite_repositories import SQLiteAuthRepository


class SQLiteAuthServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.database = AuthorityDatabase(Path(self.temporary.name))
        self.database.initialize()
        self.repository = SQLiteAuthRepository(self.database)
        self.auth = SQLiteAuthService(self.repository)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_register_login_and_identity_store_only_token_digest(self) -> None:
        user = self.auth.register(
            "Player_One", "correct horse battery staple", email="Shared@Example.com"
        )
        token = self.auth.login("player_one", "correct horse battery staple", "secondary")
        session, authenticated = self.auth.require_identity(token.token)

        self.assertEqual(authenticated.id, user.id)
        self.assertEqual(user.username, "Player_One")
        self.assertEqual(authenticated.username, "Player_One")
        self.assertEqual(session.server_id, "secondary")
        with self.database.connect() as connection:
            stored = connection.execute(
                "SELECT token_digest FROM auth_sessions WHERE token_digest = ?",
                (hashlib.sha256(token.token.encode()).hexdigest(),),
            ).fetchone()[0]
        self.assertEqual(stored, hashlib.sha256(token.token.encode()).hexdigest())
        self.assertNotEqual(stored, token.token)
        self.assertEqual(
            self.auth.login("PLAYER_ONE", "correct horse battery staple").user_id,
            user.id,
        )

    def test_duplicate_username_is_rejected_but_duplicate_email_is_allowed(self) -> None:
        self.auth.register("first-user", "password123", email="shared@example.com")
        self.auth.register("second-user", "password123", email="shared@example.com")
        with self.assertRaises(DuplicateUserError):
            self.auth.register("FIRST-USER", "password123")

    def test_normalized_username_is_stored_separately_and_not_returned(self) -> None:
        user = self.auth.register("  MixedCase_01  ", "password123")

        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT username, username_normalized FROM users WHERE user_id = ?",
                (user.id,),
            ).fetchone()

        self.assertEqual(tuple(row), ("MixedCase_01", "mixedcase_01"))

    def test_change_password_clears_initial_password_flag(self) -> None:
        self.auth.register(
            "initial-user", "password123", must_change_password=True
        )
        token = self.auth.login("initial-user", "password123")

        updated = self.auth.change_password(token.token, "new-password123")

        self.assertFalse(updated.must_change_password)
        self.assertTrue(verify_password("new-password123", updated.password_hash))
        self.assertEqual(self.auth.require_user(token.token).id, updated.id)

    def test_reset_password_revokes_every_session_for_user(self) -> None:
        self.auth.register("reset-user", "password123")
        first = self.auth.login("reset-user", "password123")
        second = self.auth.login("reset-user", "password123", "secondary")

        updated = self.auth.reset_password("reset-user", "replacement123")

        self.assertTrue(updated.must_change_password)
        for token in (first.token, second.token):
            with self.assertRaisesRegex(AuthenticationError, "invalid token"):
                self.auth.require_user(token)
        self.assertEqual(
            self.auth.login("reset-user", "replacement123").user_id,
            updated.id,
        )

    def test_expired_token_is_deleted_and_reported_as_expired(self) -> None:
        self.auth.register("expired-user", "password123")
        token = self.auth.login("expired-user", "password123")
        digest = hashlib.sha256(token.token.encode()).hexdigest()
        expired_ms = int(
            (datetime.now(timezone.utc) - timedelta(seconds=1)).timestamp() * 1000
        )
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "UPDATE auth_sessions SET expires_at_ms = ? WHERE token_digest = ?",
                (expired_ms, digest),
            )

        with self.assertRaisesRegex(AuthenticationError, "token expired"):
            self.auth.require_user(token.token)
        with self.database.connect() as connection:
            revoked = connection.execute(
                "SELECT revoked_at_ms FROM auth_sessions"
            ).fetchone()[0]
        self.assertIsNotNone(revoked)

    def test_logout_is_fail_closed(self) -> None:
        self.auth.register("logout-user", "password123")
        token = self.auth.login("logout-user", "password123")
        self.auth.logout(token.token)
        with self.assertRaisesRegex(AuthenticationError, "invalid token"):
            self.auth.logout(token.token)


if __name__ == "__main__":
    unittest.main()
