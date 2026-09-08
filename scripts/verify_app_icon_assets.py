#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed when an application icon is missing or has the wrong format."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


NAMES = {
    "png": "integral_emulator_icon.png",
    "ico": "integral_emulator_icon.ico",
    "icns": "integral_emulator_icon.icns",
    "bmp": "integral_emulator_icon.bmp",
}


def read(path: Path) -> bytes:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SystemExit(f"application icon is unavailable: {path}: {error}") from error
    if not data:
        raise SystemExit(f"application icon is empty: {path}")
    return data


def verify(path: Path, kind: str) -> None:
    data = read(path)
    if kind == "png":
        if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            raise SystemExit(f"invalid PNG application icon: {path}")
        width, height = struct.unpack(">II", data[16:24])
        if width != height or width < 256:
            raise SystemExit(f"PNG application icon must be square and at least 256 px: {path}")
    elif kind == "bmp":
        if len(data) < 26 or data[:2] != b"BM":
            raise SystemExit(f"invalid BMP application icon: {path}")
        width, height = struct.unpack("<ii", data[18:26])
        if width != abs(height) or width < 32:
            raise SystemExit(f"BMP application icon must be square and at least 32 px: {path}")
    elif kind == "ico":
        if len(data) < 22 or data[:4] != b"\x00\x00\x01\x00":
            raise SystemExit(f"invalid ICO application icon: {path}")
        count = struct.unpack("<H", data[4:6])[0]
        if count < 1 or len(data) < 6 + count * 16:
            raise SystemExit(f"ICO application icon has no valid directory: {path}")
        for index in range(count):
            entry = data[6 + index * 16:22 + index * 16]
            size, offset = struct.unpack("<II", entry[8:16])
            if size == 0 or offset < 6 + count * 16 or offset + size > len(data):
                raise SystemExit(f"ICO application icon has an invalid image entry: {path}")
    elif kind == "icns":
        if len(data) < 16 or data[:4] != b"icns" or struct.unpack(">I", data[4:8])[0] != len(data):
            raise SystemExit(f"invalid ICNS application icon: {path}")
        offset = 8
        chunks = 0
        while offset < len(data):
            if offset + 8 > len(data):
                raise SystemExit(f"ICNS application icon has a truncated chunk: {path}")
            length = struct.unpack(">I", data[offset + 4:offset + 8])[0]
            if length < 8 or offset + length > len(data):
                raise SystemExit(f"ICNS application icon has an invalid chunk: {path}")
            chunks += 1
            offset += length
        if chunks == 0 or offset != len(data):
            raise SystemExit(f"ICNS application icon has no image chunks: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="icon directory, or a file with --format")
    parser.add_argument("--format", choices=tuple(NAMES))
    args = parser.parse_args()
    if args.format:
        verify(args.path.resolve(), args.format)
    else:
        root = args.path.resolve()
        for kind, name in NAMES.items():
            verify(root / name, kind)
    print(f"application icon verified: {args.path.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
