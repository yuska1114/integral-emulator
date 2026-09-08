# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""INTEGRAL EMULATOR domain foundation."""

from .api import LeagueApplication, create_handler
from .gb_runtime_host import GBRuntimeHostProcessManager
from .sessions import LinkSessionManager, LinkSessionStatus
from .storage import LeagueStorage

__all__ = [
    "LeagueApplication",
    "GBRuntimeHostProcessManager",
    "create_handler",
    "LeagueStorage",
    "LinkSessionManager",
    "LinkSessionStatus",
]
