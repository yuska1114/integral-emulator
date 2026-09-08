#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-contained integrity and source-identity checks for public source trees."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import subprocess
import unicodedata
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any


MANIFEST_NAME = "PUBLIC_SOURCE_MANIFEST.json"
GENERATED_DIRECTORY_NAMES = {
    ".deps", ".libs", ".mypy_cache", ".pytest_cache", ".venv", "CMakeFiles",
    "__pycache__", "_obj", "build", "build_exp", "dist", "release", "runtime",
    "screenshots",
}
GENERATED_SUFFIXES = {
    ".a", ".d", ".dll", ".dylib", ".exe", ".ilk", ".la", ".lo", ".log",
    ".o", ".obj", ".pdb", ".pyc", ".pyo", ".res", ".so",
}
GENERATED_NAMES = {
    ".DS_Store", ".dependencies", "CMakeCache.txt", "cmake_install.cmake",
    "compile_commands.json", "install_manifest.txt",
}


class IntegrityError(ValueError):
    """The public source manifest, archive, or tree is inconsistent."""


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_path(value: str) -> PurePosixPath:
    if not value or "\x00" in value or "\\" in value:
        raise IntegrityError(f"unsafe public source path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or value != path.as_posix() or any(part in {"", ".", ".."} for part in path.parts):
        raise IntegrityError(f"unsafe public source path: {value!r}")
    return path


def _validate_unique_paths(paths: list[str]) -> None:
    exact: set[str] = set()
    normalized: dict[str, str] = {}
    folded: dict[str, str] = {}
    for value in paths:
        safe_path(value)
        nfc = unicodedata.normalize("NFC", value)
        case_key = nfc.casefold()
        if value in exact:
            raise IntegrityError(f"duplicate public source path: {value}")
        if nfc in normalized:
            raise IntegrityError(f"Unicode-normalization path collision: {normalized[nfc]} / {value}")
        if case_key in folded:
            raise IntegrityError(f"case-insensitive path collision: {folded[case_key]} / {value}")
        exact.add(value)
        normalized[nfc] = value
        folded[case_key] = value


def _is_digest(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def validate_manifest(manifest: dict[str, Any]) -> dict[str, str]:
    required = {
        "format", "release_status", "source_commit", "source_date_epoch",
        "file_count", "content_manifest_sha256", "files", "file_modes",
    }
    if set(manifest) != required or manifest.get("format") != 4:
        raise IntegrityError("unsupported or malformed public source manifest")
    if manifest.get("release_status") not in {"FORMAL", "NOT LICENSED FOR FORMAL RELEASE"}:
        raise IntegrityError("public source release status is invalid")
    commit = manifest.get("source_commit")
    if not isinstance(commit, str) or re.fullmatch(r"[0-9a-f]{40,64}", commit) is None:
        raise IntegrityError("public source commit is invalid")
    if type(manifest.get("source_date_epoch")) is not int or manifest["source_date_epoch"] < 0:
        raise IntegrityError("public source date epoch is invalid")
    for key in ("file_count",):
        if type(manifest.get(key)) is not int or manifest[key] < 0:
            raise IntegrityError(f"public source {key} is invalid")
    files = manifest.get("files")
    modes = manifest.get("file_modes")
    if not isinstance(files, dict) or not files or not isinstance(modes, dict) or set(modes) != set(files):
        raise IntegrityError("public source file inventory is invalid")
    paths = list(files)
    if not all(isinstance(path, str) for path in paths):
        raise IntegrityError("public source file path is not a string")
    _validate_unique_paths(paths + [MANIFEST_NAME])
    for path, digest in files.items():
        if not _is_digest(digest):
            raise IntegrityError(f"public source hash is invalid: {path}")
        if modes[path] not in ("100644", "100755"):
            raise IntegrityError(f"public source mode is invalid: {path}")
    if manifest.get("file_count") != len(files):
        raise IntegrityError("public source file_count is inconsistent")
    if manifest.get("content_manifest_sha256") != sha256_bytes(canonical_json(files)):
        raise IntegrityError("public source content digest is inconsistent")
    return files


def load_manifest_bytes(data: bytes) -> dict[str, Any]:
    try:
        manifest = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise IntegrityError("public source manifest is not valid UTF-8 JSON") from error
    if not isinstance(manifest, dict):
        raise IntegrityError("public source manifest must be an object")
    validate_manifest(manifest)
    return manifest


def verify_archive(archive_path: Path, *, require_formal: bool = False) -> dict[str, Any]:
    with zipfile.ZipFile(archive_path, "r") as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        _validate_unique_paths(names)
        if MANIFEST_NAME not in names:
            raise IntegrityError(f"public source archive lacks {MANIFEST_NAME}")
        manifest = load_manifest_bytes(archive.read(MANIFEST_NAME))
        files = manifest["files"]
        if set(names) != set(files) | {MANIFEST_NAME}:
            raise IntegrityError("public source archive has missing or extra files")
        for info in infos:
            if info.is_dir():
                raise IntegrityError(f"directory entry is not allowed: {info.filename}")
            unix_mode = (info.external_attr >> 16) & 0o170000
            if unix_mode == stat.S_IFLNK:
                raise IntegrityError(f"public source symlink is not allowed: {info.filename}")
            if info.flag_bits & 0x1:
                raise IntegrityError(f"encrypted public source entry is not allowed: {info.filename}")
            if info.filename == MANIFEST_NAME:
                continue
            data = archive.read(info)
            if sha256_bytes(data) != files[info.filename]:
                raise IntegrityError(f"public source content hash mismatch: {info.filename}")
            stored_mode = "100755" if ((info.external_attr >> 16) & 0o111) else "100644"
            if stored_mode != manifest["file_modes"][info.filename]:
                raise IntegrityError(f"public source executable mode mismatch: {info.filename}")
    if require_formal and manifest["release_status"] != "FORMAL":
        raise IntegrityError("public source is not a formal release")
    return manifest


def is_generated_path(relative: str) -> bool:
    path = PurePosixPath(relative)
    if not path.parts:
        return False
    if path.name in GENERATED_NAMES or path.name.startswith("._"):
        return True
    if path.suffix.lower() in GENERATED_SUFFIXES:
        return True
    return any(part in GENERATED_DIRECTORY_NAMES or part.endswith(".dSYM") for part in path.parts)


def verify_directory(root: Path) -> dict[str, Any]:
    root = root.resolve()
    manifest_path = root / MANIFEST_NAME
    try:
        manifest = load_manifest_bytes(manifest_path.read_bytes())
    except OSError as error:
        raise IntegrityError(f"public source manifest is unavailable: {manifest_path}") from error
    files = manifest["files"]
    for relative, expected_hash in files.items():
        path = root / relative
        if path.is_symlink() or not path.is_file():
            raise IntegrityError(f"public source file is missing or not regular: {relative}")
        if sha256_file(path) != expected_hash:
            raise IntegrityError(f"public source content hash mismatch: {relative}")
    extras: list[str] = []
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if relative == ".git" or relative.startswith(".git/"):
            continue
        if path.is_symlink():
            try:
                target = path.resolve(strict=True)
            except OSError:
                extras.append(relative)
                continue
            if not is_generated_path(relative) or not target.is_relative_to(root):
                extras.append(relative)
            continue
        if not path.is_file() or relative in files or relative == MANIFEST_NAME:
            continue
        if not is_generated_path(relative):
            extras.append(relative)
    if extras:
        raise IntegrityError(f"public source tree has unexpected files: {', '.join(sorted(extras)[:5])}")
    return manifest


def _git(root: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", *args], cwd=root, text=True, stderr=subprocess.DEVNULL
    ).strip()


def source_identity(root: Path) -> dict[str, Any]:
    root = root.resolve()
    manifest_path = root / MANIFEST_NAME
    has_git = False
    checkout_commit: str | None = None
    checkout_dirty = False
    try:
        has_git = _git(root, "rev-parse", "--is-inside-work-tree") == "true"
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    if has_git:
        checkout_commit = _git(root, "rev-parse", "HEAD")
        checkout_dirty = bool(_git(root, "status", "--porcelain=v1", "--untracked-files=all"))
        if re.fullmatch(r"[0-9a-f]{40,64}", checkout_commit) is None:
            raise IntegrityError("Git checkout commit is invalid")
    if manifest_path.is_file():
        manifest = verify_directory(root)
        return {
            "source_kind": "public_git_checkout" if has_git else "public_source_archive",
            "source_commit": manifest["source_commit"],
            "source_date_epoch": manifest["source_date_epoch"],
            "checkout_commit": checkout_commit,
            "dirty": checkout_dirty,
            "public_manifest_sha256": sha256_file(manifest_path),
            "public_source_release_status": manifest["release_status"],
        }
    if not has_git or checkout_commit is None:
        raise IntegrityError(f"source root has neither Git metadata nor {MANIFEST_NAME}")
    epoch = int(_git(root, "show", "-s", "--format=%ct", "HEAD"))
    return {
        "source_kind": "git_checkout",
        "source_commit": checkout_commit,
        "source_date_epoch": epoch,
        "checkout_commit": checkout_commit,
        "dirty": checkout_dirty,
        "public_manifest_sha256": None,
        "public_source_release_status": None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--archive", type=Path)
    group.add_argument("--directory", type=Path)
    group.add_argument("--source-identity", type=Path)
    parser.add_argument("--require-formal", action="store_true")
    parser.add_argument("--require-clean", action="store_true")
    parser.add_argument("--json", action="store_true", help="print the complete verified record")
    args = parser.parse_args()
    try:
        if args.archive:
            result = verify_archive(args.archive.resolve(), require_formal=args.require_formal)
        elif args.directory:
            result = verify_directory(args.directory.resolve())
            if args.require_formal and result["release_status"] != "FORMAL":
                raise IntegrityError("public source is not a formal release")
        else:
            result = source_identity(args.source_identity.resolve())
            if args.require_clean and result["dirty"]:
                raise IntegrityError("source tree is dirty")
    except (IntegrityError, OSError, zipfile.BadZipFile) as error:
        print(f"public source integrity failed: {error}")
        return 1
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    elif args.source_identity:
        print(
            f"source identity verified: kind={result['source_kind']} "
            f"dirty={str(result['dirty']).lower()}"
        )
    else:
        print(
            f"public source integrity verified: files={result['file_count']} "
            f"release_status={result['release_status']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
