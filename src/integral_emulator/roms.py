# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ROM registration data and input validation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .errors import ValidationError
from .rom_metadata import (
    generated_game_type,
    normalize_rom_header_title,
    normalize_rom_platform,
    normalize_rom_region,
    rom_platform_from_filename,
)


@dataclass(frozen=True)
class RomRegistration:
    id: str
    user_id: str
    sha256: str
    title: str
    game_type: str
    platform: str
    region: str
    created_at: str
    sha1: str | None = None
    verified_name: str | None = None
    rom_header_title: str | None = None

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RomRegistration":
        normalized = data.copy()
        if not normalized.get("rom_header_title"):
            raise ValidationError("registered ROM header title is missing")
        platform = rom_platform_from_filename(normalized.get("title"))
        if platform is None:
            raise ValidationError("stored ROM filename has no supported platform")
        normalized["platform"] = platform
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__.copy()


def validate_registration_metadata(
    sha256: str,
    title: str,
    platform: str,
    region: str,
    sha1: str | None = None,
    rom_header_title: str | None = None,
    *,
    allow_unlisted_roms: bool,
) -> dict[str, str | None]:
    normalized_hash = _validate_sha256(sha256)
    normalized_sha1 = _validate_sha1(sha1)
    normalized_platform = _validate_platform_for_title(platform, title)
    normalized_region = normalize_rom_region(region)
    normalized_title = _validate_title(title)
    normalized_header_title = normalize_rom_header_title(rom_header_title)
    allowed = find_allowed_registration(
        normalized_platform,
        normalized_region,
        normalized_hash,
        normalized_sha1,
        allow_unlisted_roms=allow_unlisted_roms,
    )
    trusted_header = allowed.rom_header_title if allowed else None
    if trusted_header and normalized_header_title and normalized_header_title != trusted_header:
        raise ValidationError("ROM header title does not match the enabled ROM catalog")
    normalized_header_title = trusted_header or normalized_header_title
    game_type = (
        allowed.game_type
        if allowed
        else generated_game_type(normalized_platform, normalized_header_title)
    )
    return {
        "sha256": normalized_hash,
        "sha1": normalized_sha1,
        "game_type": game_type,
        "platform": normalized_platform,
        "region": normalized_region,
        "title": normalized_title,
        "verified_name": allowed.canonical_name if allowed else normalized_title,
        "rom_header_title": normalized_header_title,
    }


def find_allowed_registration(
    platform: str,
    region: str,
    sha256: str,
    sha1: str | None,
    *,
    allow_unlisted_roms: bool,
):
    try:
        from .allowed_roms import find_allowed_rom
    except (ImportError, ValueError):
        if allow_unlisted_roms:
            return None
        raise
    allowed = find_allowed_rom(platform, region, sha256, sha1)
    if allowed is None and not allow_unlisted_roms:
        raise ValidationError("ROM is not in the enabled ROM catalog")
    return allowed


def _validate_sha256(sha256: str) -> str:
    normalized = sha256.strip().lower()
    if len(normalized) != 64 or any(char not in "0123456789abcdef" for char in normalized):
        raise ValidationError("sha256 must be 64 lowercase or uppercase hex characters")
    return normalized


def _validate_sha1(sha1: str | None) -> str | None:
    if sha1 is None:
        return None
    normalized = sha1.strip().lower()
    if not normalized:
        return None
    if len(normalized) != 40 or any(char not in "0123456789abcdef" for char in normalized):
        raise ValidationError("sha1 must be 40 lowercase or uppercase hex characters")
    return normalized


def _validate_title(title: str) -> str:
    normalized = title.strip()
    if not 1 <= len(normalized) <= 80:
        raise ValidationError("title must be 1 to 80 characters")
    return normalized


def _validate_platform_for_title(platform: str, title: str) -> str:
    normalized = normalize_rom_platform(platform)
    expected = rom_platform_from_filename(title)
    if expected is None:
        raise ValidationError("title must use a supported ROM extension")
    if expected != normalized:
        raise ValidationError("platform does not match the ROM filename extension")
    return normalized
