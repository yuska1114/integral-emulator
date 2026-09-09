# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""External transport mode shared by the API and media relay."""

from __future__ import annotations

import os
from collections.abc import Mapping

from .errors import ValidationError


NETWORK_MODE_ENV = "INTEGRAL_EMULATOR_NETWORK_MODE"
NETWORK_MODE_TLS = "tls"
NETWORK_MODE_PLAIN = "plain"
NETWORK_MODES = {NETWORK_MODE_TLS, NETWORK_MODE_PLAIN}


def configured_network_mode(environ: Mapping[str, str] | None = None) -> str:
    values = os.environ if environ is None else environ
    value = values.get(NETWORK_MODE_ENV, NETWORK_MODE_TLS).strip()
    if value not in NETWORK_MODES:
        raise ValidationError(f"{NETWORK_MODE_ENV} must be tls or plain")
    return value
