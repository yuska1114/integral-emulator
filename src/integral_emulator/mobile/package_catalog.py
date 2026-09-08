# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Strict, game-agnostic GB Mobile data-package catalog."""

from __future__ import annotations

import base64
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import stat
from typing import Any, Iterable

from ..errors import ValidationError


PACKAGE_ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,63}\Z")
ADAPTER_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
RELEASE_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
ROLE_ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,63}\Z")
CONTENT_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
MAX_ARTIFACTS = 32
MAX_ARTIFACT_SIZE = 16 * 1024 * 1024
MAX_TOTAL_SIZE = 32 * 1024 * 1024
MAX_HTTP_ROUTES = 256
MAX_DNS_ROUTES = 64
MAX_RESPONSE_HEADERS = 32
MAX_STATE_FLAGS = 64
MAX_REQUEST_HEADER_PREDICATES = 16
MAX_REQUEST_BODY_SLICES = 8
SUPPORTED_RUNTIME_CAPABILITY = 3

PACKAGE_KEYS = {
    "schema_version", "package_id", "display_name", "adapter_id", "rom_titles",
    "active_release_id", "status", "provenance_path", "license_paths",
}
PACKAGE_OPTIONAL_KEYS = {"scenarios_path"}
SCENARIO_CATALOG_KEYS = {"schema_version", "default_scenario_id", "scenarios"}
SCENARIO_KEYS = {"scenario_id", "display_name", "release_id"}
RELEASE_KEYS = {
    "schema_version", "release_id", "created_at", "approved_by", "approved_at",
    "package_digest", "runtime_capability_version", "route_schema_version", "limits",
    "artifacts", "materializers", "rollback_release_id",
}
ARTIFACT_KEYS = {"role", "content_id", "path", "size", "sha256"}
LIMIT_KEYS = {"max_request_body", "max_sink_body", "max_connections"}
MATERIALIZER_KEYS = {
    "type", "source_content_id", "target_role", "target_content_id", "item_size",
}


def normalize_rom_title(value: str) -> str:
    """Normalize a persisted cartridge-header title for exact package matching."""

    if not isinstance(value, str):
        raise ValidationError("ROM header title must be text")
    normalized = " ".join(value.rstrip("\x00 ").strip().upper().split())
    if not 1 <= len(normalized) <= 20:
        raise ValidationError("ROM header title must be 1 to 20 characters")
    if any(ord(char) < 0x20 or ord(char) > 0x7E for char in normalized):
        raise ValidationError("ROM header title must contain printable ASCII only")
    return normalized


@dataclass(frozen=True)
class PackageArtifact:
    role: str
    content_id: str
    path: Path
    size: int
    sha256: str
    data: bytes

    def verified_bytes(self) -> bytes:
        return self.data

    def contract_dict(self) -> dict[str, Any]:
        data = self.verified_bytes()
        if self.role == "adapter_eeprom":
            if len(data) > 512:
                raise ValidationError("adapter EEPROM template exceeds 512 bytes")
            data = data + bytes([0xFF]) * (512 - len(data))
        return {
            "role": self.role,
            "content_id": self.content_id,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "data": base64.b64encode(data).decode("ascii"),
        }


@dataclass(frozen=True)
class RandomFixedSliceMaterializer:
    source_content_id: str
    target_role: str
    target_content_id: str
    item_size: int


@dataclass(frozen=True)
class MobilePackageRelease:
    package_id: str
    display_name: str
    adapter_id: str
    rom_titles: tuple[str, ...]
    release_id: str
    package_digest: str
    runtime_capability_version: int
    artifacts: tuple[PackageArtifact, ...]
    materializers: tuple[RandomFixedSliceMaterializer, ...] = ()
    scenario_id: str = "default"
    scenario_display_name: str = "DEFAULT"

    def runtime_contract(self) -> dict[str, Any]:
        source_ids = {item.source_content_id for item in self.materializers}
        artifacts = [
            artifact.contract_dict() for artifact in self.artifacts
            if artifact.content_id not in source_ids
        ]
        for materializer in self.materializers:
            source = next(
                artifact for artifact in self.artifacts
                if artifact.content_id == materializer.source_content_id
            )
            data = source.verified_bytes()
            count = len(data) // materializer.item_size
            index = secrets.randbelow(count)
            payload = data[index * materializer.item_size:(index + 1) * materializer.item_size]
            artifacts.append({
                "role": materializer.target_role,
                "content_id": materializer.target_content_id,
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "data": base64.b64encode(payload).decode("ascii"),
            })
        return {
            "schema_version": 2,
            "adapter_id": self.adapter_id,
            "package_id": self.package_id,
            "release_id": self.release_id,
            "package_digest": self.package_digest,
            "runtime_capability_version": self.runtime_capability_version,
            "artifacts": artifacts,
        }


