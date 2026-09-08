# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Explicit GB Runtime link communication modes."""

from __future__ import annotations

import json
from enum import StrEnum
from pathlib import Path
from typing import Any

from .errors import ValidationError


class GBRuntimeLinkMode(StrEnum):
    BATTLE = "battle"
    TRADE = "trade"


GB_RUNTIME_LINK_RUNTIME = "gb_runtime"


LINK_MODE_LABELS = {
    GBRuntimeLinkMode.BATTLE.value: "Battle Mode",
    GBRuntimeLinkMode.TRADE.value: "Trade Mode",
}
DEFAULT_LINK_MACRO_CONFIG_PATH = (
    Path(__file__).resolve().parents[2] / "config" / "gb_runtime_link_macros.json"
)
ALLOWED_MACRO_KEYS = frozenset(".UDLRABSTudlrabst-_|")


def normalize_link_mode(value: str) -> str:
    if value not in {
        GBRuntimeLinkMode.BATTLE.value,
        GBRuntimeLinkMode.TRADE.value,
    }:
        raise ValidationError("link mode must be battle or trade")
    return value


def effective_link_mode(value: str) -> str:
    """Return the internal transport/runtime mode for a visible ROOM mode."""
    normalize_link_mode(value)
    return GB_RUNTIME_LINK_RUNTIME


def gb_runtime_protocol_mode(value: str) -> str:
    """Return the executable protocol profile selected by a visible ROOM mode."""
    normalize_link_mode(value)
    return GBRuntimeLinkMode.BATTLE.value


def load_battle_turbo_macro_config(
    path: Path | str | None = None,
    *,
    rom_generation: str = "gen2",
    requested_mode: str = GBRuntimeLinkMode.BATTLE.value,
) -> dict[str, Any]:
    config_path = Path(path) if path is not None else DEFAULT_LINK_MACRO_CONFIG_PATH
    try:
        payload = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"GB Runtime link macro config is invalid: {error}") from error
    battle_turbo = payload.get("battle_turbo") if isinstance(payload, dict) else None
    if not isinstance(battle_turbo, dict):
        raise ValidationError("GB Runtime link macro config requires battle_turbo")
    generation_value = battle_turbo.get(rom_generation)
    if not isinstance(generation_value, dict):
        raise ValidationError(
            f"GB Runtime link macro config requires battle_turbo.{rom_generation}"
        )
    if rom_generation == "gen1":
        value = generation_value.get(normalize_link_mode(requested_mode))
        if not isinstance(value, dict):
            raise ValidationError(
                f"GB Runtime link macro config requires battle_turbo.gen1.{normalize_link_mode(requested_mode)}"
            )
    else:
        value = generation_value
    result: dict[str, Any] = {}
    sync_offsets: dict[str, int] = {}
    for role in ("player_a", "player_b"):
        macro = value.get(role)
        if not isinstance(macro, str) or not macro or len(macro) > 512:
            raise ValidationError(f"GB Runtime {role} Turbo macro must be 1 to 512 keys")
        if any(key not in ALLOWED_MACRO_KEYS for key in macro):
            raise ValidationError(f"GB Runtime {role} Turbo macro contains an unsupported key")
        if macro.count("|") != 1:
            raise ValidationError(f"GB Runtime {role} Turbo macro requires exactly one sync marker")
        sync_offsets[role] = macro.index("|")
        result[role] = macro.replace("|", "")
    for name, minimum, maximum in (
        ("step_frames", 1, 600),
        ("press_frames", 1, 600),
    ):
        number = value.get(name)
        if not isinstance(number, int) or isinstance(number, bool) or not minimum <= number <= maximum:
            raise ValidationError(f"GB Runtime {name} must be {minimum} to {maximum}")
        result[name] = number
    if result["press_frames"] > result["step_frames"]:
        raise ValidationError("GB Runtime press_frames cannot exceed step_frames")
    sync_every_step = value.get("sync_every_step")
    if not isinstance(sync_every_step, bool):
        raise ValidationError("GB Runtime sync_every_step must be true or false")
    result["sync_every_step"] = sync_every_step
    if sync_offsets["player_a"] != sync_offsets["player_b"]:
        raise ValidationError("GB Runtime Turbo sync markers must have the same timed position")
    result["sync_offset_frames"] = sync_offsets["player_a"] * result["step_frames"]
    for role in ("player_a", "player_b"):
        macro_duration = (len(result[role]) - 1) * result["step_frames"]
        if result["sync_offset_frames"] > macro_duration:
            raise ValidationError("GB Runtime Turbo sync offset must be within both macros")
    return result


def link_mode_profile(
    mode: str,
    macro_config_path: Path | str | None = None,
    *,
    rom_generation: str = "gen2",
    byte_preannounce: bool = False,
) -> dict[str, Any]:
    requested_mode = normalize_link_mode(mode)
    protocol_mode = gb_runtime_protocol_mode(requested_mode)
    emulator_args = [
        "--frames",
        "36000",
        "--byte-sync",
        "--byte-sync-paced-lockstep",
    ]
    if byte_preannounce:
        emulator_args.append("--byte-protocol-v2")
    return {
        "runtime": GB_RUNTIME_LINK_RUNTIME,
        "mode": protocol_mode,
        "requested_mode": requested_mode,
        "label": LINK_MODE_LABELS[requested_mode],
        "protocol_profile": "serial_paced",
        "save_policy": (
            "discard" if requested_mode == GBRuntimeLinkMode.BATTLE.value else "commit_changed"
        ),
        "automation_profile": "turbo_seating",
        "status": "usable",
        "byte_sync": True,
        "byte_sync_paced": True,
        "byte_sync_paced_lockstep": True,
        "byte_preannounce": byte_preannounce,
        "byte_completion_ack": False,
        "byte_protocol_v2": byte_preannounce,
        "turbo_while_byte_sync": False,
        "passive_serial_data_bit": False,
        "in_development": False,
        "emulator_args": emulator_args,
        "rom_generation": rom_generation,
        "turbo_macro": load_battle_turbo_macro_config(
            macro_config_path,
            rom_generation=rom_generation,
            requested_mode=requested_mode,
        ),
    }
