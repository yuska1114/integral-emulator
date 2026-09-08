#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the complete pinned libmobile tree used by Integral GB."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import sys


REVISION = "0704f56902f23b7ebf05c82c222e0e145e3140b6"
UPSTREAM_REPOSITORY = "https://github.com/REONTeam/libmobile"
UPSTREAM_ARCHIVE_SHA256 = "b3bb8407b9039ee229d76e9cc64b91a06c34624a66a8522f610e2aeb1905bb13"
UPSTREAM_TREE_SHA256 = "e7fe8de42789bdec99a0914f3602fccd100f4eca5c7e5b38dee1f98790c60cd6"
UPSTREAM_FILES = {
    ".gitignore",
    "CMakeLists.txt",
    "CMakeOptions.txt",
    "COPYING",
    "COPYING.LESSER",
    "Makefile.am",
    "README.md",
    "atomic.h",
    "callback.c",
    "callback.h",
    "commands.c",
    "commands.h",
    "compat.h",
    "config.c",
    "config.h",
    "configure.ac",
    "debug.c",
    "debug.h",
    "dns.c",
    "dns.h",
    "global.h",
    "inet_pton.c",
    "libmobile.pc.in",
    "memsize.sh",
    "meson.build",
    "meson_options.txt",
    "mobile.c",
    "mobile.h",
    "mobile_config.cmake.h.in",
    "mobile_config.h.in",
    "mobile_config.meson.h.in",
    "mobile_data.h",
    "mobile_inet.h",
    "relay.c",
    "relay.h",
    "serial.c",
    "serial.h",
    "util.c",
    "util.h",
}


def tree_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for relative in sorted(UPSTREAM_FILES):
        path = root / relative
        if os.name == "nt":
            # MSYS2's Windows mount does not reliably expose a transferred
            # POSIX executable bit. The pinned upstream tree has one script.
            mode = "100755" if relative == "memsize.sh" else "100644"
        else:
            mode = "100755" if path.stat().st_mode & 0o111 else "100644"
        file_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        digest.update(f"{mode} {relative} {file_hash}\n".encode("utf-8"))
    return digest.hexdigest()


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} LIBMOBILE_ROOT", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    marker = root / "INTEGRAL_REVISION.txt"
    if not marker.is_file() or f"Revision: {REVISION}" not in marker.read_text():
        print(f"libmobile revision marker is missing or not pinned to {REVISION}", file=sys.stderr)
        return 1
    actual_files = {path.name for path in root.iterdir() if path.is_file()}
    expected_files = UPSTREAM_FILES | {"INTEGRAL_REVISION.txt"}
    if actual_files != expected_files:
        missing = sorted(expected_files - actual_files)
        extra = sorted(actual_files - expected_files)
        print(f"libmobile tree file-set mismatch: missing={missing} extra={extra}", file=sys.stderr)
        return 1
    actual_tree_hash = tree_sha256(root)
    if actual_tree_hash != UPSTREAM_TREE_SHA256:
        print(
            f"libmobile tree mismatch: expected={UPSTREAM_TREE_SHA256} actual={actual_tree_hash}",
            file=sys.stderr,
        )
        return 1
    print(
        "libmobile dependency verified: "
        f"revision={REVISION} upstream_files={len(UPSTREAM_FILES)} "
        f"tree_sha256={actual_tree_hash} archive_sha256={UPSTREAM_ARCHIVE_SHA256}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