class MobilePackageCatalog:
    """Load approved immutable releases without importing game-specific code."""

    def __init__(self, roots: Iterable[Path], *, runtime_capability_version: int = SUPPORTED_RUNTIME_CAPABILITY):
        self.roots = tuple(Path(root).resolve() for root in roots)
        self.runtime_capability_version = runtime_capability_version
        self._by_title: dict[str, list[dict[str, MobilePackageRelease]]] = {}
        self._default_scenario_by_title: dict[str, list[str]] = {}
        self._by_id: dict[str, MobilePackageRelease] = {}
        self.reload()

    def reload(self) -> None:
        by_title: dict[str, list[dict[str, MobilePackageRelease]]] = {}
        default_scenario_by_title: dict[str, list[str]] = {}
        by_id: dict[str, MobilePackageRelease] = {}
        for root in self.roots:
            if not root.exists():
                continue
            if not root.is_dir() or root.is_symlink():
                raise ValidationError(f"GB Mobile package root is not a regular directory: {root}")
            for package_file in sorted(root.glob("*/package.json")):
                loaded = self._load_package(root, package_file)
                if loaded is None:
                    continue
                release, scenario_releases = loaded
                if release.package_id in by_id:
                    raise ValidationError(f"duplicate active GB Mobile package ID: {release.package_id}")
                by_id[release.package_id] = release
                for title in release.rom_titles:
                    by_title.setdefault(title, []).append({
                        item.scenario_id: item for item in scenario_releases
                    })
                    default_scenario_by_title.setdefault(title, []).append(release.scenario_id)
        self._by_title = by_title
        self._default_scenario_by_title = default_scenario_by_title
        self._by_id = by_id

    def select(self, rom_header_title: str, scenario_id: str | None = None) -> MobilePackageRelease:
        title = normalize_rom_title(rom_header_title)
        package_matches = self._by_title.get(title, [])
        if len(package_matches) > 1:
            raise ValidationError("Multiple active GB Mobile packages match the ROM header title")
        matches = package_matches[0] if package_matches else {}
        if not matches:
            raise ValidationError("No approved GB Mobile package matches the ROM header title")
        selected_id = scenario_id or self._default_scenario_by_title[title][0]
        if not isinstance(selected_id, str) or not CONTENT_ID.fullmatch(selected_id):
            raise ValidationError("GB Mobile scenario ID is invalid")
        selected = matches.get(selected_id)
        if selected is None:
            raise ValidationError("GB Mobile scenario is not available for the ROM header title")
        return selected

    def scenarios(self, rom_header_title: str) -> tuple[dict[str, Any], ...]:
        title = normalize_rom_title(rom_header_title)
        package_matches = self._by_title.get(title, [])
        if len(package_matches) > 1:
            raise ValidationError("Multiple active GB Mobile packages match the ROM header title")
        matches = package_matches[0] if package_matches else {}
        if not matches:
            raise ValidationError("No approved GB Mobile package matches the ROM header title")
        default_id = self._default_scenario_by_title[title][0]
        return tuple({
            "scenario_id": release.scenario_id,
            "display_name": release.scenario_display_name,
            "release_id": release.release_id,
            "default": release.scenario_id == default_id,
        } for release in matches.values())

    def active_releases(self) -> tuple[MobilePackageRelease, ...]:
        return tuple(self._by_id[key] for key in sorted(self._by_id))

    def _load_package(self, root: Path, package_file: Path) -> tuple[MobilePackageRelease, tuple[MobilePackageRelease, ...]] | None:
        package_dir = package_file.parent
        package = _read_json_object(package_file)
        if not PACKAGE_KEYS <= set(package) or set(package) - PACKAGE_KEYS - PACKAGE_OPTIONAL_KEYS:
            raise ValidationError("package.json has unknown or missing fields")
        if package["schema_version"] != 2:
            raise ValidationError("package.json schema_version must be 2")
        package_id = _checked_id(package["package_id"], PACKAGE_ID, "package_id")
        if package_dir.name != package_id:
            raise ValidationError("package directory name must equal package_id")
        display_name = _bounded_text(package["display_name"], "display_name", 1, 128)
        adapter_id = _checked_id(package["adapter_id"], ADAPTER_ID, "adapter_id")
        if adapter_id != "gb_mobile_v2":
            raise ValidationError("adapter_id must be gb_mobile_v2")
        status = package["status"]
        if status not in {"candidate", "approved", "disabled"}:
            raise ValidationError("invalid package status")
        titles_value = package["rom_titles"]
        if not isinstance(titles_value, list) or not 1 <= len(titles_value) <= 32:
            raise ValidationError("rom_titles must contain 1 to 32 entries")
        titles = tuple(normalize_rom_title(item) for item in titles_value)
        if len(set(titles)) != len(titles):
            raise ValidationError("duplicate normalized ROM title")
        provenance = _confined_regular_file(package_dir, package["provenance_path"])
        if provenance.stat().st_size == 0:
            raise ValidationError("provenance document must not be empty")
        licenses = package["license_paths"]
        if not isinstance(licenses, list) or not licenses:
            raise ValidationError("license_paths must not be empty")
        for license_path in licenses:
            if _confined_regular_file(package_dir, license_path).stat().st_size == 0:
                raise ValidationError("license document must not be empty")
        active_release_id = _checked_id(package["active_release_id"], RELEASE_ID, "active_release_id")
        if status != "approved":
            return None
        scenarios_path = package.get("scenarios_path")
        if scenarios_path is None:
            scenario_values = [{
                "scenario_id": "default",
                "display_name": "DEFAULT",
                "release_id": active_release_id,
            }]
            default_scenario_id = "default"
        else:
            scenario_file = _confined_regular_file(package_dir, scenarios_path)
            scenario_catalog = _read_json_object(scenario_file)
            _require_exact_keys(scenario_catalog, SCENARIO_CATALOG_KEYS, "scenario catalog")
            if scenario_catalog["schema_version"] != 1:
                raise ValidationError("scenario catalog schema_version must be 1")
            scenario_values = scenario_catalog["scenarios"]
            if not isinstance(scenario_values, list) or not 1 <= len(scenario_values) <= 16:
                raise ValidationError("scenario catalog must contain 1 to 16 scenarios")
            default_scenario_id = _checked_id(
                scenario_catalog["default_scenario_id"], CONTENT_ID, "default_scenario_id"
            )
        releases: list[MobilePackageRelease] = []
        scenario_ids: set[str] = set()
        for scenario in scenario_values:
            if not isinstance(scenario, dict):
                raise ValidationError("scenario must be an object")
            _require_exact_keys(scenario, SCENARIO_KEYS, "scenario")
            scenario_id = _checked_id(scenario["scenario_id"], CONTENT_ID, "scenario_id")
            if scenario_id in scenario_ids:
                raise ValidationError("duplicate scenario_id")
            scenario_ids.add(scenario_id)
            scenario_display_name = _bounded_text(
                scenario["display_name"], "scenario display_name", 1, 48
            )
            if any(ord(char) < 0x20 or ord(char) > 0x7E for char in scenario_display_name):
                raise ValidationError("scenario display_name must contain printable ASCII only")
            release_id = _checked_id(scenario["release_id"], RELEASE_ID, "scenario release_id")
            releases.append(self._load_release(
                package_dir=package_dir,
                package_id=package_id,
                display_name=display_name,
                adapter_id=adapter_id,
                titles=titles,
                release_id=release_id,
                scenario_id=scenario_id,
                scenario_display_name=scenario_display_name,
            ))
        if default_scenario_id not in scenario_ids:
            raise ValidationError("default_scenario_id is not present in scenarios")
        default_release = next(item for item in releases if item.scenario_id == default_scenario_id)
        if default_release.release_id != active_release_id:
            raise ValidationError("active_release_id must match the default scenario release")
        return default_release, tuple(releases)

    def _load_release(self, *, package_dir: Path, package_id: str, display_name: str,
                      adapter_id: str, titles: tuple[str, ...], release_id: str,
                      scenario_id: str, scenario_display_name: str) -> MobilePackageRelease:
        release_dir = package_dir / "releases" / release_id
        release_file = _confined_regular_file(package_dir, f"releases/{release_id}/release.json")
        release = _read_json_object(release_file)
        _require_exact_keys(release, RELEASE_KEYS, "release.json")
        if release["schema_version"] != 2 or release["release_id"] != release_id:
            raise ValidationError("release identity does not match active release")
        capability = _bounded_int(release["runtime_capability_version"], "runtime_capability_version", 2, 3)
        if capability > self.runtime_capability_version:
            raise ValidationError("package requires a newer GB Mobile Runtime capability")
        route_schema_version = _bounded_int(
            release["route_schema_version"], "route_schema_version", 1, 2
        )
        if capability != route_schema_version + 1:
            raise ValidationError("route schema and Runtime capability are incompatible")
        _bounded_text(release["created_at"], "created_at", 1, 64)
        _bounded_text(release["approved_by"], "approved_by", 1, 128)
        _bounded_text(release["approved_at"], "approved_at", 1, 64)
        limits = release["limits"]
        if not isinstance(limits, dict):
            raise ValidationError("limits must be an object")
        _require_exact_keys(limits, LIMIT_KEYS, "release limits")
        _bounded_int(limits["max_request_body"], "max_request_body", 0, 1024 * 1024)
        _bounded_int(limits["max_sink_body"], "max_sink_body", 0, 1024 * 1024)
        _bounded_int(limits["max_connections"], "max_connections", 1, 32)
        rollback = release["rollback_release_id"]
        if rollback is not None:
            _checked_id(rollback, RELEASE_ID, "rollback_release_id")
        artifact_values = release["artifacts"]
        if not isinstance(artifact_values, list) or not 1 <= len(artifact_values) <= MAX_ARTIFACTS:
            raise ValidationError("artifacts must contain 1 to 32 entries")
        artifacts: list[PackageArtifact] = []
        roles: set[str] = set()
        content_ids: set[str] = set()
        total = 0
        digest_entries: list[dict[str, Any]] = []
        for value in artifact_values:
            if not isinstance(value, dict):
                raise ValidationError("artifact must be an object")
            _require_exact_keys(value, ARTIFACT_KEYS, "artifact")
            role = _checked_id(value["role"], ROLE_ID, "artifact role")
            content_id = _checked_id(value["content_id"], CONTENT_ID, "content_id")
            if role in roles or content_id in content_ids:
                raise ValidationError("artifact roles and content IDs must be unique")
            roles.add(role)
            content_ids.add(content_id)
            size = _bounded_int(value["size"], "artifact size", 0, MAX_ARTIFACT_SIZE)
            digest = _checked_id(value["sha256"], SHA256, "artifact sha256")
            path, data = _verified_regular_file_bytes(
                release_dir, value["path"], size=size, digest=digest
            )
            total += size
            if total > MAX_TOTAL_SIZE:
                raise ValidationError("aggregate artifact size exceeds 32 MiB")
            artifacts.append(PackageArtifact(role, content_id, path, size, digest, data))
            digest_entries.append({key: value[key] for key in ("role", "content_id", "path", "size", "sha256")})
        for required in ("adapter_eeprom", "dns_routes", "http_routes"):
            if required not in roles:
                raise ValidationError(f"missing required artifact role: {required}")
        materializers = self._validate_materializers(release["materializers"], artifacts)
        package_digest = _checked_id(release["package_digest"], SHA256, "package_digest")
        calculated = hashlib.sha256(
            json.dumps(
                {"artifacts": digest_entries, "materializers": release["materializers"]},
                ensure_ascii=True, separators=(",", ":"), sort_keys=True,
            ).encode("utf-8")
        ).hexdigest()
        if calculated != package_digest:
            raise ValidationError("aggregate package digest mismatch")
        self._validate_routes(artifacts, limits, materializers, route_schema_version)
        return MobilePackageRelease(
            package_id, display_name, adapter_id, titles, release_id,
            package_digest, capability, tuple(artifacts), tuple(materializers),
            scenario_id, scenario_display_name,
        )

    def _validate_materializers(
        self, values: Any, artifacts: list[PackageArtifact]
    ) -> list[RandomFixedSliceMaterializer]:
        if not isinstance(values, list) or len(values) > 8:
            raise ValidationError("materializers must be an array of at most 8 entries")
        by_id = {artifact.content_id: artifact for artifact in artifacts}
        roles = {artifact.role for artifact in artifacts}
        targets: set[str] = set()
        sources: set[str] = set()
        result: list[RandomFixedSliceMaterializer] = []
        for value in values:
            if not isinstance(value, dict):
                raise ValidationError("materializer must be an object")
            _require_exact_keys(value, MATERIALIZER_KEYS, "materializer")
            if value["type"] != "random_fixed_slice":
                raise ValidationError("unsupported materializer type")
            source_id = _checked_id(value["source_content_id"], CONTENT_ID, "source_content_id")
            target_role = _checked_id(value["target_role"], ROLE_ID, "target_role")
            target_id = _checked_id(value["target_content_id"], CONTENT_ID, "target_content_id")
            item_size = _bounded_int(value["item_size"], "item_size", 1, 4096)
            source = by_id.get(source_id)
            if (
                source is None or not source.role.startswith("content_") or
                source_id in sources or target_id in by_id or target_id in targets or
                target_role in roles or source.size == 0 or source.size % item_size or
                source.size // item_size > 65536
            ):
                raise ValidationError("invalid random fixed-slice materializer")
            sources.add(source_id)
            targets.add(target_id)
            roles.add(target_role)
            result.append(RandomFixedSliceMaterializer(
                source_id, target_role, target_id, item_size
            ))
        return result

    def _validate_routes(
        self,
        artifacts: list[PackageArtifact],
        limits: dict[str, Any],
        materializers: list[RandomFixedSliceMaterializer],
        route_schema_version: int,
    ) -> None:
        by_role = {artifact.role: artifact for artifact in artifacts}
        content_ids = {artifact.content_id for artifact in artifacts}
        virtual_sizes = {
            materializer.target_content_id: materializer.item_size
            for materializer in materializers
        }
        content_ids.update(virtual_sizes)
        dns = _read_json_bytes(by_role["dns_routes"].data, "dns_routes")
        _require_exact_keys(dns, {"schema_version", "routes"}, "DNS routes")
        if dns["schema_version"] != 1 or not isinstance(dns["routes"], list) or len(dns["routes"]) > MAX_DNS_ROUTES:
            raise ValidationError("invalid DNS route table")
        dns_names: set[str] = set()
        for route in dns["routes"]:
            _require_exact_keys(route, {"name", "endpoint"}, "DNS route")
            name = _normalize_host(route["name"])
            endpoint = _bounded_text(route["endpoint"], "DNS endpoint", 1, 64)
            if name in dns_names or endpoint != "mobile-gateway":
                raise ValidationError("duplicate DNS route or forbidden endpoint")
            dns_names.add(name)
        http = _read_json_bytes(by_role["http_routes"].data, "http_routes")
        _require_exact_keys(http, {"schema_version", "initial_state", "routes"}, "HTTP routes")
        if http["schema_version"] != route_schema_version or not isinstance(http["routes"], list) or len(http["routes"]) > MAX_HTTP_ROUTES:
            raise ValidationError("invalid HTTP route table")
        initial_state = http["initial_state"]
        if not isinstance(initial_state, dict) or len(initial_state) > MAX_STATE_FLAGS:
            raise ValidationError("initial_state must be a bounded object")
        for key, value in initial_state.items():
            _checked_id(key, CONTENT_ID, "state flag")
            if not isinstance(value, bool):
                raise ValidationError("state flag values must be boolean")
        route_shapes: list[dict[str, Any]] = []
        referenced_content: set[str] = set()
        allowed_keys = {
            "host", "method", "path", "requires", "sets", "status", "headers", "body",
            "discard_body", "sink_body", "http_version",
        }
        if route_schema_version == 2:
            allowed_keys.update({
                "request_headers", "request_body", "max_uses", "same_body_replay",
            })
        for route in http["routes"]:
            if not isinstance(route, dict):
                raise ValidationError("HTTP route must be an object")
            if set(route) - allowed_keys or not {"host", "method", "path", "status"} <= set(route):
                raise ValidationError("HTTP route has unknown or missing fields")
            host = _normalize_host(route["host"])
            method = _bounded_text(route["method"], "HTTP method", 1, 8).upper()
            if method not in {"GET", "POST", "PUT"}:
                raise ValidationError("unsupported HTTP method")
            path = _normalize_http_path(route["path"])
            if route.get("http_version", "1.1") not in {"1.0", "1.1"}:
                raise ValidationError("unsupported HTTP response version")
            requires = _state_object(route.get("requires", {}), "requires")
            sets = _state_object(route.get("sets", {}), "sets")
            for state_name in (*requires, *sets):
                if state_name not in initial_state:
                    raise ValidationError("route references undeclared state flag")
            request_headers = _validate_request_headers(
                route.get("request_headers", []), route_schema_version
            )
            request_body = _validate_request_body(
                route.get("request_body"), route_schema_version,
                int(limits["max_request_body"]),
            )
            max_uses = route.get("max_uses")
            if route_schema_version == 2:
                if max_uses is None:
                    raise ValidationError("schema 2 routes require max_uses")
                _bounded_int(max_uses, "max_uses", 1, 16)
                if set(requires) != set(initial_state):
                    raise ValidationError("schema 2 routes must completely specify state")
            elif max_uses is not None:
                raise ValidationError("max_uses requires HTTP route schema 2")
            same_body_replay = route.get("same_body_replay", False)
            if not isinstance(same_body_replay, bool):
                raise ValidationError("same_body_replay must be boolean")
            if same_body_replay and request_body is None:
                raise ValidationError("same_body_replay requires request_body")
            route_shape = {
                "host": host,
                "method": method,
                "path": path,
                "requires": requires,
                "sets": sets,
                "request_headers": request_headers,
                "request_body": request_body,
            }
            for prior in route_shapes:
                if _route_predicates_overlap(prior, route_shape):
                    raise ValidationError("ambiguous or duplicate HTTP route")
            route_shapes.append(route_shape)
            _bounded_int(route["status"], "HTTP status", 100, 599)
            headers = route.get("headers", {})
            if not isinstance(headers, dict) or len(headers) > MAX_RESPONSE_HEADERS:
                raise ValidationError("response headers must be a bounded object")
            for header_name, header_value in headers.items():
                if not re.fullmatch(r"[A-Za-z0-9-]{1,64}", header_name):
                    raise ValidationError("invalid response header name")
                _bounded_text(header_value, "response header", 0, 1024)
                if header_name.lower() in {"set-cookie", "authorization", "proxy-authorization"}:
                    raise ValidationError("forbidden response header")
            body = route.get("body")
            if body is not None:
                if not isinstance(body, dict):
                    raise ValidationError("route body must be an object")
                if set(body) not in ({"fixed_base64"}, {"content_id", "offset", "length"}):
                    raise ValidationError("route body must use one approved primitive")
                if "fixed_base64" in body:
                    try:
                        fixed = base64.b64decode(body["fixed_base64"], validate=True)
                    except Exception as exc:
                        raise ValidationError("invalid fixed response Base64") from exc
                    if len(fixed) > MAX_ARTIFACT_SIZE:
                        raise ValidationError("fixed response exceeds hard limit")
                else:
                    content_id = _checked_id(body["content_id"], CONTENT_ID, "body content_id")
                    if content_id not in content_ids:
                        raise ValidationError("route references missing content")
                    referenced_content.add(content_id)
                    artifact_size = virtual_sizes.get(content_id)
                    if artifact_size is None:
                        artifact_size = next(
                            item.size for item in artifacts if item.content_id == content_id
                        )
                    offset = _bounded_int(body["offset"], "content offset", 0, artifact_size)
                    length = _bounded_int(body["length"], "content length", 0, artifact_size)
                    if offset + length > artifact_size:
                        raise ValidationError("content slice exceeds artifact")
            discard = route.get("discard_body", 0)
            sink = route.get("sink_body", 0)
            _bounded_int(discard, "discard_body", 0, int(limits["max_request_body"]))
            _bounded_int(sink, "sink_body", 0, int(limits["max_sink_body"]))
            if discard and sink:
                raise ValidationError("route cannot discard and sink the same body")
            if request_body is not None and request_body["max_length"] > max(discard, sink):
                raise ValidationError("request body predicate exceeds route body handling limit")
        if route_schema_version == 2:
            _validate_route_reachability(initial_state, route_shapes)
        materializer_sources = {item.source_content_id for item in materializers}
        content_artifacts = {
            item.content_id for item in artifacts
            if item.role.startswith("content_") and item.content_id not in materializer_sources
        }
        if content_artifacts - referenced_content:
            raise ValidationError("package contains unreferenced content artifacts")


