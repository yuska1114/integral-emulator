# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SAV validation limits and commit authority."""

from __future__ import annotations

from dataclasses import dataclass
import re


GAME_TYPE = re.compile(r"[a-z0-9][a-z0-9_-]{0,63}\Z")
MAX_SAVE_BYTES = 128 * 1024 + 48
REQUEST_ID = re.compile(r"[A-Za-z0-9._:-]{1,192}\Z")


def normalize_game_type(game_type: str) -> str:
    if not isinstance(game_type, str):
        raise ValueError("game_type must be a string")
    normalized = game_type.strip().lower()
    if not GAME_TYPE.fullmatch(normalized):
        raise ValueError(
            "game_type must be a 1 to 64 character identifier using letters, digits, '_' or '-'"
        )
    return normalized


@dataclass(frozen=True)
class SaveUploadAuthority:
    mode: str
    game_session_id: str = ""
    game_run_id: str = ""
    fencing_token: int = 0
    mobile_session_id: str | None = None
    lock_owner: str | None = None
    auth_session_id: str | None = None

    @classmethod
    def unlocked(
        cls,
        mode: str = "UNLOCKED",
        *,
        game_session_id: str = "",
        fencing_token: int = 0,
        auth_session_id: str | None = None,
        game_run_id: str = "",
    ) -> "SaveUploadAuthority":
        return cls(
            mode=mode,
            game_session_id=game_session_id,
            game_run_id=game_run_id,
            fencing_token=fencing_token,
            auth_session_id=auth_session_id,
        )
