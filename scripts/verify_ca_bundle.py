#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify that a PEM file can be loaded as a non-empty CA bundle."""

from __future__ import annotations

import argparse
import ssl
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    try:
        context.load_verify_locations(cafile=str(args.bundle))
    except (OSError, ssl.SSLError) as error:
        raise SystemExit(f"CA bundle validation failed: {error}") from error
    certificates = context.get_ca_certs()
    if not certificates:
        raise SystemExit("CA bundle validation failed: no certificates")
    print(f"Verified CA bundle: {len(certificates)} certificates")


if __name__ == "__main__":
    main()
