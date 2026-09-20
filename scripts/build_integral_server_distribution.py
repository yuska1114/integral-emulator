#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the source-only Ubuntu distribution for the standalone server."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
import sys
import tarfile
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from public_source_integrity import (
    MANIFEST_NAME, canonical_json, sha256_bytes, source_identity, verify_directory,
)


VERSION = "0.1.1"
PACKAGE_ROOT = f"integral-server-{VERSION}"
INCLUDE = (
    Path("install.sh"),
    Path("install-certificate-acl-watch.sh"),
    Path("pyproject.toml"),
    Path("LICENSES/AGPL-3.0-or-later.txt"),
    Path("docs/INTEGRAL_SERVER_APPLICATION.md"),
    Path("deploy/integral-server"),
    Path("deploy/systemd/integral-server.service"),
    Path("deploy/systemd/integral-server-media-relay.service"),
    Path("deploy/systemd/integral-server-certificate-acl.service"),
    Path("src/integral_emulator"),
    Path("config/allowed_roms"),
    Path("config/gb_runtime_link_macros.json"),
    Path("config/gb_mobile/schema"),
)


def excluded(path: Path) -> bool:
    return (
        "__pycache__" in path.parts
        or path.name == ".DS_Store"
        or path.name.startswith("._")
        or path.suffix == ".pyc"
    )


def build(root: Path, output: Path) -> str:
    # Only an exported, verified public tree is a distribution source. Never
    # silently filter private files from the internal development checkout.
    manifest = verify_directory(root)
    source = source_identity(root)
    payload: dict[str, bytes] = {}
    for relative in INCLUDE:
        source_path = root / relative
        if not source_path.exists():
            raise FileNotFoundError(
                f"required distribution path is missing: {relative}"
            )
        paths = [source_path]
        if source_path.is_dir():
            paths.extend(sorted(source_path.rglob("*")))
        for path in paths:
            name = path.relative_to(root).as_posix()
            if path.is_symlink():
                raise ValueError(f"distribution symlink is not allowed: {name}")
            if path.is_dir() or excluded(path):
                continue
            if not path.is_file() or name not in manifest["files"]:
                raise ValueError(f"distribution file is outside public manifest: {name}")
            data = path.read_bytes()
            if sha256_bytes(data) != manifest["files"][name]:
                raise ValueError(f"distribution content differs from public manifest: {name}")
            payload[name] = data
    provenance = canonical_json({
        "format": 1,
        "source": source,
        "files": {name: sha256_bytes(data) for name, data in sorted(payload.items())},
    })
    # These three generated metadata files are not source payload. The original
    # public manifest and provenance allow an independent subset/hash check.
    payload[MANIFEST_NAME] = (root / MANIFEST_NAME).read_bytes()
    payload["BUILD_PROVENANCE.json"] = provenance
    payload["SHA256SUMS"] = "".join(
        f"{sha256_bytes(data)}  {name}\n" for name, data in sorted(payload.items())
    ).encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with temporary.open("wb") as raw_stream:
            with gzip.GzipFile(
                filename="", mode="wb", fileobj=raw_stream, mtime=0
            ) as compressed:
                with tarfile.open(
                    mode="w", fileobj=compressed, format=tarfile.PAX_FORMAT
                ) as archive:
                    for name, data in sorted(payload.items()):
                        info = tarfile.TarInfo(f"{PACKAGE_ROOT}/{name}")
                        info.size = len(data)
                        info.mode = 0o755 if manifest["file_modes"].get(name) == "100755" else 0o644
                        archive.addfile(info, io.BytesIO(data))
        with tarfile.open(temporary, "r:gz") as archive:
            members = archive.getmembers()
            if len(members) != len(payload):
                raise ValueError("distribution archive inventory differs")
            for member in members:
                path = Path(member.name)
                if (
                    not path.parts
                    or path.parts[0] != PACKAGE_ROOT
                    or excluded(path)
                    or not member.isfile()
                    or path.relative_to(PACKAGE_ROOT).as_posix() not in payload
                ):
                    raise ValueError(f"invalid distribution member: {member.name}")
                name = path.relative_to(PACKAGE_ROOT).as_posix()
                if archive.extractfile(member).read() != payload[name]:
                    raise ValueError(f"distribution archive content differs: {name}")
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()
    return hashlib.sha256(output.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = (
        args.output
        or root / "dist/server" / f"INTEGRAL_SERVER_{VERSION}_UBUNTU.tar.gz"
    ).resolve()
    digest = build(root, output)
    print(f"server distribution: {output}")
    print(f"sha256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
