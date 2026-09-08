# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared administrator account issuance without plaintext persistence."""

from __future__ import annotations

from dataclasses import dataclass
import secrets
import string

from .models import User
from .sqlite_auth import SQLiteAuthService


INITIAL_PASSWORD_LENGTH = 12
INITIAL_PASSWORD_ALPHABET = string.ascii_lowercase + string.digits


@dataclass(frozen=True)
class IssuedUser:
    user: User
    initial_password: str


@dataclass(frozen=True)
class ResetUserPassword:
    user: User
    temporary_password: str
    revoked_session_count: int


def generate_initial_password() -> str:
    return "".join(
        secrets.choice(INITIAL_PASSWORD_ALPHABET)
        for _ in range(INITIAL_PASSWORD_LENGTH)
    )


def issue_user(
    auth: SQLiteAuthService,
    username: str,
    *,
    email: str = "",
) -> IssuedUser:
    password = generate_initial_password()
    user = auth.register(
        username,
        password,
        must_change_password=True,
        email=email,
    )
    return IssuedUser(user=user, initial_password=password)


def reset_user_password(
    auth: SQLiteAuthService,
    username: str,
) -> ResetUserPassword:
    user, password, revoked = auth.reset_password_generated(
        username, generate_initial_password
    )
    return ResetUserPassword(
        user=user,
        temporary_password=password,
        revoked_session_count=revoked,
    )