def _validate_request_headers(value: Any, schema_version: int) -> tuple[dict[str, Any], ...]:
    if schema_version == 1:
        if value not in ([], None):
            raise ValidationError("request header predicates require HTTP route schema 2")
        return ()
    if not isinstance(value, list) or len(value) > MAX_REQUEST_HEADER_PREDICATES:
        raise ValidationError("request_headers must be a bounded array")
    result: list[dict[str, Any]] = []
    names: set[str] = set()
    allowed = {"name", "presence", "max_length", "value_exact", "value_prefix", "value_sha256"}
    for predicate in value:
        if not isinstance(predicate, dict) or set(predicate) - allowed or not {"name", "presence"} <= set(predicate):
            raise ValidationError("invalid request header predicate")
        name = _bounded_text(predicate["name"], "request header name", 1, 64).lower()
        if not re.fullmatch(r"[a-z0-9-]{1,64}", name) or name in names:
            raise ValidationError("invalid or duplicate request header predicate name")
        if name in {"host", "content-length", "connection", "proxy-authorization"}:
            raise ValidationError("request header predicate uses a reserved header")
        names.add(name)
        presence = predicate["presence"]
        if presence not in {"present", "absent"}:
            raise ValidationError("invalid request header presence predicate")
        matchers = [key for key in ("value_exact", "value_prefix", "value_sha256") if key in predicate]
        if len(matchers) > 1 or (presence == "absent" and (matchers or "max_length" in predicate)):
            raise ValidationError("invalid absent or multi-match header predicate")
        normalized: dict[str, Any] = {"name": name, "presence": presence}
        if presence == "present":
            maximum = _bounded_int(predicate.get("max_length", 2048), "request header max_length", 0, 2048)
            normalized["max_length"] = maximum
            if matchers:
                matcher = matchers[0]
                if matcher == "value_sha256":
                    normalized[matcher] = _checked_id(predicate[matcher], SHA256, "request header value_sha256")
                else:
                    text = _bounded_text(predicate[matcher], f"request header {matcher}", 0, 512)
                    if any(ord(char) < 0x20 or ord(char) > 0x7E for char in text):
                        raise ValidationError("request header matcher must be printable ASCII")
                    if len(text) > maximum:
                        raise ValidationError("request header matcher exceeds max_length")
                    normalized[matcher] = text
        result.append(normalized)
    return tuple(result)


