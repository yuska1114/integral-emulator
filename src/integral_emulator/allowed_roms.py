# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Validated, data-driven ROM catalogs."""

from __future__ import annotations

import hashlib
import json
import logging
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .errors import ValidationError
from .rom_metadata import normalize_rom_header_title, normalize_rom_platform
from .save_contract import normalize_game_type


LOGGER = logging.getLogger(__name__)
DEFAULT_CATALOG_DIRECTORY = Path(__file__).resolve().parents[2] / "config" / "allowed_roms"
MAX_CATALOGS = 32
MAX_ROMS_PER_CATALOG = 1024
MAX_ROM_SIZE = 64 * 1024 * 1024
MAX_TEXT_LENGTH = 160
_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_.-]{0,63}$")
_REGION_PATTERN = re.compile(r"^[A-Z0-9][A-Z0-9_-]{0,15}$")
_HASH_LENGTHS = {"crc32": 8, "md5": 32, "sha1": 40, "sha256": 64}


class AllowedRomCatalogError(ValueError):
    """Raised when an enabled catalog is malformed or ambiguous."""


@dataclass(frozen=True)
class AllowedRom:
    catalog_id: str
    catalog_role: str
    content_id: str
    platform: str
    game_type: str
    display_name: str
    canonical_name: str
    region: str
    size: int
    crc32: str
    md5: str
    sha1: str
    sha256: str | None
    rom_header_title: str | None = None

def _catalog_error(path: Path, message: str) -> AllowedRomCatalogError:
    return AllowedRomCatalogError(f"{path.name}: {message}")


def _required_text(data: dict[str, Any], field: str, path: Path, *, maximum: int = MAX_TEXT_LENGTH) -> str:
    value = data.get(field)
    if not isinstance(value, str) or not value.strip() or len(value) > maximum:
        raise _catalog_error(path, f"{field} must be a non-empty string of at most {maximum} characters")
    return value.strip()


def _identifier(data: dict[str, Any], field: str, path: Path) -> str:
    value = _required_text(data, field, path, maximum=64).lower()
    if not _ID_PATTERN.fullmatch(value):
        raise _catalog_error(path, f"{field} has an invalid identifier")
    return value


def _hash(hashes: dict[str, Any], name: str, path: Path, *, required: bool = True) -> str | None:
    value = hashes.get(name)
    if value is None and not required:
        return None
    if not isinstance(value, str):
        raise _catalog_error(path, f"hashes.{name} is required")
    normalized = value.strip().upper()
    if len(normalized) != _HASH_LENGTHS[name] or any(char not in "0123456789ABCDEF" for char in normalized):
        raise _catalog_error(path, f"hashes.{name} must be {_HASH_LENGTHS[name]} hexadecimal characters")
    return normalized


def _optional_rom_header_title(data: dict[str, Any], path: Path) -> str | None:
    if data.get("rom_header_title") is None:
        return None
    try:
        return normalize_rom_header_title(data.get("rom_header_title"))
    except ValidationError as error:
        raise _catalog_error(path, str(error)) from error


def _load_catalog(path: Path) -> tuple[str, str, list[AllowedRom]]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise _catalog_error(path, f"cannot parse JSON: {error}") from error
    if not isinstance(raw, dict):
        raise _catalog_error(path, "catalog root must be an object")
    if raw.get("schema_version") != 1:
        raise _catalog_error(path, "schema_version must be 1")
    catalog_id = _identifier(raw, "catalog_id", path)
    catalog_role = _required_text(raw, "catalog_role", path, maximum=32).lower()
    if catalog_role not in {"primary", "compatibility", "optional_game_pack"}:
        raise _catalog_error(
            path, "catalog_role must be primary, compatibility, or optional_game_pack"
        )
    raw_roms = raw.get("roms")
    if not isinstance(raw_roms, list):
        raise _catalog_error(path, "roms must be an array")
    if len(raw_roms) > MAX_ROMS_PER_CATALOG:
        raise _catalog_error(path, f"roms exceeds the {MAX_ROMS_PER_CATALOG}-entry limit")

    entries: list[AllowedRom] = []
    for index, item in enumerate(raw_roms):
        item_path = Path(f"{path.name}#roms[{index}]")
        if not isinstance(item, dict):
            raise _catalog_error(item_path, "entry must be an object")
        content_id = _identifier(item, "content_id", item_path)
        try:
            game_type = normalize_game_type(
                _required_text(item, "game_type", item_path, maximum=64)
            )
        except ValueError:
            raise _catalog_error(item_path, "game_type has an invalid identifier")
        region = _required_text(item, "region", item_path, maximum=16).upper()
        if not _REGION_PATTERN.fullmatch(region):
            raise _catalog_error(item_path, "region has an invalid identifier")
        size = item.get("size")
        if isinstance(size, bool) or not isinstance(size, int) or not 0 < size <= MAX_ROM_SIZE:
            raise _catalog_error(item_path, f"size must be an integer from 1 to {MAX_ROM_SIZE}")
        hashes = item.get("hashes")
        if not isinstance(hashes, dict):
            raise _catalog_error(item_path, "hashes must be an object")
        enabled = item.get("enabled", True)
        if not isinstance(enabled, bool):
            raise _catalog_error(item_path, "enabled must be boolean")
        try:
            platform = normalize_rom_platform(_required_text(item, "platform", item_path, maximum=8))
        except ValidationError as error:
            raise _catalog_error(item_path, str(error)) from error
        sha256 = _hash(hashes, "sha256", item_path, required=False)
        sha1 = _hash(hashes, "sha1", item_path, required=False)
        if enabled and sha256 is None and sha1 is None:
            raise _catalog_error(item_path, "at least one of hashes.sha256 or hashes.sha1 is required")
        if not enabled:
            continue
        entries.append(
            AllowedRom(
                catalog_id=catalog_id,
                catalog_role=catalog_role,
                content_id=content_id,
                platform=platform,
                game_type=game_type,
                display_name=_required_text(item, "display_name", item_path),
                canonical_name=_required_text(item, "canonical_name", item_path),
                region=region,
                size=size,
                crc32=_hash(hashes, "crc32", item_path) or "",
                md5=_hash(hashes, "md5", item_path) or "",
                sha1=sha1 or "",
                sha256=sha256,
                rom_header_title=_optional_rom_header_title(item, item_path),
            )
        )
    return catalog_id, catalog_role, entries


