# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Platform-neutral ROM metadata validation and identity generation."""

from __future__ import annotations

import re
from pathlib import PurePath

from .errors import ValidationError


ROM_PLATFORMS = frozenset({"gb", "n64"})
GB_GBC_ROM_EXTENSIONS = frozenset({".gb", ".gbc"})
N64_ROM_EXTENSIONS = frozenset({".z64", ".n64", ".v64"})
REGION_PATTERN = re.compile(r"^[A-Z0-9][A-Z0-9_-]{0,15}$")


def normalize_rom_platform(platform: str) -> str:
    if not isinstance(platform, str):
        raise ValidationError("platform must be a string")
    normalized = platform.strip().lower()
    if normalized not in ROM_PLATFORMS:
        raise ValidationError("platform must be gb or n64")
    return normalized


def rom_platform_from_filename(filename: str | None) -> str | None:
    suffix = PurePath(filename or "").suffix.lower()
    if suffix in GB_GBC_ROM_EXTENSIONS:
        return "gb"
    if suffix in N64_ROM_EXTENSIONS:
        return "n64"
    return None


def normalize_rom_region(region: str) -> str:
    if not isinstance(region, str):
        raise ValidationError("region must be a string")
    normalized = region.strip().upper()
    if not REGION_PATTERN.fullmatch(normalized):
        raise ValidationError("region must be a 1 to 16 character identifier")
    return normalized


def normalize_rom_header_title(title: str | None) -> str:
    if not isinstance(title, str):
        raise ValidationError("rom_header_title is required")
    trimmed = title.rstrip("\x00 ").strip()
    if not trimmed or len(trimmed) > 20:
        raise ValidationError("rom_header_title must be 1 to 20 characters")
    if any(ord(char) < 0x20 or ord(char) > 0x7E for char in trimmed):
        raise ValidationError("rom_header_title must contain printable ASCII only")
    normalized = " ".join(trimmed.upper().split())
    if not normalized or not any(char.isascii() and char.isalnum() for char in normalized):
        raise ValidationError("rom_header_title must contain an ASCII letter or digit")
    return normalized


def generated_game_type(platform: str, rom_header_title: str) -> str:
    normalized_platform = normalize_rom_platform(platform)
    normalized_title = normalize_rom_header_title(rom_header_title)
    slug = re.sub(r"[^a-z0-9]+", "_", normalized_title.lower()).strip("_")
    if not slug:
        raise ValidationError("rom_header_title cannot produce a game_type")
    return f"{normalized_platform}_{slug}"
