#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify locked upstream, vendored, public-export, and patch identities."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any


PUBLIC_CATEGORIES = {
    "PUBLIC_REQUIRED",
    "PUBLIC_OPTIONAL_DOC",
    "THIRD_PARTY_REQUIRED",
}
DEFAULT_LOCK = "THIRD_PARTY_LOCK.json"
DEFAULT_MANIFEST = "config/public_source_manifest.json"
GENERATED_PUBLIC_MANIFEST = "PUBLIC_SOURCE_MANIFEST.json"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
GIT_OID_PATTERN = re.compile(r"[0-9a-f]{40}\Z")


class LockError(ValueError):
    """Raised when a locked third-party identity cannot be reproduced."""


def _safe_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if (
        not value
        or "\\" in value
        or path.is_absolute()
        or value != path.as_posix()
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        raise LockError(f"unsafe lock path: {value!r}")
    return path


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _git(root: Path, *args: str) -> bytes:
    try:
        return subprocess.check_output(["git", "-C", str(root), *args])
    except subprocess.CalledProcessError as error:
        raise LockError(f"git command failed under {root}: {' '.join(args)}") from error


def _git_index(root: Path, prefix: str | None = None) -> dict[str, str]:
    raw = _git(root, "ls-files", "-s", "-z", *( [prefix] if prefix else [] ))
    result: dict[str, str] = {}
    marker = f"{prefix}/" if prefix else ""
    for item in raw.split(b"\0"):
        if not item:
            continue
        metadata, raw_path = item.split(b"\t", 1)
        path = raw_path.decode("utf-8")
        if marker:
            if not path.startswith(marker):
                continue
            path = path[len(marker):]
        result[path] = metadata.split(b" ", 1)[0].decode("ascii")
    return result


def inventory_digest(
    base: Path,
    index: dict[str, str],
    line_ending_normalizations: list[str] | tuple[str, ...] = (),
) -> str:
    digest = hashlib.sha256()
    normalized = set(line_ending_normalizations)
    for relative in sorted(index):
        path = base / relative
        if not path.is_file():
            raise LockError(f"locked source file is missing: {path}")
        data = path.read_bytes()
        if relative in normalized:
            data = data.replace(b"\r\n", b"\n")
        content_hash = hashlib.sha256(data).hexdigest()
        digest.update(f"{index[relative]} {relative} {content_hash}\n".encode("utf-8"))
    return digest.hexdigest()


def load_lock(path: Path) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LockError(f"cannot read third-party lock: {error}") from error
    if (
        lock.get("format") != 1
        or lock.get("digest_algorithm") != "sha256-mode-path-content-v1"
        or lock.get("public_digest_algorithm") != "sha256-path-content-v1"
    ):
        raise LockError("unsupported third-party lock format")
    components = lock.get("components")
    if not isinstance(components, list) or not components:
        raise LockError("third-party lock has no components")
    names: set[str] = set()
    for component in components:
        if not isinstance(component, dict):
            raise LockError("third-party component must be an object")
        name = component.get("component")
        if not isinstance(name, str) or not name or name in names:
            raise LockError("third-party component names must be unique")
        _safe_path(name)
        names.add(name)
        for key in (
            "source_path",
            "upstream_repository",
            "upstream_commit",
            "upstream_archive_url",
            "upstream_archive_sha256",
            "upstream_tree_oid",
            "upstream_tree_sha256",
            "vendored_tree_sha256",
            "public_source_tree_sha256",
            "license_review_status",
        ):
            if not isinstance(component.get(key), str) or not component[key]:
                raise LockError(f"{name}: missing {key}")
        for key in (
            "upstream_archive_sha256",
            "upstream_tree_sha256",
            "vendored_tree_sha256",
            "public_source_tree_sha256",
        ):
            if not SHA256_PATTERN.fullmatch(component[key]):
                raise LockError(f"{name}: invalid {key}")
        for key in ("upstream_commit", "upstream_tree_oid"):
            if not GIT_OID_PATTERN.fullmatch(component[key]):
                raise LockError(f"{name}: invalid {key}")
        _safe_path(component["source_path"])
        for key in ("upstream_file_count", "vendored_file_count", "public_source_file_count"):
            if type(component.get(key)) is not int or component[key] < 0:
                raise LockError(f"{name}: invalid {key}")
        for key in ("excluded_upstream_paths", "line_ending_normalizations", "license_files"):
            values = component.get(key)
            if not isinstance(values, list) or any(not isinstance(value, str) for value in values):
                raise LockError(f"{name}: invalid {key}")
            for value in values:
                _safe_path(value)
        for key in ("materialized_patches", "build_patches"):
            patches = component.get(key)
            if not isinstance(patches, list):
                raise LockError(f"{name}: invalid {key}")
            for patch in patches:
                if not isinstance(patch, dict) or set(patch) != {"license", "order", "path", "sha256"}:
                    raise LockError(f"{name}: invalid patch record")
                _safe_path(patch["path"])
                if not SHA256_PATTERN.fullmatch(patch["sha256"]):
                    raise LockError(f"{name}: invalid patch SHA-256")
    return lock


def _public_index(root: Path, source_path: str) -> dict[str, str]:
    manifest_path = root / DEFAULT_MANIFEST
    generated = False
    if not manifest_path.is_file():
        manifest_path = root / GENERATED_PUBLIC_MANIFEST
        generated = True
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LockError(f"cannot read public manifest: {error}") from error
    marker = f"{source_path}/"
    result: dict[str, str] = {}
    records = manifest.get("files", {}) if generated else manifest.get("files", [])
    selected_paths = records if generated and isinstance(records, dict) else (
        record.get("path")
        for record in records
        if isinstance(record, dict) and record.get("category") in PUBLIC_CATEGORIES
    )
    for path in selected_paths:
        if not isinstance(path, str):
            continue
        if not path.startswith(marker):
            continue
        relative = path[len(marker):]
        source = root / path
        if source.is_file():
            # ZIP extraction does not portably restore POSIX modes. The public
            # subset identity therefore uses a stable marker plus path/bytes;
            # the exporter separately validates and writes archive modes.
            result[relative] = "file"
    return result


def _verify_patch_records(root: Path, component: dict[str, Any]) -> None:
    for group in ("materialized_patches", "build_patches"):
        records = component[group]
        orders = [record["order"] for record in records]
        if orders and orders != list(range(1, len(orders) + 1)):
            raise LockError(f"{component['component']}: non-contiguous patch order")
        for record in records:
            path = root / record["path"]
            if not path.is_file() or _sha256(path) != record["sha256"]:
                raise LockError(f"{component['component']}: patch hash mismatch: {record['path']}")
    for relative in component["license_files"]:
        if not (root / relative).is_file():
            raise LockError(f"{component['component']}: license file is missing: {relative}")


def verify_local(root: Path, lock: dict[str, Any]) -> None:
    has_private_manifest = (root / DEFAULT_MANIFEST).is_file()
    verify_full_vendored_tree = (root / ".git").exists() and has_private_manifest
    for component in lock["components"]:
        _verify_patch_records(root, component)
        source_path = component["source_path"]
        source = root / source_path
        if not source.is_dir():
            raise LockError(f"{component['component']}: source directory is missing")
        public_index = _public_index(root, source_path)
        if len(public_index) != component["public_source_file_count"]:
            raise LockError(f"{component['component']}: public source file count mismatch")
        normalizations = component["line_ending_normalizations"]
        if inventory_digest(source, public_index, normalizations) != component["public_source_tree_sha256"]:
            raise LockError(f"{component['component']}: public source tree mismatch")
        if verify_full_vendored_tree:
            vendored_index = _git_index(root, source_path)
            if len(vendored_index) != component["vendored_file_count"]:
                raise LockError(f"{component['component']}: vendored file count mismatch")
            if inventory_digest(source, vendored_index, normalizations) != component["vendored_tree_sha256"]:
                raise LockError(f"{component['component']}: vendored tree mismatch")


def _apply_patch(directory: Path, patch_path: Path) -> None:
    with patch_path.open("rb") as patch_input:
        result = subprocess.run(
            ["patch", "-d", str(directory), "-p1", "--forward", "--batch"],
            stdin=patch_input,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    if result.returncode != 0:
        output = result.stdout.decode("utf-8", errors="replace")
        raise LockError(f"patch does not apply: {patch_path}\n{output}")


def verify_upstreams(root: Path, upstream_root: Path, lock: dict[str, Any]) -> None:
    for component in lock["components"]:
        name = component["component"]
        upstream = upstream_root / name
        commit = _git(upstream, "rev-parse", "HEAD").decode("ascii").strip()
        tree_oid = _git(upstream, "rev-parse", "HEAD^{tree}").decode("ascii").strip()
        if commit != component["upstream_commit"] or tree_oid != component["upstream_tree_oid"]:
            raise LockError(f"{name}: upstream Git identity mismatch")
        upstream_index = _git_index(upstream)
        if len(upstream_index) != component["upstream_file_count"]:
            raise LockError(f"{name}: upstream file count mismatch")
        if inventory_digest(upstream, upstream_index) != component["upstream_tree_sha256"]:
            raise LockError(f"{name}: upstream tree digest mismatch")

        with tempfile.TemporaryDirectory(prefix=f"integral-{name}-attest-") as temporary:
            transformed = Path(temporary) / name
            shutil.copytree(upstream, transformed, ignore=shutil.ignore_patterns(".git"))
            transformed_index = dict(upstream_index)
            for relative in component["excluded_upstream_paths"]:
                path = transformed / relative
                if not path.is_file() or relative not in transformed_index:
                    raise LockError(f"{name}: declared upstream exclusion is absent: {relative}")
                path.unlink()
                transformed_index.pop(relative)
            for record in component["materialized_patches"]:
                _apply_patch(transformed, root / record["path"])
            for relative in component["line_ending_normalizations"]:
                path = transformed / relative
                data = path.read_bytes()
                if b"\r\n" not in data:
                    raise LockError(f"{name}: declared CRLF normalization has no CRLF: {relative}")
                path.write_bytes(data.replace(b"\r\n", b"\n"))
            if len(transformed_index) != component["vendored_file_count"]:
                raise LockError(f"{name}: transformed vendored count mismatch")
            if inventory_digest(transformed, transformed_index) != component["vendored_tree_sha256"]:
                raise LockError(f"{name}: upstream transformations do not reproduce vendored tree")

        if component["build_patches"]:
            with tempfile.TemporaryDirectory(prefix=f"integral-{name}-patches-") as temporary:
                build_tree = Path(temporary) / name
                shutil.copytree(upstream, build_tree, ignore=shutil.ignore_patterns(".git"))
                for record in component["build_patches"]:
                    _apply_patch(build_tree, root / record["path"])


def verify_archives(archive_root: Path, lock: dict[str, Any]) -> None:
    for component in lock["components"]:
        filename = f"{component['component']}-{component['upstream_commit']}.tar.gz"
        archive = archive_root / filename
        if not archive.is_file():
            raise LockError(f"{component['component']}: official archive is missing: {archive}")
        if _sha256(archive) != component["upstream_archive_sha256"]:
            raise LockError(f"{component['component']}: official archive SHA-256 mismatch")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--lock", type=Path)
    parser.add_argument("--upstream-root", type=Path)
    parser.add_argument("--archive-root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    lock_path = (args.lock or root / DEFAULT_LOCK).resolve()
    try:
        lock = load_lock(lock_path)
        verify_local(root, lock)
        if args.upstream_root:
            verify_upstreams(root, args.upstream_root.resolve(), lock)
        if args.archive_root:
            verify_archives(args.archive_root.resolve(), lock)
    except (LockError, OSError, KeyError, TypeError) as error:
        print(f"third-party lock verification failed: {error}")
        return 1
    print(f"third-party lock verified: {len(lock['components'])} components")
    if args.upstream_root:
        print("upstream reconstruction verified: true")
    if args.archive_root:
        print("official archive hashes verified: true")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