def _validate_request_body(
    value: Any, schema_version: int, hard_limit: int
) -> dict[str, Any] | None:
    if value is None:
        return None
    if schema_version != 2 or not isinstance(value, dict):
        raise ValidationError("request body predicates require HTTP route schema 2")
    allowed = {"min_length", "max_length", "sha256", "slices"}
    if set(value) - allowed or not {"min_length", "max_length", "slices"} <= set(value):
        raise ValidationError("invalid request body predicate")
    minimum = _bounded_int(value["min_length"], "request body min_length", 0, hard_limit)
    maximum = _bounded_int(value["max_length"], "request body max_length", minimum, hard_limit)
    slices = value["slices"]
    if not isinstance(slices, list) or len(slices) > MAX_REQUEST_BODY_SLICES:
        raise ValidationError("request body slices must be a bounded array")
    normalized_slices: list[dict[str, Any]] = []
    occupied: list[tuple[int, int]] = []
    for item in slices:
        if not isinstance(item, dict) or set(item) != {"offset", "fixed_base64"}:
            raise ValidationError("invalid request body slice")
        offset = _bounded_int(item["offset"], "request body slice offset", 0, maximum)
        try:
            fixed = base64.b64decode(item["fixed_base64"], validate=True)
        except Exception as exc:
            raise ValidationError("invalid request body slice Base64") from exc
        if not fixed or len(fixed) > 64 or offset + len(fixed) > maximum:
            raise ValidationError("request body slice exceeds bounds")
        span = (offset, offset + len(fixed))
        if any(span[0] < other[1] and other[0] < span[1] for other in occupied):
            raise ValidationError("request body slices overlap")
        occupied.append(span)
        normalized_slices.append({"offset": offset, "fixed": fixed})
    result: dict[str, Any] = {
        "min_length": minimum,
        "max_length": maximum,
        "slices": tuple(normalized_slices),
    }
    if "sha256" in value:
        result["sha256"] = _checked_id(value["sha256"], SHA256, "request body sha256")
    return result


