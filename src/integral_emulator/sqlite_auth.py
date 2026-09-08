# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SQLite-backed authentication service."""

from __future__ import annotations

from collections.abc import Callable
from datetime import datetime, timedelta, timezone

from .auth_validation import (
    TOKEN_TOUCH_INTERVAL,
    normalize_email,
    normalize_server_id,
    normalize_username,
    token_digest,
    validate_password,
    validate_username,
)
from .errors import AuthenticationError, DuplicateUserError, ValidationError
from .models import SessionToken, User
from .security import hash_password, issue_secret, verify_password
from .sqlite_repositories import RepositoryConflictError, SQLiteAuthRepository


class SQLiteAuthService:
    """Authentication backed exclusively by normalized SQLite rows."""

    def __init__(self, repository: SQLiteAuthRepository, token_ttl_hours: int = 24):
        self.repository = repository
        self.token_ttl = timedelta(hours=token_ttl_hours)
        self.repository.delete_expired_sessions(self._now_ms())

    def register(
        self,
        username: str,
        password: str,
        must_change_password: bool = False,
        email: str = "",
    ) -> User:
        display = validate_username(username)
        normalized = normalize_username(display)
        normalized_email = normalize_email(email)
        validate_password(password)
        timestamp = self._now_ms()
        record = {
            "user_id": issue_secret("user"),
            "username": display,
            "username_normalized": normalized,
            "email": normalized_email or None,
            "password_hash": hash_password(password),
            "status": "active",
            "must_change_password": must_change_password,
            "created_at_ms": timestamp,
            "updated_at_ms": timestamp,
        }
        try:
            self.repository.create_user(record)
        except RepositoryConflictError as error:
            raise DuplicateUserError("username is already registered") from error
        return self._user(record)

    def login(self, username: str, password: str, server_id: str = "primary") -> SessionToken:
        normalized = normalize_username(username)
        normalized_server_id = normalize_server_id(server_id)
        user_data = self.repository.find_user_by_username(normalized)
        if not user_data or not verify_password(password, user_data["password_hash"]):
            raise AuthenticationError("invalid username or password")
        if user_data.get("status") != "active":
            raise AuthenticationError("user is not active")
        now = datetime.now(timezone.utc)
        raw_token = issue_secret("token")
        digest = token_digest(raw_token)
        self.repository.create_session(
            {
                "token_digest": digest,
                "user_id": user_data["user_id"],
                "server_id": normalized_server_id,
                "created_at_ms": self._epoch_ms(now),
                "expires_at_ms": self._epoch_ms(now + self.token_ttl),
                "last_access_at_ms": self._epoch_ms(now),
            }
        )
        return SessionToken(
            token=raw_token,
            user_id=user_data["user_id"],
            created_at=now.isoformat(),
            expires_at=(now + self.token_ttl).isoformat(),
            server_id=normalized_server_id,
            last_access_at=now.isoformat(),
        )

    def change_password_for_user(self, user: User, new_password: str) -> User:
        validate_password(new_password)
        updated = self.repository.update_password(
            user.id,
            hash_password(new_password),
            must_change_password=False,
            updated_at_ms=self._now_ms(),
        )
        if updated is None:
            raise AuthenticationError("token user no longer exists")
        return self._user(updated)

    def change_password(self, token: str, new_password: str) -> User:
        return self.change_password_for_user(self.require_user(token), new_password)

    def reset_password(self, username: str, new_password: str) -> User:
        user, _password, _revoked = self.reset_password_generated(
            username, lambda: new_password
        )
        return user

    def reset_password_generated(
        self,
        username: str,
        password_factory: Callable[[], str],
    ) -> tuple[User, str, int]:
        normalized = normalize_username(username)
        plaintext: list[str] = []

        def password_hash_factory() -> str:
            password = password_factory()
            validate_password(password)
            plaintext.append(password)
            return hash_password(password)

        status, updated, revoked = self.repository.reset_password_if_idle(
            normalized,
            password_hash_factory,
            updated_at_ms=self._now_ms(),
        )
        if status == "not_found":
            raise ValidationError("username not found")
        if status == "in_room":
            raise ValidationError("user is currently participating in a ROOM")
        if status == "game_running":
            raise ValidationError("user is currently running a game")
        if updated is None or len(plaintext) != 1:
            raise RuntimeError("password reset did not produce credentials")
        return self._user(updated), plaintext[0], revoked

    def require_identity(self, token: str) -> tuple[SessionToken, User]:
        digest = token_digest(token)
        identity = self.repository.find_session_and_user(digest)
        if identity is None:
            raise AuthenticationError("invalid token")
        session, user_data = identity
        now_ms = self._now_ms()
        if int(session["expires_at_ms"]) <= now_ms:
            self.repository.delete_session(digest)
            raise AuthenticationError("token expired")
        if user_data.get("status") != "active":
            raise AuthenticationError("user is not active")
        last_access_ms = int(session["last_access_at_ms"])
        if now_ms - last_access_ms >= int(TOKEN_TOUCH_INTERVAL.total_seconds() * 1000):
            self.repository.touch_session(digest, last_access_ms, now_ms)
            session["last_access_at_ms"] = now_ms
        return self._session_token(token, session), self._user(user_data)

    def require_user(self, token: str) -> User:
        _session, user = self.require_identity(token)
        return user

    def require_token(self, token: str) -> SessionToken:
        session, _user = self.require_identity(token)
        return session

    def logout(self, token: str) -> None:
        if not self.repository.delete_session(token_digest(token)):
            raise AuthenticationError("invalid token")

    def logout_user(self, user_id: str) -> None:
        self.repository.delete_sessions_for_user(user_id)

    def list_users(self) -> list[User]:
        return [self._user(record) for record in self.repository.list_users()]

    @classmethod
    def _user(cls, record: dict) -> User:
        return User(
            id=str(record["user_id"]),
            username=str(record["username"]),
            password_hash=str(record["password_hash"]),
            created_at=cls._iso(int(record["created_at_ms"])),
            updated_at=cls._iso(int(record["updated_at_ms"])),
            email=str(record.get("email") or ""),
            status=str(record.get("status") or "active"),
            must_change_password=bool(record.get("must_change_password")),
        )

    @classmethod
    def _session_token(cls, raw_token: str, record: dict) -> SessionToken:
        return SessionToken(
            token=raw_token,
            user_id=str(record["user_id"]),
            server_id=str(record["server_id"]),
            created_at=cls._iso(int(record["created_at_ms"])),
            expires_at=cls._iso(int(record["expires_at_ms"])),
            last_access_at=cls._iso(int(record["last_access_at_ms"])),
        )

    @staticmethod
    def _epoch_ms(value: datetime) -> int:
        return int(value.timestamp() * 1000)

    @classmethod
    def _now_ms(cls) -> int:
        return cls._epoch_ms(datetime.now(timezone.utc))

    @staticmethod
    def _iso(value: int) -> str:
        return datetime.fromtimestamp(value / 1000, tz=timezone.utc).isoformat()
