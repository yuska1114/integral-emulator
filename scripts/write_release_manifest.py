#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write and verify a C Client product-release inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from public_source_integrity import IntegrityError, source_identity, verify_archive as verify_public_archive


MANIFEST_NAME = "RELEASE_MANIFEST.json"
SHA256_NAME = "SHA256SUMS"
FORBIDDEN_SUFFIXES = {
    ".gb", ".gbc", ".z64", ".n64", ".v64", ".sav", ".rtc",
    ".log", ".sqlite", ".sqlite3", ".db", ".db-wal", ".db-shm",
    ".p12", ".pfx", ".key", ".bin",
}
FORBIDDEN_NAMES = {".DS_Store", ".env", "integral_client.conf"}
PRIVATE_KEY_PATTERN = re.compile(
    rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----"
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_relative_path(relative: str) -> None:
    path = PurePosixPath(relative)
    if not relative or path.is_absolute() or ".." in path.parts:
        raise SystemExit(f"unsafe release path: {relative}")
    if any(part == "__MACOSX" for part in path.parts):
        raise SystemExit(f"forbidden release path: {relative}")
    if path.name in FORBIDDEN_NAMES:
        raise SystemExit(f"forbidden release file: {relative}")
    lower_name = path.name.lower()
    allowed_bootrom = relative.endswith("runtimes/gb/bootroms/sgb2_boot.bin")
    if any(lower_name.endswith(suffix) for suffix in FORBIDDEN_SUFFIXES) and not allowed_bootrom:
        raise SystemExit(f"forbidden release file: {relative}")


def regular_files(release_dir: Path) -> dict[str, Path]:
    files: dict[str, Path] = {}
    for path in sorted(release_dir.rglob("*")):
        relative = path.relative_to(release_dir).as_posix()
        validate_relative_path(relative)
        if path.is_symlink():
            raise SystemExit(f"release symlinks are not allowed: {relative}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise SystemExit(f"unsupported release filesystem object: {relative}")
        if PRIVATE_KEY_PATTERN.search(path.read_bytes()):
            raise SystemExit(f"private key material found in release: {relative}")
        files[relative] = path
    return files


def executable_contract(platform: str, app_name: str = "INTEGRAL EMULATOR") -> dict[str, dict[str, object]]:
    if platform == "windows":
        paths = {
            "client": "client/integral_client.exe",
            "gb_dual_server": "runtimes/gb/integral_gb_runtime_dual_server.exe",
            "gb_fixed_host": "runtimes/gb/integral_gb_runtime_fixed_host.exe",
            "gb_mobile_runtime": "runtimes/gb/integral_gb_runtime_mobile_runtime.exe",
            "n64_frontend": "runtimes/n64/build/integral_n64_runtime_frontend.exe",
            "gb_frontend": "runtimes/gb/integral_gb_runtime_frontend.exe",
        }
    elif platform == "linux":
        paths = {
            "client": "client/integral_client",
            "gb_dual_server": "runtimes/gb/integral_gb_runtime_dual_server",
            "gb_fixed_host": "runtimes/gb/integral_gb_runtime_fixed_host",
            "gb_mobile_runtime": "runtimes/gb/integral_gb_runtime_mobile_runtime",
            "n64_frontend": "runtimes/n64/build/integral_n64_runtime_frontend",
        }
    else:
        prefix = f"{app_name}.app/Contents/Resources/"
        paths = {
            "client": f"{prefix}client/integral_client",
            "gb_dual_server": f"{prefix}runtimes/gb/integral_gb_runtime_dual_server",
            "gb_fixed_host": f"{prefix}runtimes/gb/integral_gb_runtime_fixed_host",
            "gb_mobile_runtime": f"{prefix}runtimes/gb/integral_gb_runtime_mobile_runtime",
            "n64_frontend": f"{prefix}runtimes/n64/build/integral_n64_runtime_frontend",
        }
    return {
        role: {"canonical": path, "compatibility": []}
        for role, path in paths.items()
    }


def executable_paths(platform: str, app_name: str) -> dict[str, str]:
    return {
        role: str(contract["canonical"])
        for role, contract in executable_contract(platform, app_name).items()
    }


def current_source_identity(project_root: Path) -> dict[str, object]:
    try:
        return source_identity(project_root)
    except IntegrityError as error:
        raise SystemExit(str(error)) from error


def build_source_state(release_dir: Path, platform: str, project_root: Path) -> dict[str, object]:
    record_path = release_dir / "BUILD_PROVENANCE.json"
    if not record_path.is_file():
        raise SystemExit("release is missing BUILD_PROVENANCE.json")
    try:
        record = json.loads(record_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit("release build provenance is invalid") from error
    source = record.get("source")
    recorded_files = record.get("files")
    if record.get("format") != 2 or record.get("platform") != platform:
        raise SystemExit("release build provenance platform does not match")
    if not isinstance(source, dict):
        raise SystemExit("release build provenance source identity is invalid")
    commit = source.get("source_commit")
    dirty = source.get("dirty")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40,64}", commit):
        raise SystemExit("release build provenance source commit is invalid")
    if type(dirty) is not bool:
        raise SystemExit("release build provenance source dirty state is invalid")
    if not isinstance(recorded_files, dict) or not recorded_files:
        raise SystemExit("release build provenance file inventory is missing")
    actual_files = {
        relative: path
        for relative, path in regular_files(release_dir).items()
        if relative not in {"BUILD_PROVENANCE.json", MANIFEST_NAME, SHA256_NAME}
    }
    if set(recorded_files) != set(actual_files):
        raise SystemExit("release build provenance file set does not match")
    for relative, expected_hash in recorded_files.items():
        if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise SystemExit(f"release build provenance hash is invalid: {relative}")
        if sha256_file(actual_files[relative]) != expected_hash:
            raise SystemExit(f"release build provenance hash mismatch: {relative}")
    current_source = current_source_identity(project_root)
    if dirty:
        raise SystemExit("dirty build artifacts cannot be packaged")
    if current_source["dirty"]:
        raise SystemExit("release packaging requires a clean checkout")
    if source != current_source:
        raise SystemExit("release build source identity does not match the current source tree")
    return source


def verify_formal_source(project_root: Path, archive_path: Path) -> dict[str, str]:
    source = current_source_identity(project_root)
    if source["dirty"]:
        raise SystemExit("formal dist/releases generation requires a clean worktree")
    if not archive_path.is_file():
        raise SystemExit(f"formal public source archive is missing: {archive_path}")
    try:
        public_manifest = verify_public_archive(archive_path, require_formal=True)
    except (IntegrityError, zipfile.BadZipFile) as error:
        raise SystemExit(str(error)) from error
    if public_manifest.get("source_commit") != source["source_commit"]:
        raise SystemExit("public source archive commit does not match the product release")
    return {
        "filename": archive_path.name,
        "sha256": sha256_file(archive_path),
    }


def write_sha256s(release_dir: Path) -> None:
    files = regular_files(release_dir)
    payload = [
        relative for relative in files
        if relative not in {SHA256_NAME, MANIFEST_NAME}
    ]
    lines = [f"{sha256_file(files[relative])}  {relative}" for relative in payload]
    (release_dir / SHA256_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_manifest(
    release_dir: Path,
    platform: str,
    version: str,
    app_name: str,
    minimum_os: str,
    source_archive: dict[str, str] | None,
    project_root: Path,
) -> None:
    files = regular_files(release_dir)
    hashes = {
        relative: sha256_file(path)
        for relative, path in files.items()
        if relative != MANIFEST_NAME
    }
    executables: dict[str, dict[str, str]] = {}
    for role, path in executable_paths(platform, app_name).items():
        if path not in hashes:
            raise SystemExit(f"release manifest missing canonical {role}: {path}")
        executables[role] = {"path": path, "sha256": hashes[path]}
    required_legal = {
        "LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md",
        "LICENSES/GPL-2.0-or-later.txt", "LICENSES/GPL-3.0-or-later.txt",
    }
    missing_legal = sorted(required_legal - set(hashes))
    if missing_legal:
        raise SystemExit(f"release legal files are missing: {', '.join(missing_legal)}")
    source = build_source_state(release_dir, platform, project_root)
    manifest = {
        "format": 3,
        "platform": platform,
        "version": version,
        "minimum_os": minimum_os or None,
        "source": source,
        "source_archive": source_archive,
        "hash_scope": "all regular files except RELEASE_MANIFEST.json",
        "files": hashes,
        "licenses": sorted(
            path for path in hashes
            if path == "LICENSE" or path.startswith("LICENSES/")
            or "LICENSE" in path or "COPYING" in path or "NOTICE" in path
        ),
        "executables": executables,
    }
    (release_dir / MANIFEST_NAME).write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def parse_sha256s(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match or match.group(2) in entries:
            raise SystemExit(f"invalid {SHA256_NAME} entry: {line}")
        entries[match.group(2)] = match.group(1)
    return entries


def verify_release_dir(release_dir: Path) -> dict[str, Path]:
    files = regular_files(release_dir)
    if MANIFEST_NAME not in files or SHA256_NAME not in files:
        raise SystemExit("release manifest or SHA256SUMS is missing")
    manifest = json.loads(files[MANIFEST_NAME].read_text(encoding="utf-8"))
    actual_hashes = {
        relative: sha256_file(path)
        for relative, path in files.items()
        if relative != MANIFEST_NAME
    }
    if manifest.get("files") != actual_hashes:
        raise SystemExit("RELEASE_MANIFEST.json file set or hash mismatch")
    sums = parse_sha256s(files[SHA256_NAME])
    expected_sum_paths = set(files) - {SHA256_NAME, MANIFEST_NAME}
    if set(sums) != expected_sum_paths:
        raise SystemExit("SHA256SUMS file set mismatch")
    for relative, expected in sums.items():
        if sha256_file(files[relative]) != expected:
            raise SystemExit(f"SHA256SUMS mismatch: {relative}")
    return files


def archive_file_data(archive_path: Path, release_name: str) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    prefix = f"{release_name}/"
    if archive_path.name.endswith(".zip"):
        with zipfile.ZipFile(archive_path) as archive:
            for info in archive.infolist():
                name = info.filename
                path = PurePosixPath(name)
                if path.is_absolute() or ".." in path.parts or not name.startswith(prefix):
                    raise SystemExit(f"unsafe archive path: {name}")
                mode = info.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise SystemExit(f"archive symlink is not allowed: {name}")
                if info.is_dir():
                    continue
                relative = name[len(prefix):]
                validate_relative_path(relative)
                if relative in result:
                    raise SystemExit(f"duplicate archive path: {name}")
                result[relative] = archive.read(info)
    elif archive_path.name.endswith(".tar.gz"):
        with tarfile.open(archive_path, "r:gz") as archive:
            for member in archive.getmembers():
                name = member.name
                path = PurePosixPath(name)
                if path.is_absolute() or ".." in path.parts:
                    raise SystemExit(f"unsafe archive path: {name}")
                if name == release_name and member.isdir():
                    continue
                if not name.startswith(prefix):
                    raise SystemExit(f"unsafe archive path: {name}")
                if member.isdir():
                    continue
                if not member.isfile():
                    raise SystemExit(f"unsupported archive member: {name}")
                relative = name[len(prefix):]
                validate_relative_path(relative)
                if relative in result:
                    raise SystemExit(f"duplicate archive path: {name}")
                source = archive.extractfile(member)
                if source is None:
                    raise SystemExit(f"archive member cannot be read: {name}")
                result[relative] = source.read()
    else:
        raise SystemExit(f"unsupported release archive: {archive_path}")
    return result


def verify_archive(release_dir: Path, archive_path: Path) -> None:
    files = verify_release_dir(release_dir)
    archive_files = archive_file_data(archive_path, release_dir.name)
    expected = {relative: path.read_bytes() for relative, path in files.items()}
    if archive_files.keys() != expected.keys():
        raise SystemExit("archive and release directory file sets differ")
    for relative, data in archive_files.items():
        if data != expected[relative]:
            raise SystemExit(f"archive content mismatch: {relative}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_dir", type=Path, nargs="?")
    parser.add_argument("--platform", choices=("linux", "windows", "macos"))
    parser.add_argument("--app-name", default="INTEGRAL EMULATOR")
    parser.add_argument("--version")
    parser.add_argument("--minimum-os", default="")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--formal", action="store_true")
    parser.add_argument("--preflight-formal", action="store_true")
    parser.add_argument("--public-source-archive", type=Path)
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    public_source = (
        args.public_source_archive
        or project_root / "dist/source/INTEGRAL_EMULATOR_PUBLIC_SOURCE.zip"
    ).resolve()
    source_archive = None
    if args.preflight_formal or args.formal:
        source_archive = verify_formal_source(project_root, public_source)
    if args.preflight_formal:
        return 0
    if args.release_dir is None or args.platform is None or args.version is None:
        parser.error("release_dir, --platform, and --version are required")
    release_dir = args.release_dir.resolve()
    if not release_dir.is_dir():
        raise SystemExit(f"release directory not found: {release_dir}")
    write_sha256s(release_dir)
    write_manifest(
        release_dir, args.platform, args.version, args.app_name,
        args.minimum_os, source_archive, project_root,
    )
    verify_release_dir(release_dir)
    if args.archive is not None:
        verify_archive(release_dir, args.archive.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
