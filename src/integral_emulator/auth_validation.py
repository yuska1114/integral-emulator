# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Input validation shared by the SQLite authentication service."""

from __future__ import annotations

import hashlib
from datetime import timedelta

from .errors import ValidationError


TOKEN_TOUCH_INTERVAL = timedelta(minutes=5)


def validate_username(username: str) -> str:
    display = username.strip()
    if not 3 <= len(display) <= 32:
        raise ValidationError("username must be 3 to 32 characters")
    if not all(char.isalnum() or char in {"_", "-"} for char in display):
        raise ValidationError(
            "username may only contain letters, numbers, underscore, and hyphen"
        )
    return display


def username_comparison_key(username: str) -> str:
    """Return the case-insensitive identity key without changing display spelling."""
    return username.strip().casefold()


def normalize_username(username: str) -> str:
    return username_comparison_key(validate_username(username))


def validate_password(password: str) -> None:
    if not isinstance(password, str):
        raise ValidationError("password must be a string")
    if len(password) < 8:
        raise ValidationError("password must be at least 8 characters")
    if len(password) > 256:
        raise ValidationError("password must be 256 characters or fewer")
    if "\x00" in password:
        raise ValidationError("password must not contain NUL characters")


def normalize_email(email: str) -> str:
    normalized = email.strip().lower()
    if not normalized:
        return ""
    if len(normalized) > 254 or any(char.isspace() for char in normalized):
        raise ValidationError("email is invalid")
    local, separator, domain = normalized.partition("@")
    if not separator or not local or not domain or "." not in domain:
        raise ValidationError("email is invalid")
    return normalized


def normalize_server_id(server_id: str) -> str:
    normalized = server_id.strip().lower()
    if not normalized:
        return "primary"
    if not all(char.isalnum() or char in {"_", "-"} for char in normalized):
        raise ValidationError(
            "server_id may only contain letters, numbers, underscore, and hyphen"
        )
    return normalized


def token_digest(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()
