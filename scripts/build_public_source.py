#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and verify the reviewed public-source archive.

The file-level manifest is the sole allowlist.  Formal exports additionally
require a clean Git tree and completed legal/provenance decision gates.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import unicodedata
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from public_source_integrity import (
    INTEGRITY_SCOPE,
    IntegrityError,
    verify_archive as verify_public_archive,
)


PUBLIC_CATEGORIES = {
    "PUBLIC_REQUIRED",
    "PUBLIC_OPTIONAL_DOC",
    "THIRD_PARTY_REQUIRED",
}
OPTIONAL_GAME_PACK_CATEGORY = "PUBLIC_OPTIONAL_GAME_PACK"
KNOWN_CATEGORIES = PUBLIC_CATEGORIES | {
    "GENERATED_FROM_PUBLIC_SOURCE",
    "INTERNAL_ONLY",
    "FORBIDDEN_PAYLOAD",
    OPTIONAL_GAME_PACK_CATEGORY,
}
FORBIDDEN_SUFFIXES = {
    ".a",
    ".car",
    ".core",
    ".db",
    ".dmg",
    ".dll",
    ".dylib",
    ".exe",
    ".gb",
    ".gbc",
    ".gz",
    ".ips",
    ".log",
    ".n64",
    ".o",
    ".rom",
    ".rtc",
    ".sav",
    ".so",
    ".sqlite",
    ".trace",
    ".xz",
    ".v64",
    ".z64",
    ".zip",
}
ASSET_SUFFIXES = {".bmp", ".gif", ".icns", ".ico", ".jpeg", ".jpg", ".otf", ".png", ".ttf", ".webp", ".woff", ".woff2"}
EXECUTABLE_MAGICS = (
    b"\x7fELF",
    b"MZ",
    b"!<arch>\n",
    b"\xca\xfe\xba\xbe",
    b"\xcf\xfa\xed\xfe",
    b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf",
    b"\xfe\xed\xfa\xce",
    b"PK\x03\x04",
)
SECRET_PATTERNS = (
    re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    re.compile(rb"AKIA[0-9A-Z]{16}"),
    re.compile(rb"gh[opusr]_[A-Za-z0-9]{30,}"),
    re.compile(rb"Bearer[ \t]+[A-Za-z0-9._~+/=-]{24,}", re.IGNORECASE),
)
GENERATED_MANIFEST_NAME = "PUBLIC_SOURCE_MANIFEST.json"
EXPORTER_RELATIVE_PATH = "scripts/build_public_source.py"
DEFAULT_CONFIG = "config/public_source_manifest.json"
DEFAULT_CANDIDATE_OUTPUT = "dist/source/INTEGRAL_EMULATOR_PUBLIC_SOURCE_CANDIDATE.zip"
DEFAULT_FORMAL_OUTPUT = "dist/source/INTEGRAL_EMULATOR_PUBLIC_SOURCE.zip"
PUBLIC_GITIGNORE_SOURCE = "config/public_gitignore"
RELEVANT_UNTRACKED_ROOTS = {
    "assets",
    "c_client",
    "config",
    "deploy",
    "doc",
    "docs",
    "ios",
    "runtimes",
    "scripts",
    "src",
    "test_fixtures",
    "tests",
}
IRRELEVANT_UNTRACKED_SEGMENTS = {
    ".cache",
    "__pycache__",
    "artifacts",
    "build",
    "build_exp",
    "dist",
    "export",
    "logs",
    "release",
    "runtime",
    "test_evidence",
}


class ExportError(ValueError):
    """A public-source policy or validation failure."""


