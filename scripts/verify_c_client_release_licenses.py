#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the small, reviewed license-file allowlist for C Client releases."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath


COMMON_REQUIRED = (
    "LICENSE",
    "LICENSE_SCOPE.md",
    "THIRD_PARTY_NOTICES.md",
    "LICENSES/third-party/mupen64plus-core/LICENSES",
    "LICENSES/third-party/mupen64plus-core/gpl-license",
    "LICENSES/third-party/mupen64plus-core/lgpl-license",
    "LICENSES/third-party/mupen64plus-core/font-license",
    "LICENSES/third-party/GLideN64/LICENSE",
    "LICENSES/third-party/GLideN64/Glow/LICENSE",
    "LICENSES/third-party/GLideN64/gles2n64/LICENSE",
)
MACOS_REQUIRED = (
    "LICENSES/runtime-dependencies/FreeType/FTL.TXT",
    "LICENSES/runtime-dependencies/FreeType/GPLv2.TXT",
)
NOTICE_REFERENCES = (
    "LICENSES/third-party/mupen64plus-core/font-license",
    "LICENSES/third-party/mupen64plus-core/gpl-license",
    "LICENSES/third-party/mupen64plus-core/lgpl-license",
    "LICENSES/third-party/GLideN64/Glow/LICENSE",
    "LICENSES/third-party/GLideN64/gles2n64/LICENSE",
)


def verify(root: Path, platform: str) -> None:
    required = COMMON_REQUIRED + (MACOS_REQUIRED if platform == "macos" else ())
    failures: list[str] = []
    for relative in required:
        safe = PurePosixPath(relative)
        path = root.joinpath(*safe.parts)
        if not path.is_file() or path.stat().st_size == 0:
            failures.append(f"missing or empty: {relative}")

    notices_path = root / "THIRD_PARTY_NOTICES.md"
    notices = notices_path.read_text(encoding="utf-8") if notices_path.is_file() else ""
    for reference in NOTICE_REFERENCES:
        if reference not in notices:
            failures.append(f"notice reference missing: {reference}")

    if platform == "macos":
        dependencies_path = root / "RUNTIME_DEPENDENCIES.md"
        dependencies = (
            dependencies_path.read_text(encoding="utf-8")
            if dependencies_path.is_file()
            else ""
        )
        for reference in MACOS_REQUIRED:
            if reference not in dependencies:
                failures.append(f"runtime dependency reference missing: {reference}")

    if failures:
        raise SystemExit("Required release licenses failed:\n" + "\n".join(f"- {item}" for item in failures))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package_root", type=Path)
    parser.add_argument("--platform", required=True, choices=("linux", "windows", "macos"))
    args = parser.parse_args()
    verify(args.package_root.resolve(), args.platform)
    print(f"Verified required {args.platform} release licenses.")


if __name__ == "__main__":
    main()
