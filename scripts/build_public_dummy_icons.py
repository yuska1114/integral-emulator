#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate the public, brand-neutral application icon files."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - dependency error is user-facing
    raise SystemExit("Pillow is required: python3 -m pip install Pillow") from error


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = ROOT / "assets" / "public"
CANVAS = 1024


def render() -> Image.Image:
    return Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 255))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = render()
    source.save(output / "integral_emulator_icon.png", format="PNG", optimize=True)
    source.convert("RGB").resize((256, 256), Image.Resampling.LANCZOS).save(
        output / "integral_emulator_icon.bmp", format="BMP"
    )
    source.save(
        output / "integral_emulator_icon.ico", format="ICO",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    source.save(output / "integral_emulator_icon.icns", format="ICNS")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
