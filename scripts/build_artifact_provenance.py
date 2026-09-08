#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record and verify the source identity of platform build artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from public_source_integrity import IntegrityError, source_identity


RECORD_NAME = "BUILD_PROVENANCE.json"
IGNORED_GENERATED_FILES = {RECORD_NAME, "RELEASE_MANIFEST.json", "SHA256SUMS"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def artifact_files(artifact_root: Path) -> dict[str, Path]:
    files: dict[str, Path] = {}
    for path in sorted(artifact_root.rglob("*")):
        relative = path.relative_to(artifact_root).as_posix()
        if path.name == ".DS_Store" or path.name.startswith("._") or "__MACOSX" in Path(relative).parts:
            raise SystemExit(f"macOS metadata is not allowed in build artifacts: {relative}")
        if path.is_symlink():
            raise SystemExit(f"build artifact symlink is not allowed: {path}")
        if not path.is_file():
            continue
        if relative in IGNORED_GENERATED_FILES:
            continue
        files[relative] = path
    if not files:
        raise SystemExit("build provenance requires at least one artifact")
    return files


def write_record(
    artifact_root: Path,
    project_root: Path,
    platform: str,
    source_record: dict[str, object] | None = None,
) -> Path:
    if source_record is None:
        try:
            source = source_identity(project_root)
        except IntegrityError as error:
            raise SystemExit(str(error)) from error
    else:
        source = source_record["source"]
        if not isinstance(source, dict):
            raise SystemExit("input build provenance source identity is invalid")
    files = artifact_files(artifact_root)
    record = {
        "format": 2,
        "platform": platform,
        "source": source,
        "files": {relative: sha256_file(path) for relative, path in files.items()},
    }
    output = artifact_root / RECORD_NAME
    output.write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return output


def load_and_verify(
    artifact_root: Path,
    platform: str,
    expected_source: dict[str, object] | None,
    require_clean: bool,
) -> dict[str, object]:
    record_path = artifact_root / RECORD_NAME
    if not record_path.is_file():
        raise SystemExit(f"build provenance is missing: {record_path}")
    try:
        record = json.loads(record_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"build provenance is invalid: {record_path}") from error
    if record.get("format") != 2 or record.get("platform") != platform:
        raise SystemExit("build provenance format or platform does not match")
    source = record.get("source")
    files = record.get("files")
    if not isinstance(source, dict):
        raise SystemExit("build provenance source identity is invalid")
    commit = source.get("source_commit")
    dirty = source.get("dirty")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40,64}", commit):
        raise SystemExit("build provenance source commit is invalid")
    if type(dirty) is not bool:
        raise SystemExit("build provenance source dirty state is invalid")
    if require_clean and dirty:
        raise SystemExit("dirty build artifacts cannot be integrated")
    if expected_source is not None and source != expected_source:
        raise SystemExit("build provenance source identity does not match the current source tree")
    if not isinstance(files, dict) or not files:
        raise SystemExit("build provenance file inventory is missing")
    actual = artifact_files(artifact_root)
    if set(files) != set(actual):
        raise SystemExit("build provenance file set does not match the build artifacts")
    for relative, expected_hash in files.items():
        if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise SystemExit(f"build provenance hash is invalid: {relative}")
        if sha256_file(actual[relative]) != expected_hash:
            raise SystemExit(f"build provenance hash mismatch: {relative}")
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_root", type=Path)
    parser.add_argument("--platform", required=True, choices=("linux", "windows", "macos"))
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--expected-commit")
    parser.add_argument("--require-clean", action="store_true")
    parser.add_argument("--source-provenance-root", type=Path)
    parser.add_argument("--match-source", action="store_true")
    args = parser.parse_args()
    if args.write == args.verify:
        parser.error("choose exactly one of --write or --verify")
    artifact_root = args.artifact_root.resolve()
    if not artifact_root.is_dir():
        raise SystemExit(f"artifact root is not a directory: {artifact_root}")
    if args.write:
        source_record = None
        if args.source_provenance_root is not None:
            project_root = args.project_root.resolve()
            try:
                expected_source = source_identity(project_root)
            except IntegrityError as error:
                raise SystemExit(str(error)) from error
            if expected_source["dirty"]:
                raise SystemExit("provenance integration requires a clean checkout")
            source_record = load_and_verify(
                args.source_provenance_root.resolve(), args.platform,
                expected_source, True,
            )
        path = write_record(
            artifact_root, args.project_root.resolve(), args.platform, source_record
        )
        print(path)
    else:
        expected_source = None
        if args.match_source:
            try:
                expected_source = source_identity(args.project_root.resolve())
            except IntegrityError as error:
                raise SystemExit(str(error)) from error
        record = load_and_verify(
            artifact_root, args.platform, expected_source, args.require_clean
        )
        if args.expected_commit and record["source"]["source_commit"] != args.expected_commit:
            raise SystemExit("build provenance source commit does not match --expected-commit")
        print(f"verified {args.platform} build {record['source']['source_commit']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
