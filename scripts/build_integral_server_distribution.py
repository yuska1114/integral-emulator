#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the source-only Ubuntu distribution for the standalone server."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import os
import tarfile
import tempfile
from pathlib import Path


VERSION = "0.1.1"
PACKAGE_ROOT = f"integral-server-{VERSION}"
INCLUDE = (
    Path("install.sh"),
    Path("pyproject.toml"),
    Path("LICENSES/AGPL-3.0-or-later.txt"),
    Path("docs/INTEGRAL_SERVER_APPLICATION.md"),
    Path("deploy/integral-server"),
    Path("deploy/systemd/integral-server.service"),
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


def normalized_info(info: tarfile.TarInfo) -> tarfile.TarInfo | None:
    relative = Path(info.name)
    if excluded(relative):
        return None
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = 0
    if info.isdir():
        info.mode = 0o755
    elif info.name.endswith(
        (
            "/install.sh",
            "/deploy/integral-server/integral-server",
            "/deploy/integral-server/uninstall",
        )
    ):
        info.mode = 0o755
    else:
        info.mode = 0o644
    return info


def build(root: Path, output: Path) -> str:
    for relative in INCLUDE:
        if not (root / relative).exists():
            raise FileNotFoundError(
                f"required distribution path is missing: {relative}"
            )
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
                    for relative in INCLUDE:
                        archive.add(
                            root / relative,
                            arcname=f"{PACKAGE_ROOT}/{relative.as_posix()}",
                            recursive=True,
                            filter=normalized_info,
                        )
        with tarfile.open(temporary, "r:gz") as archive:
            members = archive.getmembers()
            if not members:
                raise ValueError("distribution archive is empty")
            for member in members:
                path = Path(member.name)
                if (
                    not path.parts
                    or path.parts[0] != PACKAGE_ROOT
                    or excluded(path)
                ):
                    raise ValueError(f"invalid distribution member: {member.name}")
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
