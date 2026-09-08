# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fixed authoritative ROOM and game-session lifetime policy."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
import os


@dataclass(frozen=True)
class RoomSessionPolicy:
    lease_seconds: int = 45
    finalize_seconds: int = 300
    sweeper_interval_seconds: int = 5
    waiting_room_idle_seconds: int = 10 * 60
    link_seconds: int = 60 * 60
    n64_runtime_seconds: int = 120 * 60

    @classmethod
    def from_environment(cls) -> "RoomSessionPolicy":
        def integer(name: str, default: int) -> int:
            raw = os.environ.get(name)
            if raw is None:
                return default
            value = int(raw)
            if value <= 0:
                raise ValueError(f"{name} must be a positive integer")
            return value

        policy = cls(
            lease_seconds=integer("INTEGRAL_EMULATOR_ROOM_LEASE_SECONDS", 45),
            sweeper_interval_seconds=integer("INTEGRAL_EMULATOR_ROOM_SWEEPER_SECONDS", 5),
        )
        return policy

    @staticmethod
    def deadline(base: datetime, seconds: int) -> str:
        if base.tzinfo is None:
            base = base.replace(tzinfo=timezone.utc)
        return (base + timedelta(seconds=seconds)).isoformat()
