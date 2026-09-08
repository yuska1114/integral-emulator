#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebuild and verify the SameBoot SGB2 resource from preferred source."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_RGBDS_VERSION = "v1.0.3"
EXPECTED_SIZE = 256
EXPECTED_OUTPUT_SHA256 = "8a65465a9da7ec657726a671da9a85963cf19d2c975e499aea6b97eb37e0b6ea"
EXPECTED_CLOSURE_SHA256 = "742fef97deb37853124055a488981d96dd7a43e35eb1d8caf09afd0264a5b264"
CLOSURE_FILES = (
    "BootROMs/hardware.inc",
    "BootROMs/sameboot.inc",
    "BootROMs/sgb_boot.asm",
    "BootROMs/sgb2_boot.asm",
    "LICENSE",
    "version.mk",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def closure_sha256(sameboy_root: Path) -> str:
    digest = hashlib.sha256()
    for relative in CLOSURE_FILES:
        path = sameboy_root / relative
        if not path.is_file():
            raise ValueError(f"SameBoot source closure is missing {relative}")
        digest.update(f"100644 {relative} {sha256(path)}\n".encode("utf-8"))
    return digest.hexdigest()


def tool_version(command: str) -> str:
    try:
        result = subprocess.run(
            [command, "--version"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"required RGBDS tool is unavailable: {command}") from error
    return (result.stdout + result.stderr).strip()


def verify(project_root: Path) -> dict[str, str | int]:
    sameboy_root = project_root / "runtimes/gb/third_party/SameBoy"
    expected_resource = project_root / "runtimes/gb/assets/bootroms/sgb2_boot.bin"
    closure_hash = closure_sha256(sameboy_root)
    if closure_hash != EXPECTED_CLOSURE_SHA256:
        raise ValueError(
            f"SameBoot source closure mismatch: expected={EXPECTED_CLOSURE_SHA256} actual={closure_hash}"
        )
    versions = {tool: tool_version(tool) for tool in ("rgbasm", "rgblink")}
    for tool, version in versions.items():
        if EXPECTED_RGBDS_VERSION not in version:
            raise ValueError(
                f"{tool} version mismatch: expected {EXPECTED_RGBDS_VERSION}, found {version}"
            )
    with tempfile.TemporaryDirectory(prefix="integral-sameboot-") as temporary:
        work = Path(temporary)
        object_path = work / "sgb2_boot.o"
        output_path = work / "sgb2_boot.bin"
        subprocess.run(
            [
                "rgbasm",
                "--include",
                str(sameboy_root / "BootROMs") + "/",
                "-o",
                str(object_path),
                str(sameboy_root / "BootROMs/sgb2_boot.asm"),
            ],
            check=True,
        )
        subprocess.run(["rgblink", "-x", "-o", str(output_path), str(object_path)], check=True)
        output_size = output_path.stat().st_size
        output_hash = sha256(output_path)
        if output_size != EXPECTED_SIZE or output_hash != EXPECTED_OUTPUT_SHA256:
            raise ValueError(
                "rebuilt SameBoot resource mismatch: "
                f"size={output_size} sha256={output_hash}"
            )
        if not expected_resource.is_file() or expected_resource.read_bytes() != output_path.read_bytes():
            raise ValueError("packaged SameBoot resource differs from the reproducible build output")
    return {
        "source_files": len(CLOSURE_FILES),
        "closure_sha256": closure_hash,
        "output_size": EXPECTED_SIZE,
        "output_sha256": EXPECTED_OUTPUT_SHA256,
        "rgbds_version": EXPECTED_RGBDS_VERSION,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "project_root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
    )
    args = parser.parse_args()
    try:
        result = verify(args.project_root.resolve())
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"SameBoot verification failed: {error}", file=sys.stderr)
        return 1
    print(
        "SameBoot resource verified: "
        f"source_files={result['source_files']} closure_sha256={result['closure_sha256']} "
        f"output_size={result['output_size']} output_sha256={result['output_sha256']} "
        f"rgbds={result['rgbds_version']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