def _route_predicates_overlap(left: dict[str, Any], right: dict[str, Any]) -> bool:
    if any(left[key] != right[key] for key in ("host", "method", "path", "requires")):
        return False
    left_headers = {item["name"]: item for item in left["request_headers"]}
    right_headers = {item["name"]: item for item in right["request_headers"]}
    for name in left_headers.keys() & right_headers.keys():
        a, b = left_headers[name], right_headers[name]
        if {a["presence"], b["presence"]} == {"present", "absent"}:
            return False
        if a["presence"] == b["presence"] == "present":
            if "value_exact" in a and "value_exact" in b and a["value_exact"] != b["value_exact"]:
                return False
            if "value_sha256" in a and "value_sha256" in b and a["value_sha256"] != b["value_sha256"]:
                return False
    a_body, b_body = left["request_body"], right["request_body"]
    if a_body is not None and b_body is not None:
        if a_body["max_length"] < b_body["min_length"] or b_body["max_length"] < a_body["min_length"]:
            return False
        if "sha256" in a_body and "sha256" in b_body and a_body["sha256"] != b_body["sha256"]:
            return False
    return True


def _validate_route_reachability(
    initial_state: dict[str, bool], routes: list[dict[str, Any]]
) -> None:
    initial = frozenset(name for name, enabled in initial_state.items() if enabled)
    reachable_states = {initial}
    reachable_routes: set[int] = set()
    changed = True
    while changed:
        changed = False
        for index, route in enumerate(routes):
            required = frozenset(
                name for name, enabled in route["requires"].items() if enabled
            )
            if required not in reachable_states:
                continue
            reachable_routes.add(index)
            result = set(required)
            for name, enabled in route["sets"].items():
                if enabled:
                    result.add(name)
                else:
                    result.discard(name)
            result_state = frozenset(result)
            if result_state not in reachable_states:
                reachable_states.add(result_state)
                changed = True
    if len(reachable_routes) != len(routes):
        raise ValidationError("HTTP route table contains an unreachable route")