def _git(root: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", *args], cwd=root, stderr=subprocess.DEVNULL)


def _git_paths(root: Path) -> set[str]:
    raw = _git(root, "ls-files", "-z", "--cached")
    return {
        value
        for item in raw.split(b"\0")
        if item
        for value in (item.decode("utf-8"),)
        if (root / value).is_file()
    }


def _relevant_untracked_paths(root: Path) -> set[str]:
    raw = _git(root, "ls-files", "-z", "--others", "--exclude-standard")
    relevant: set[str] = set()
    for item in raw.split(b"\0"):
        if not item:
            continue
        value = item.decode("utf-8")
        unchecked = PurePosixPath(value)
        if not unchecked.parts or unchecked.parts[0] not in RELEVANT_UNTRACKED_ROOTS:
            continue
        path = _safe_path(value)
        if path.name == ".DS_Store":
            continue
        if any(
            part in IRRELEVANT_UNTRACKED_SEGMENTS
            or part == "xcuserdata"
            or part.startswith(("build-", "build_"))
            for part in path.parts
        ):
            continue
        relevant.add(value)
    return relevant


def _git_modes(root: Path) -> dict[str, str]:
    raw = _git(root, "ls-files", "-s", "-z")
    result: dict[str, str] = {}
    for item in raw.split(b"\0"):
        if not item:
            continue
        metadata, path = item.split(b"\t", 1)
        result[path.decode("utf-8")] = metadata.split(b" ", 1)[0].decode("ascii")
    return result


def _git_eol_attributes(root: Path, paths: list[str]) -> dict[str, str]:
    if not paths:
        return {}
    output = subprocess.check_output(
        ["git", "check-attr", "-z", "--stdin", "eol"],
        cwd=root,
        input=b"\0".join(path.encode("utf-8") for path in paths) + b"\0",
        stderr=subprocess.DEVNULL,
    )
    fields = output.split(b"\0")
    if fields and fields[-1] == b"":
        fields.pop()
    if len(fields) % 3:
        raise ExportError("cannot read Git end-of-line attributes")
    result: dict[str, str] = {}
    for index in range(0, len(fields), 3):
        path = fields[index].decode("utf-8")
        attribute = fields[index + 1].decode("ascii")
        value = fields[index + 2].decode("ascii")
        if attribute != "eol":
            raise ExportError("unexpected Git attribute response")
        if value in {"lf", "crlf"}:
            result[path] = value
    return result


def _apply_checkout_eol(data: bytes, eol: str | None) -> bytes:
    if eol not in {"lf", "crlf"}:
        return data
    normalized = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return normalized if eol == "lf" else normalized.replace(b"\n", b"\r\n")


def _source_identity(root: Path) -> tuple[str, int, bool]:
    commit = _git(root, "rev-parse", "HEAD").decode("ascii").strip()
    epoch = int(_git(root, "show", "-s", "--format=%ct", "HEAD").decode("ascii").strip())
    dirty = bool(_git(root, "status", "--porcelain=v1").strip())
    override = os.environ.get("SOURCE_DATE_EPOCH")
    if override is not None:
        try:
            epoch = int(override)
        except ValueError as error:
            raise ExportError("SOURCE_DATE_EPOCH must be an integer") from error
    if epoch < 0:
        raise ExportError("SOURCE_DATE_EPOCH must not be negative")
    return commit, epoch, dirty


def _safe_path(value: str) -> PurePosixPath:
    if not value or "\x00" in value or "\\" in value:
        raise ExportError(f"unsafe manifest path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or value != path.as_posix() or any(part in {"", ".", ".."} for part in path.parts):
        raise ExportError(f"unsafe manifest path: {value!r}")
    return path


def load_policy(path: Path) -> dict[str, Any]:
    try:
        policy = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ExportError(f"cannot read public-source manifest: {error}") from error
    if policy.get("format") != 2 or not isinstance(policy.get("files"), list):
        raise ExportError("unsupported public-source manifest format")
    if not isinstance(policy.get("decision_gates"), dict):
        raise ExportError("manifest decision_gates must be an object")
    if policy.get("game_pack_release_policy") not in {
        "EXCLUDED_INITIAL_PUBLIC_RELEASE",
        "APPROVED_FOR_PUBLIC_RELEASE",
    }:
        raise ExportError("manifest game_pack_release_policy is invalid")
    return policy


def validate_asset_provenance(policy: dict[str, Any], entries: dict[str, str]) -> dict[str, dict[str, Any]]:
    records = policy.get("asset_provenance")
    if not isinstance(records, list):
        raise ExportError("manifest asset_provenance must be a list")
    result: dict[str, dict[str, Any]] = {}
    required_keys = {
        "path",
        "component",
        "license_path",
        "copyright_source",
        "sha256",
        "provenance_status",
        "source_files",
        "build_recipe",
    }
    for record in records:
        if not isinstance(record, dict) or set(record) != required_keys:
            raise ExportError("each asset provenance record has an invalid field set")
        path = record["path"]
        if not isinstance(path, str):
            raise ExportError("asset provenance path must be a string")
        _safe_path(path)
        if path in result:
            raise ExportError(f"duplicate asset provenance path: {path}")
        if entries.get(path) not in PUBLIC_CATEGORIES:
            raise ExportError(f"asset provenance path is not selected for public source: {path}")
        for key in ("component", "copyright_source", "provenance_status"):
            if not isinstance(record[key], str) or not record[key].strip():
                raise ExportError(f"asset provenance {key} is missing for {path}")
        digest = record["sha256"]
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ExportError(f"asset provenance sha256 is invalid for {path}")
        license_path = record["license_path"]
        if not isinstance(license_path, str) or entries.get(license_path) not in PUBLIC_CATEGORIES:
            raise ExportError(f"asset license path is not public: {path}")
        source_files = record["source_files"]
        if not isinstance(source_files, list) or any(
            not isinstance(value, str) or entries.get(value) not in PUBLIC_CATEGORIES
            for value in source_files
        ):
            raise ExportError(f"asset source closure is not public: {path}")
        build_recipe = record["build_recipe"]
        if build_recipe is not None and (
            not isinstance(build_recipe, str) or entries.get(build_recipe) not in PUBLIC_CATEGORIES
        ):
            raise ExportError(f"asset build recipe is not public: {path}")
        result[path] = record
    return result


def validate_policy(policy: dict[str, Any]) -> dict[str, str]:
    entries: dict[str, str] = {}
    normalized: dict[str, str] = {}
    folded: dict[str, str] = {}
    for entry in policy["files"]:
        if not isinstance(entry, dict) or set(entry) != {"path", "category"}:
            raise ExportError("each manifest file entry must contain only path and category")
        value = entry["path"]
        category = entry["category"]
        if not isinstance(value, str) or category not in KNOWN_CATEGORIES:
            raise ExportError(f"invalid manifest file entry: {entry!r}")
        _safe_path(value)
        if value in entries:
            raise ExportError(f"duplicate manifest path: {value}")
        nfc = unicodedata.normalize("NFC", value)
        case_key = nfc.casefold()
        if nfc in normalized:
            raise ExportError(f"Unicode-normalization path collision: {normalized[nfc]} / {value}")
        if case_key in folded:
            raise ExportError(f"case-insensitive path collision: {folded[case_key]} / {value}")
        entries[value] = category
        normalized[nfc] = value
        folded[case_key] = value
    if GENERATED_MANIFEST_NAME in entries:
        raise ExportError(f"{GENERATED_MANIFEST_NAME} is generated and must not be in the file allowlist")
    return entries


def _validate_data(path: str, data: bytes, asset_provenance: dict[str, dict[str, Any]]) -> None:
    suffix = PurePosixPath(path).suffix.lower()
    if suffix in FORBIDDEN_SUFFIXES:
        raise ExportError(f"forbidden payload suffix selected: {path}")
    if any(data.startswith(magic) for magic in EXECUTABLE_MAGICS):
        raise ExportError(f"compiled/archive payload magic selected: {path}")
    if suffix in ASSET_SUFFIXES and path not in asset_provenance:
        raise ExportError(f"asset lacks explicit provenance allowlist entry: {path}")
    if path in asset_provenance:
        actual_hash = hashlib.sha256(data).hexdigest()
        if actual_hash != asset_provenance[path]["sha256"]:
            raise ExportError(f"asset provenance hash mismatch: {path}")
    if b"\x00" in data and path not in asset_provenance:
        utf16_source = data.startswith((b"\xff\xfe", b"\xfe\xff"))
        if utf16_source:
            try:
                data.decode("utf-16")
            except UnicodeDecodeError as error:
                raise ExportError(f"invalid UTF-16 source selected: {path}") from error
        else:
            raise ExportError(f"undeclared binary payload selected: {path}")
    for pattern in SECRET_PATTERNS:
        if pattern.search(data):
            raise ExportError(f"secret-like content selected: {path}")


def _zip_info(path: str, epoch: int, executable: bool) -> zipfile.ZipInfo:
    # ZIP timestamps start in 1980 and have two-second precision.
    safe_epoch = max(epoch, 315532800)
    timestamp = datetime.fromtimestamp(safe_epoch, timezone.utc).replace(tzinfo=None)
    timestamp = timestamp.replace(second=timestamp.second - timestamp.second % 2, microsecond=0)
    info = zipfile.ZipInfo(path, date_time=timestamp.timetuple()[:6])
    info.create_system = 3
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (0o100755 if executable else 0o100644) << 16
    return info


def _canonical_json(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def build_archive(root: Path, policy_path: Path, output: Path, *, candidate: bool,
                  include_game_packs: bool = False) -> dict[str, Any]:
    policy = load_policy(policy_path)
    entries = validate_policy(policy)
    asset_provenance = validate_asset_provenance(policy, entries)
    tracked = _git_paths(root)
    relevant_untracked = _relevant_untracked_paths(root)
    modes = _git_modes(root)
    missing_classifications = sorted(tracked - set(entries))
    if missing_classifications:
        preview = ", ".join(missing_classifications[:5])
        raise ExportError(f"tracked files missing from file-level manifest ({len(missing_classifications)}): {preview}")
    missing_relevant_untracked = sorted(relevant_untracked - set(entries))
    if missing_relevant_untracked:
        preview = ", ".join(missing_relevant_untracked[:5])
        raise ExportError(
            "relevant untracked files missing from file-level manifest "
            f"({len(missing_relevant_untracked)}): {preview}"
        )
    if not candidate:
        extra = sorted(set(entries) - tracked)
        if extra:
            raise ExportError(f"formal export manifest contains untracked paths: {', '.join(extra[:5])}")

    commit, epoch, dirty = _source_identity(root)
    pending = sorted(key for key, value in policy["decision_gates"].items() if value != "APPROVED")
    if not candidate and (not policy.get("release_ready") or pending):
        raise ExportError(f"formal export blocked by pending decision gates: {', '.join(pending) or 'release_ready'}")
    if (
        not candidate
        and include_game_packs
        and policy["game_pack_release_policy"] != "APPROVED_FOR_PUBLIC_RELEASE"
    ):
        raise ExportError("formal export of optional game packs is not approved")
    if not candidate and dirty:
        raise ExportError("formal export requires tracked_dirty=false and no untracked files")

    selected_categories = set(PUBLIC_CATEGORIES)
    if include_game_packs:
        selected_categories.add(OPTIONAL_GAME_PACK_CATEGORY)
    selected = sorted(path for path, category in entries.items() if category in selected_categories)
    if not selected:
        raise ExportError("public-source manifest selects no files")
    eol_attributes = _git_eol_attributes(root, selected)
    files: list[tuple[str, bytes, bool]] = []
    for relative in selected:
        source = root / relative
        try:
            metadata = source.lstat()
        except OSError as error:
            raise ExportError(f"selected file is unavailable: {relative}: {error}") from error
        if stat.S_ISLNK(metadata.st_mode):
            raise ExportError(f"symlinks are not allowed in public source: {relative}")
        if not stat.S_ISREG(metadata.st_mode):
            raise ExportError(f"selected path is not a regular file: {relative}")
        if modes.get(relative) == "120000":
            raise ExportError(f"Git symlinks are not allowed in public source: {relative}")
        data = _apply_checkout_eol(source.read_bytes(), eol_attributes.get(relative))
        _validate_data(relative, data, asset_provenance)
        executable = modes.get(relative) == "100755" or (relative not in modes and bool(metadata.st_mode & 0o111))
        files.append((relative, data, executable))

    public_gitignore = root / PUBLIC_GITIGNORE_SOURCE
    if not public_gitignore.is_file():
        raise ExportError(f"public .gitignore source is unavailable: {PUBLIC_GITIGNORE_SOURCE}")
    if any(relative == ".gitignore" for relative, _, _ in files):
        raise ExportError("public .gitignore must be generated from its separate source")
    public_gitignore_data = public_gitignore.read_bytes()
    if not public_gitignore_data.endswith(b"\n"):
        raise ExportError("public .gitignore source must end with a newline")
    files.append((".gitignore", public_gitignore_data, False))
    files.sort(key=lambda item: item[0])

    file_hashes = {path: hashlib.sha256(data).hexdigest() for path, data, _ in files}
    file_modes = {path: "100755" if executable else "100644" for path, _, executable in files}
    content_digest = hashlib.sha256(_canonical_json(file_hashes)).hexdigest()
    public_asset_provenance = sorted(
        (record for record in policy["asset_provenance"] if record["path"] in file_hashes),
        key=lambda record: record["path"],
    )
    asset_provenance_digest = hashlib.sha256(_canonical_json(public_asset_provenance)).hexdigest()
    policy_digest = hashlib.sha256(_canonical_json(policy)).hexdigest()
    exporter_digest = file_hashes.get(EXPORTER_RELATIVE_PATH)
    if exporter_digest is None:
        raise ExportError(f"public exporter is absent from the selected source: {EXPORTER_RELATIVE_PATH}")
    generated = {
        "format": 3,
        "candidate": candidate,
        "included_game_packs": include_game_packs,
        "game_pack_release_policy": policy["game_pack_release_policy"],
        "release_status": "NOT LICENSED FOR FORMAL RELEASE" if candidate else "FORMAL",
        "source_commit": commit,
        "source_date_epoch": epoch,
        "tracked_dirty": dirty,
        "decision_gates": policy["decision_gates"],
        "relevant_untracked_count": len(relevant_untracked),
        "relevant_untracked_unclassified": 0,
        "file_count": len(files),
        "content_manifest_sha256": content_digest,
        "asset_provenance": public_asset_provenance,
        "asset_provenance_sha256": asset_provenance_digest,
        "policy_sha256": policy_digest,
        "exporter_sha256": exporter_digest,
        "files": file_hashes,
        "file_modes": file_modes,
        "integrity_scope": INTEGRITY_SCOPE,
    }
    generated_bytes = _canonical_json(generated)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for relative, data, executable in files:
                archive.writestr(_zip_info(relative, epoch, executable), data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
            archive.writestr(_zip_info(GENERATED_MANIFEST_NAME, epoch, False), generated_bytes, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        verify_archive(temporary, policy)
        temporary.replace(output)
    finally:
        if temporary.exists():
            temporary.unlink()
    generated["archive_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    return generated


def verify_archive(archive_path: Path, policy: dict[str, Any] | None = None) -> dict[str, Any]:
    """Verify public integrity, then optionally enforce the private release policy."""
    generated = verify_public_archive(archive_path)
    if policy is None:
        return generated

    entries = validate_policy(policy)
    asset_provenance = validate_asset_provenance(policy, entries)
    selected_categories = set(PUBLIC_CATEGORIES)
    if generated["included_game_packs"]:
        selected_categories.add(OPTIONAL_GAME_PACK_CATEGORY)
    expected = {path for path, category in entries.items() if category in selected_categories}
    expected.add(".gitignore")
    if set(generated["files"]) != expected:
        raise ExportError("archive file set does not match the private reviewed policy")
    if generated["decision_gates"] != policy["decision_gates"]:
        raise ExportError("archive decision gates differ from the private reviewed policy")
    if generated["game_pack_release_policy"] != policy["game_pack_release_policy"]:
        raise ExportError("archive game-pack policy differs from the private reviewed policy")
    if generated["policy_sha256"] != hashlib.sha256(_canonical_json(policy)).hexdigest():
        raise ExportError("archive private policy digest is inconsistent")
    expected_public_provenance = sorted(
        (record for record in policy["asset_provenance"] if record["path"] in generated["files"]),
        key=lambda record: record["path"],
    )
    if generated["asset_provenance"] != expected_public_provenance:
        raise ExportError("archive asset provenance differs from the private reviewed policy")
    with zipfile.ZipFile(archive_path, "r") as archive:
        for relative in generated["files"]:
            if relative != ".gitignore":
                _validate_data(relative, archive.read(relative), asset_provenance)
    if generated["candidate"] is False and (
        generated["tracked_dirty"]
        or not policy.get("release_ready")
        or any(value != "APPROVED" for value in policy["decision_gates"].values())
        or (
            generated["included_game_packs"]
            and policy["game_pack_release_policy"] != "APPROVED_FOR_PUBLIC_RELEASE"
        )
    ):
        raise ExportError("generated formal-release state violates the private reviewed policy")
    return generated


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--candidate", action="store_true", help="allow dirty/unapproved review builds")
    parser.add_argument("--include-game-packs", action="store_true", help="include optional installed game packs")
    parser.add_argument("--verify", type=Path, help="verify an archive using only its public manifest")
    parser.add_argument("--verify-policy", type=Path, help="also enforce the private reviewed policy")
    args = parser.parse_args()
    root = (args.root or Path(__file__).resolve().parent.parent).resolve()
    policy_path = (args.manifest or root / DEFAULT_CONFIG).resolve()
    try:
        if args.verify and args.verify_policy:
            parser.error("--verify and --verify-policy are mutually exclusive")
        if args.verify:
            verify_archive(args.verify.resolve())
            print(f"public source archive verified: {args.verify.resolve()}")
            return 0
        policy = load_policy(policy_path)
        if args.verify_policy:
            verify_archive(args.verify_policy.resolve(), policy)
            print(f"public source archive and private policy verified: {args.verify_policy.resolve()}")
            return 0
        default_output = DEFAULT_CANDIDATE_OUTPUT if args.candidate else DEFAULT_FORMAL_OUTPUT
        output = (args.output or root / default_output).resolve()
        result = build_archive(root, policy_path, output, candidate=args.candidate,
                               include_game_packs=args.include_game_packs)
    except (ExportError, IntegrityError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile, KeyError, TypeError) as error:
        print(f"public source export failed: {error}", file=sys.stderr)
        return 1
    print(f"public source archive: {output}")
    print(f"files: {result['file_count']}")
    print(f"candidate: {str(result['candidate']).lower()}")
    print(f"release status: {result['release_status']}")
    print(f"tracked dirty: {str(result['tracked_dirty']).lower()}")
    print(f"archive sha256: {result['archive_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
