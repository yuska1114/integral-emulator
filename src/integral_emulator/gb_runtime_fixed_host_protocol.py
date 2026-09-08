# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fail-closed control contract for the deployed GB fixed-Host v2 runtime.

The stable persisted protocol ID remains ``gb_runtime_fixed_host_v1`` while the required
product runtime build is ``integral-gb-runtime-fixed-host-v2``.
"""

from __future__ import annotations

import hashlib
import hmac
import json
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
from enum import StrEnum
from types import MappingProxyType
from typing import Callable, Mapping

from .errors import ValidationError
from .rom_metadata import normalize_rom_header_title, normalize_rom_platform
from .save_contract import normalize_game_type
from .security import issue_secret


GB_RUNTIME_FIXED_HOST_PROTOCOL_ID = "gb_runtime_fixed_host_v1"
GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE = "gb-runtime-fixed-host-media-v1"
GB_RUNTIME_FIXED_HOST_MANIFEST_SCHEMA = "gb-runtime-fixed-host-manifest-v1"
GB_RUNTIME_FIXED_HOST_RELAY_TICKET_MAX_SECONDS = 60
GB_RUNTIME_FIXED_HOST_ROLES = ("host", "remote")


class GBRuntimeFixedHostSessionState(StrEnum):
    PREFLIGHT = "PREFLIGHT"
    READY = "READY"
    RUNNING = "RUNNING"
    PAUSED_REMOTE = "PAUSED_REMOTE"
    FINISHED = "FINISHED"
    ABORTED = "ABORTED"


class GBRuntimeFixedHostTerminationReason(StrEnum):
    NORMAL = "normal"
    USER_LEFT = "user_left"
    HOST_DISCONNECT = "host_disconnect"
    REMOTE_TIMEOUT = "remote_timeout"
    RELAY_ERROR = "relay_error"
    RUNTIME_ERROR = "runtime_error"
    PROTOCOL_ERROR = "protocol_error"
    SAVE_VALIDATION_FAILED = "save_validation_failed"
    COMMIT_FAILED = "commit_failed"
    EXPIRED = "expired"


class GBRuntimeFixedHostSaveDecision(StrEnum):
    DISCARD = "discard"
    COMMIT_PAIR = "commit_pair"
    NONCOMMIT = "noncommit"


def _utc(value: datetime) -> datetime:
    if value.tzinfo is None:
        raise ValidationError("fixed Host timestamp must be timezone-aware")
    return value.astimezone(timezone.utc)


def _text(name: str, value: str, maximum: int = 128) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum:
        raise ValidationError(f"invalid fixed Host {name}")
    return value


@dataclass(frozen=True)
class GBRuntimeFixedHostManifest:
    session_id: str
    session_epoch: int
    host_user_id: str
    remote_user_id: str
    host_save_id: str
    remote_save_id: str
    host_game_type: str
    remote_game_type: str
    host_platform: str
    remote_platform: str
    host_rom_header_title: str
    remote_rom_header_title: str
    host_base_revision: int
    remote_base_revision: int
    requested_mode: str
    save_policy: str
    runtime_build_id: str
    protocol_id: str = GB_RUNTIME_FIXED_HOST_PROTOCOL_ID

    def validated(self) -> "GBRuntimeFixedHostManifest":
        for name in (
            "session_id",
            "host_user_id",
            "remote_user_id",
            "host_save_id",
            "remote_save_id",
            "host_game_type",
            "remote_game_type",
            "host_platform",
            "remote_platform",
            "host_rom_header_title",
            "remote_rom_header_title",
            "runtime_build_id",
        ):
            _text(name, getattr(self, name))
        for field_name in ("host_game_type", "remote_game_type"):
            value = getattr(self, field_name)
            try:
                normalized = normalize_game_type(value)
            except ValueError as error:
                raise ValidationError(str(error)) from error
            if value != normalized:
                raise ValidationError(f"{field_name} must be normalized")
        for field_name in ("host_platform", "remote_platform"):
            value = getattr(self, field_name)
            if value != normalize_rom_platform(value):
                raise ValidationError(f"{field_name} must be normalized")
        for field_name in ("host_rom_header_title", "remote_rom_header_title"):
            value = getattr(self, field_name)
            if value != normalize_rom_header_title(value):
                raise ValidationError(f"{field_name} must be normalized")
        if self.protocol_id != GB_RUNTIME_FIXED_HOST_PROTOCOL_ID:
            raise ValidationError("unsupported fixed Host protocol")
        if self.host_user_id == self.remote_user_id:
            raise ValidationError("fixed Host participants must be distinct")
        if self.host_save_id == self.remote_save_id:
            raise ValidationError("fixed Host saves must be distinct")
        if not isinstance(self.session_epoch, int) or self.session_epoch <= 0:
            raise ValidationError("invalid fixed Host session epoch")
        for revision in (self.host_base_revision, self.remote_base_revision):
            if not isinstance(revision, int) or revision < 0:
                raise ValidationError("invalid fixed Host base revision")
        expected_policy = {"battle": "discard", "trade": "commit_pair"}.get(
            self.requested_mode
        )
        if expected_policy is None or self.save_policy != expected_policy:
            raise ValidationError("fixed Host mode/save policy mismatch")
        return self

    def role_for(self, user_id: str) -> str:
        self.validated()
        if user_id == self.host_user_id:
            return "host"
        if user_id == self.remote_user_id:
            return "remote"
        raise ValidationError("user is not a fixed Host participant")

    @property
    def digest(self) -> str:
        self.validated()
        encoded = json.dumps(
            {
                "schema": GB_RUNTIME_FIXED_HOST_MANIFEST_SCHEMA,
                "protocol_id": self.protocol_id,
                "session_id": self.session_id,
                "session_epoch": self.session_epoch,
                "host_user_id": self.host_user_id,
                "remote_user_id": self.remote_user_id,
                "host_save_id": self.host_save_id,
                "remote_save_id": self.remote_save_id,
                "host_game_type": self.host_game_type,
                "remote_game_type": self.remote_game_type,
                "host_platform": self.host_platform,
                "remote_platform": self.remote_platform,
                "host_rom_header_title": self.host_rom_header_title,
                "remote_rom_header_title": self.remote_rom_header_title,
                "host_base_revision": self.host_base_revision,
                "remote_base_revision": self.remote_base_revision,
                "requested_mode": self.requested_mode,
                "save_policy": self.save_policy,
                "runtime_build_id": self.runtime_build_id,
            },
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        return hashlib.sha256(encoded).hexdigest()


@dataclass(frozen=True)
class GBRuntimeFixedHostRelayGrant:
    role: str
    user_id: str
    token_sha256: str
    session_epoch: int
    manifest_digest: str
    issued_at: datetime
    expires_at: datetime
    used: bool = False


@dataclass(frozen=True)
class GBRuntimeFixedHostRelayTicketSet:
    """Immutable ticket state; the future control plane must persist each update."""

    grants: Mapping[str, GBRuntimeFixedHostRelayGrant] = MappingProxyType({})

    def issue(
        self,
        manifest: GBRuntimeFixedHostManifest,
        user_id: str,
        *,
        now: datetime,
        ttl_seconds: int = 30,
        secret_issuer: Callable[[str], str] = issue_secret,
    ) -> tuple["GBRuntimeFixedHostRelayTicketSet", str, str]:
        manifest.validated()
        timestamp = _utc(now)
        if (
            not isinstance(ttl_seconds, int)
            or not 1 <= ttl_seconds <= GB_RUNTIME_FIXED_HOST_RELAY_TICKET_MAX_SECONDS
        ):
            raise ValidationError("invalid fixed Host relay ticket lifetime")
        role = manifest.role_for(user_id)
        if role in self.grants:
            raise ValidationError("fixed Host relay ticket already issued")
        token = secret_issuer("gbfixedticket")
        _text("relay ticket", token, 256)
        grants = dict(self.grants)
        grants[role] = GBRuntimeFixedHostRelayGrant(
            role=role,
            user_id=user_id,
            token_sha256=hashlib.sha256(token.encode("utf-8")).hexdigest(),
            session_epoch=manifest.session_epoch,
            manifest_digest=manifest.digest,
            issued_at=timestamp,
            expires_at=timestamp + timedelta(seconds=ttl_seconds),
        )
        return replace(self, grants=MappingProxyType(grants)), role, token

    def consume(
        self,
        manifest: GBRuntimeFixedHostManifest,
        role: str,
        scope: str,
        token: str,
        *,
        now: datetime,
    ) -> "GBRuntimeFixedHostRelayTicketSet":
        manifest.validated()
        timestamp = _utc(now)
        if role not in GB_RUNTIME_FIXED_HOST_ROLES or scope != GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE:
            raise ValidationError("invalid fixed Host relay role or scope")
        grant = self.grants.get(role)
        if not grant:
            raise ValidationError("fixed Host relay ticket not issued")
        if grant.used:
            raise ValidationError("fixed Host relay ticket already used")
        if timestamp >= grant.expires_at:
            raise ValidationError("fixed Host relay ticket expired")
        if (
            grant.session_epoch != manifest.session_epoch
            or not hmac.compare_digest(grant.manifest_digest, manifest.digest)
            or grant.user_id != (
                manifest.host_user_id if role == "host" else manifest.remote_user_id
            )
        ):
            raise ValidationError("fixed Host relay ticket binding mismatch")
        supplied = hashlib.sha256(token.encode("utf-8")).hexdigest()
        if not hmac.compare_digest(supplied, grant.token_sha256):
            raise ValidationError("fixed Host relay ticket invalid")
        grants = dict(self.grants)
        grants[role] = replace(grant, used=True)
        return replace(self, grants=MappingProxyType(grants))


def gb_runtime_fixed_host_save_decision(
    manifest: GBRuntimeFixedHostManifest,
    *,
    termination_reason: GBRuntimeFixedHostTerminationReason,
    host_finished: bool,
    remote_terminal_receipt: bool,
    both_candidates_present: bool,
    both_candidates_valid: bool,
    pair_commit_succeeded: bool,
) -> GBRuntimeFixedHostSaveDecision:
    """Return only pair-level decisions; no one-sided commit state exists."""

    manifest.validated()
    if manifest.save_policy == "discard":
        return GBRuntimeFixedHostSaveDecision.DISCARD
    if (
        termination_reason is GBRuntimeFixedHostTerminationReason.NORMAL
        and host_finished
        and remote_terminal_receipt
        and both_candidates_present
        and both_candidates_valid
        and pair_commit_succeeded
    ):
        return GBRuntimeFixedHostSaveDecision.COMMIT_PAIR
    return GBRuntimeFixedHostSaveDecision.NONCOMMIT