def _read_json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError(f"invalid GB Mobile package JSON: {path.name}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"GB Mobile package JSON must be an object: {path.name}")
    return value


def _read_json_bytes(data: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValidationError(f"invalid GB Mobile package JSON: {label}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"GB Mobile package JSON must be an object: {label}")
    return value


def _require_exact_keys(value: dict[str, Any], keys: set[str], label: str) -> None:
    if set(value) != keys:
        raise ValidationError(f"{label} has unknown or missing fields")


def _checked_id(value: Any, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or not pattern.fullmatch(value):
        raise ValidationError(f"invalid {label}")
    return value


def _bounded_text(value: Any, label: str, minimum: int, maximum: int) -> str:
    if not isinstance(value, str) or not minimum <= len(value) <= maximum:
        raise ValidationError(f"invalid {label}")
    return value


def _bounded_int(value: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValidationError(f"invalid {label}")
    return value


def _confined_regular_file(root: Path, relative: Any) -> Path:
    if not isinstance(relative, str) or not relative or "\\" in relative:
        raise ValidationError("artifact path must be a non-empty POSIX relative path")
    path_value = Path(relative)
    if path_value.is_absolute() or ".." in path_value.parts:
        raise ValidationError("artifact path escapes package root")
    root_resolved = root.resolve()
    candidate = root / path_value
    if candidate.is_symlink():
        raise ValidationError("artifact symlinks are forbidden")
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as exc:
        raise ValidationError("package artifact does not exist") from exc
    if resolved != root_resolved and root_resolved not in resolved.parents:
        raise ValidationError("artifact path escapes package root")
    if not resolved.is_file():
        raise ValidationError("package artifact must be a regular file")
    return resolved


def _verified_regular_file_bytes(
    root: Path, relative: Any, *, size: int, digest: str
) -> tuple[Path, bytes]:
    path = _confined_regular_file(root, relative)
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise ValidationError("package artifact cannot be opened safely") from exc
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_size != size:
            raise ValidationError("package artifact is not the approved regular file")
        chunks: list[bytes] = []
        remaining = size
        while remaining:
            chunk = os.read(descriptor, min(remaining, 1024 * 1024))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        data = b"".join(chunks)
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValidationError("package artifact size or hash mismatch")
        return path, data
    finally:
        os.close(descriptor)


def _normalize_host(value: Any) -> str:
    raw = _bounded_text(value, "host", 1, 253)
    if raw.endswith("."):
        raise ValidationError("DNS host must use canonical form without a trailing dot")
    host = raw.lower()
    if not re.fullmatch(r"[a-z0-9](?:[a-z0-9.-]{0,251}[a-z0-9])?", host) or ".." in host:
        raise ValidationError("invalid normalized host")
    return host


def _normalize_http_path(value: Any) -> str:
    path = _bounded_text(value, "HTTP path", 1, 1024)
    path_part = path.split("?", 1)[0]
    if (
        not path.startswith("/") or "#" in path or path.count("?") > 1 or
        "//" in path_part or ".." in path_part.split("/") or
        any(ord(char) < 0x21 or ord(char) > 0x7E for char in path)
    ):
        raise ValidationError("invalid exact HTTP path")
    return path


def _state_object(value: Any, label: str) -> dict[str, bool]:
    if not isinstance(value, dict) or len(value) > MAX_STATE_FLAGS:
        raise ValidationError(f"{label} must be a bounded state object")
    result: dict[str, bool] = {}
    for key, item in value.items():
        result[_checked_id(key, CONTENT_ID, "state flag")] = item
        if not isinstance(item, bool):
            raise ValidationError("state flag values must be boolean")
    return result
