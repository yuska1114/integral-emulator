# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ROOM data types and limits shared by the SQLite ROOM service."""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any

from .errors import ValidationError

from .gb_runtime_link_modes import GBRuntimeLinkMode


LINK_ROOM_FIRST = 1
LINK_ROOM_LAST = 64
N64_ROOM_FIRST = 65
N64_ROOM_LAST = 128
ROOM_NUMBERS = tuple(range(LINK_ROOM_FIRST, LINK_ROOM_LAST + 1)) + tuple(
    range(N64_ROOM_FIRST, N64_ROOM_LAST + 1)
)
DEFAULT_LINK_ROOM_NUMBERS = tuple(range(1, 17))
DEFAULT_N64_ROOM_NUMBERS = tuple(range(65, 81))
LINK_ROOMS_ENV = "INTEGRAL_EMULATOR_LINK_CABLE_ROOMS"
N64_ROOMS_ENV = "INTEGRAL_EMULATOR_N64_ROOMS"
ROOM_CAPACITY = 2
ROOM_STALE_AFTER_SECONDS = 60
ROOM_CODE_REUSE_COOLDOWN_SECONDS = 30 * 60
ROOM_CODE_GENERATION_ATTEMPTS = 128


def parse_enabled_room_numbers(
    value: str,
    *,
    minimum: int,
    maximum: int,
    setting_name: str,
) -> tuple[int, ...]:
    """Parse a comma-separated list of room numbers and inclusive ranges."""
    if not value.strip():
        return ()
    enabled: set[int] = set()
    for raw_part in value.split(","):
        part = raw_part.strip()
        match = re.fullmatch(r"([0-9]+)(?:\s*-\s*([0-9]+))?", part)
        if match is None:
            raise ValidationError(
                f"{setting_name} must contain comma-separated room numbers or ranges"
            )
        first = int(match.group(1))
        last = int(match.group(2) or first)
        if first > last:
            raise ValidationError(f"{setting_name} contains a descending room range")
        if first < minimum or last > maximum:
            raise ValidationError(
                f"{setting_name} rooms must be between {minimum} and {maximum}"
            )
        numbers = set(range(first, last + 1))
        if enabled.intersection(numbers):
            raise ValidationError(f"{setting_name} contains a duplicate room")
        enabled.update(numbers)
    return tuple(sorted(enabled))


@dataclass(frozen=True)
class Room:
    room_number: int
    users: list[dict[str, Any]]
    chat: list[dict[str, str]]
    link_session_id: str | None = None
    link_mode: str = GBRuntimeLinkMode.BATTLE.value
    game_started: bool = False
    updated_at: str | None = None
    post_game_at: str | None = None
    room_code: str | None = None
    room_code_created_at: str | None = None
    creator_user_id: str | None = None
    creator_username: str | None = None

    @property
    def room_type(self) -> str:
        if N64_ROOM_FIRST <= self.room_number <= N64_ROOM_LAST:
            return "n64"
        return "link_cable"

    def to_dict(self) -> dict[str, Any]:
        return {
            "room_number": self.room_number,
            "users": self.users,
            "chat": self.chat,
            "link_session_id": self.link_session_id,
            "link_mode": self.link_mode,
            "game_started": self.game_started,
            "updated_at": self.updated_at,
            "post_game_at": self.post_game_at,
            "room_type": self.room_type,
            "room_code": self.room_code,
            "room_code_created_at": self.room_code_created_at,
            "creator_user_id": self.creator_user_id,
            "creator_username": self.creator_username,
        }