def load_allowed_roms(directory: Path | str = DEFAULT_CATALOG_DIRECTORY) -> tuple[AllowedRom, ...]:
    catalog_directory = Path(directory)
    paths = sorted(catalog_directory.glob("*.json"), key=lambda item: item.name)
    if not paths:
        raise AllowedRomCatalogError(f"no ROM catalogs found in {catalog_directory}")
    if len(paths) > MAX_CATALOGS:
        raise AllowedRomCatalogError(f"ROM catalog count exceeds {MAX_CATALOGS}")

    catalog_ids: set[str] = set()
    content_ids: set[str] = set()
    known_hashes: dict[tuple[str, str], str] = {}
    entries: list[AllowedRom] = []
    for path in paths:
        catalog_id, _catalog_role, catalog_entries = _load_catalog(path)
        if catalog_id in catalog_ids:
            raise _catalog_error(path, f"duplicate catalog_id {catalog_id}")
        catalog_ids.add(catalog_id)
        for entry in catalog_entries:
            if entry.content_id in content_ids:
                raise _catalog_error(path, f"duplicate content_id {entry.content_id}")
            content_ids.add(entry.content_id)
            for hash_name in _HASH_LENGTHS:
                value = getattr(entry, hash_name)
                if value is None:
                    continue
                key = (hash_name, value.lower())
                previous = known_hashes.get(key)
                if previous is not None:
                    raise _catalog_error(path, f"duplicate {hash_name} shared by {previous} and {entry.content_id}")
                known_hashes[key] = entry.content_id
            entries.append(entry)
    return tuple(sorted(entries, key=lambda entry: (entry.catalog_id, entry.content_id)))


_ALLOWED_ROMS = load_allowed_roms()
_CATALOG_IDS = tuple(
    sorted(
        json.loads(path.read_text(encoding="utf-8"))["catalog_id"]
        for path in DEFAULT_CATALOG_DIRECTORY.glob("*.json")
    )
)
_CATALOG_DIGEST = hashlib.sha256(
    "\n".join(
        f"{entry.catalog_id}:{entry.content_id}:{entry.sha256 or '-'}:{entry.sha1}"
        for entry in _ALLOWED_ROMS
    ).encode("utf-8")
).hexdigest()
LOGGER.warning(
    "loaded ROM catalogs schema=1 ids=%s entries=%d digest=%s",
    ",".join(_CATALOG_IDS),
    len(_ALLOWED_ROMS),
    _CATALOG_DIGEST,
)


def allowed_roms() -> tuple[AllowedRom, ...]:
    return _ALLOWED_ROMS


def find_allowed_rom(platform: str, region: str, sha256: str, sha1: str | None = None) -> AllowedRom | None:
    normalized_platform = normalize_rom_platform(platform)
    normalized_region = region.strip().upper()
    normalized_sha256 = sha256.strip().lower()
    normalized_sha1 = sha1.strip().lower() if sha1 else None
    for rom in _ALLOWED_ROMS:
        hash_matches = (
            rom.sha256.lower() == normalized_sha256
            if rom.sha256 is not None
            else normalized_sha1 is not None
            and bool(rom.sha1)
            and rom.sha1.lower() == normalized_sha1
        )
        if not hash_matches:
            continue
        if rom.platform != normalized_platform or rom.region != normalized_region:
            raise AllowedRomCatalogError(
                f"ROM hash metadata conflicts with catalog entry {rom.content_id}"
            )
        return rom
    return None
