# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Command line entry point for the INTEGRAL EMULATOR server application."""

from __future__ import annotations

from .server_cli import main


if __name__ == "__main__":
    raise SystemExit(main())
