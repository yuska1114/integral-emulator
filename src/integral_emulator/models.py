# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Serializable domain models."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


@dataclass(frozen=True)
class User:
    id: str
    username: str
    password_hash: str
    created_at: str
    updated_at: str
    email: str = ""
    status: str = "active"
    must_change_password: bool = False

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "User":
        normalized = data.copy()
        normalized.setdefault("email", "")
        normalized.setdefault("must_change_password", False)
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class SessionToken:
    token: str
    user_id: str
    created_at: str
    expires_at: str
    server_id: str
    last_access_at: str | None = None

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "SessionToken":
        normalized = data.copy()
        normalized.setdefault("last_access_at", normalized.get("created_at"))
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class SaveRecord:
    id: str
    user_id: str
    game_type: str
    revision: int
    sha256: str
    storage_path: str
    created_at: str
    updated_at: str
    lock_owner: str | None = None
    locked_at: str | None = None
    lock_expires_at: str | None = None

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "SaveRecord":
        return cls(**data)

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__.copy()
