# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Small operational CLI for the independently installed API server."""

from __future__ import annotations

import argparse
import ipaddress
import math
import os
import pwd
import re
import secrets
import shlex
import socket
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from collections.abc import Mapping
from urllib.error import URLError
from urllib.request import urlopen

from .errors import LeagueError
from .network_mode import (
    NETWORK_MODE_PLAIN,
    NETWORK_MODE_TLS,
    configured_network_mode,
)

API_SERVICE_NAME = "integral-server.service"
MEDIA_RELAY_SERVICE_NAME = "integral-server-media-relay.service"
SERVICE_NAMES = (API_SERVICE_NAME, MEDIA_RELAY_SERVICE_NAME)
SERVICE_USER = "integral-server"
DEFAULT_CONFIG = Path("/etc/integral-server/integral-server.env")
DEFAULT_STORAGE = Path("/var/lib/integral-server")
DEFAULT_MOBILE_PACKAGE_ROOT = DEFAULT_STORAGE / "mobile-packages"
UNINSTALL_HELPER = "/usr/local/libexec/integral-server-uninstall"
ENV_NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")
DEFAULT_EDITOR = "vi"
DEFAULT_CERT_RELOAD_INTERVAL_SECONDS = 60.0


class ConfigurationError(ValueError):
    pass


def command_uninstall(_args: argparse.Namespace) -> int:
    if not require_system_privileges("uninstall"):
        return 1
    try:
        os.execv(UNINSTALL_HELPER, [UNINSTALL_HELPER])
    except OSError as error:
        raise ConfigurationError(
            f"cannot start uninstall helper {UNINSTALL_HELPER}: {error}"
        ) from error
    return 2


def _configured_admin_url(values: dict[str, str]) -> tuple[str, str]:
    required = (
        "INTEGRAL_EMULATOR_API_HOST",
        "INTEGRAL_EMULATOR_API_PORT",
        "INTEGRAL_EMULATOR_PUBLIC_BASE_PATH",
        "INTEGRAL_EMULATOR_ADMIN_PASSWORD",
    )
    missing = [name for name in required if name not in values]
    if missing:
        raise ConfigurationError(
            "configuration value is missing: " + ", ".join(missing)
        )
    host = values["INTEGRAL_EMULATOR_API_HOST"].strip()
    if not host:
        raise ConfigurationError("INTEGRAL_EMULATOR_API_HOST must not be empty")
    try:
        port = int(values["INTEGRAL_EMULATOR_API_PORT"])
    except ValueError as error:
        raise ConfigurationError(
            "INTEGRAL_EMULATOR_API_PORT must be an integer"
        ) from error
    if not 1 <= port <= 65535:
        raise ConfigurationError("API port must be between 1 and 65535")

    from .api import normalized_public_base_path

    base_path = normalized_public_base_path(
        values["INTEGRAL_EMULATOR_PUBLIC_BASE_PATH"]
    )
    password = values["INTEGRAL_EMULATOR_ADMIN_PASSWORD"]
    if not password:
        raise ConfigurationError("INTEGRAL_EMULATOR_ADMIN_PASSWORD is not configured")

    browser_host = "127.0.0.1" if host in {"0.0.0.0", "::"} else host
    try:
        address = ipaddress.ip_address(browser_host)
    except ValueError:
        url_host = browser_host
    else:
        url_host = f"[{browser_host}]" if address.version == 6 else browser_host
    return f"http://{url_host}:{port}{base_path}/admin", password


def _desktop_environment_available(environ: Mapping[str, str]) -> bool:
    if any(environ.get(name) for name in ("SSH_CONNECTION", "SSH_CLIENT", "SSH_TTY")):
        return False
    if sys.platform == "darwin":
        return bool(environ.get("TERM_PROGRAM") or environ.get("__CFBundleIdentifier"))
    return bool(environ.get("DISPLAY") or environ.get("WAYLAND_DISPLAY"))


def _open_admin_browser(url: str) -> tuple[bool, str]:
    if not _desktop_environment_available(os.environ):
        return False, "no desktop environment is available"
    opener = "open" if sys.platform == "darwin" else "xdg-open"
    command = [opener, url]
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        sudo_user = os.environ.get("SUDO_USER", "")
        if not sudo_user or sudo_user == "root":
            return False, "the invoking desktop user could not be identified"
        try:
            desktop_user = pwd.getpwnam(sudo_user)
        except KeyError:
            return False, "the invoking desktop user could not be identified"
        if desktop_user.pw_uid == 0:
            return False, "the invoking desktop user could not be identified"
        command = ["sudo", "-u", sudo_user, "--", opener, url]
    try:
        completed = subprocess.run(
            command,
            check=False,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        return False, "the default browser could not be started"
    if completed.returncode != 0:
        return False, "the default browser could not be started"
    return True, ""


def read_environment(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ConfigurationError(f"cannot read {path}: {error}") from error
    for line_number, raw_line in enumerate(lines, 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].lstrip()
        if "=" not in line:
            raise ConfigurationError(f"{path}:{line_number}: expected NAME=VALUE")
        name, value = line.split("=", 1)
        name = name.strip()
        value = value.strip()
        if not ENV_NAME.fullmatch(name):
            raise ConfigurationError(f"{path}:{line_number}: invalid variable name")
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[name] = value
    return values


def apply_environment(path: Path, *, required: bool) -> dict[str, str]:
    if not path.exists():
        if required:
            raise ConfigurationError(f"configuration file not found: {path}")
        return {}
    values = read_environment(path)
    for name, value in values.items():
        os.environ.setdefault(name, value)
    return values


def resolved_runtime(args: argparse.Namespace) -> tuple[str, int, Path]:
    host = args.host or os.environ.get("INTEGRAL_EMULATOR_API_HOST", "127.0.0.1")
    raw_port = args.port
    if raw_port is None:
        raw_port = os.environ.get("INTEGRAL_EMULATOR_API_PORT", "8080")
    try:
        port = int(raw_port)
    except (TypeError, ValueError) as error:
        raise ConfigurationError("INTEGRAL_EMULATOR_API_PORT must be an integer") from error
    if not 1 <= port <= 65535:
        raise ConfigurationError("API port must be between 1 and 65535")
    storage_value = args.storage_root or os.environ.get(
        "INTEGRAL_EMULATOR_STORAGE_ROOT", str(DEFAULT_STORAGE)
    )
    storage = Path(storage_value).expanduser().resolve()
    if not host.strip():
        raise ConfigurationError("API host must not be empty")
    return host.strip(), port, storage


def write_initial_configuration(path: Path, storage: Path) -> bool:
    if path.exists():
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    password = secrets.token_urlsafe(32)
    content = "\n".join(
        (
            "# INTEGRAL EMULATOR standalone server configuration",
            "# Keep HTTP on loopback when a reverse proxy provides public HTTPS.",
            "INTEGRAL_EMULATOR_API_HOST=127.0.0.1",
            "INTEGRAL_EMULATOR_API_PORT=8080",
            "INTEGRAL_EMULATOR_NETWORK_MODE=tls",
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_HOST=127.0.0.1",
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PUBLIC_HOST=localhost",
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PORT=25164",
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_FILE=/etc/integral-server/media-relay.crt",
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_KEY_FILE=/etc/integral-server/media-relay.key",
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_RELOAD_INTERVAL_SECONDS=60",
            f"INTEGRAL_EMULATOR_STORAGE_ROOT={storage}",
            f"INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT={storage / 'mobile-packages'}",
            "INTEGRAL_EMULATOR_PUBLIC_BASE_PATH=",
            f"INTEGRAL_EMULATOR_ADMIN_PASSWORD={password}",
            "# Set to 0 to require a match in config/allowed_roms/*.json.",
            "INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS=1",
            "# Set to 1 to allow users to register themselves through the public API.",
            "INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION=0",
            "# Set to 1 to allow an explicit user-selected initial SAV during ROM registration.",
            "INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT=0",
            "# Total game durations in seconds. Heartbeats do not extend them.",
            "INTEGRAL_EMULATOR_GB_LOCAL_GAME_SECONDS=172800",
            "INTEGRAL_EMULATOR_GB_MOBILE_GAME_SECONDS=172800",
            "INTEGRAL_EMULATOR_N64_LOCAL_GAME_SECONDS=172800",
            "INTEGRAL_EMULATOR_LINK_CABLE_ROOM_GAME_SECONDS=3600",
            "INTEGRAL_EMULATOR_N64_ROOM_GAME_SECONDS=7200",
            "# Enabled ROOM numbers accept comma-separated values and inclusive ranges.",
            "INTEGRAL_EMULATOR_LINK_CABLE_ROOMS=1-16",
            "INTEGRAL_EMULATOR_N64_ROOMS=65-80",
            "",
        )
    )
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o640)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return True


def ensure_mobile_package_configuration(path: Path, storage: Path) -> None:
    values = read_environment(path)
    name = "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT"
    if name in values:
        return
    original = path.read_text(encoding="utf-8")
    suffix = "" if original.endswith("\n") else "\n"
    content = f"{original}{suffix}{name}={storage / 'mobile-packages'}\n"
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, path.stat().st_mode & 0o777)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def command_init(args: argparse.Namespace) -> int:
    config = Path(args.config).expanduser().resolve()
    requested_storage = (
        Path(args.storage_root).expanduser().resolve()
        if args.storage_root
        else DEFAULT_STORAGE
    )
    created = write_initial_configuration(config, requested_storage)
    values = read_environment(config)
    storage = Path(
        args.storage_root
        or values.get("INTEGRAL_EMULATOR_STORAGE_ROOT", str(DEFAULT_STORAGE))
    ).expanduser().resolve()
    ensure_mobile_package_configuration(config, storage)
    values = read_environment(config)
    storage.mkdir(parents=True, exist_ok=True)
    os.chmod(storage, 0o750)
    package_root = Path(
        values.get(
            "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT",
            str(storage / "mobile-packages"),
        )
    ).expanduser().resolve()
    package_root.mkdir(parents=True, exist_ok=True)
    os.chmod(package_root, 0o2750)
    print(f"configuration: {config} ({'created' if created else 'kept'})")
    print(f"storage: {storage} (ready)")
    print(f"GB Mobile packages: {package_root} (ready)")
    return 0


def command_run(args: argparse.Namespace) -> int:
    config = Path(args.config).expanduser().resolve()
    apply_environment(config, required=False)
    host, port, storage = resolved_runtime(args)
    network_mode = configured_network_mode()
    storage.mkdir(parents=True, exist_ok=True)
    from .api import run_server

    print(f"INTEGRAL EMULATOR API listening on http://{host}:{port}", flush=True)
    if network_mode == NETWORK_MODE_PLAIN:
        print(
            "WARNING: API credentials and data are not encrypted; "
            "use only on the same machine or a trusted LAN",
            flush=True,
        )
    run_server(host, port, storage)
    return 0


def resolved_media_relay() -> tuple[str, int, str, Path | None, Path | None, float]:
    transport = configured_network_mode()
    host = os.environ.get(
        "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_HOST", "127.0.0.1"
    ).strip()
    if not host:
        raise ConfigurationError("media relay host must not be empty")
    try:
        port = int(
            os.environ.get(
                "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PORT", "25164"
            )
        )
    except ValueError as error:
        raise ConfigurationError("media relay port must be an integer") from error
    if not 1 <= port <= 65535:
        raise ConfigurationError("media relay port must be between 1 and 65535")
    cert_value = os.environ.get(
        "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_FILE", ""
    ).strip()
    key_value = os.environ.get(
        "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_KEY_FILE", ""
    ).strip()
    cert_file = Path(cert_value).expanduser().absolute() if cert_value else None
    key_file = Path(key_value).expanduser().absolute() if key_value else None
    raw_reload_interval = os.environ.get(
        "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_RELOAD_INTERVAL_SECONDS",
        str(DEFAULT_CERT_RELOAD_INTERVAL_SECONDS),
    )
    try:
        reload_interval = float(raw_reload_interval)
    except ValueError as error:
        raise ConfigurationError(
            "media relay certificate reload interval must be a number"
        ) from error
    if not math.isfinite(reload_interval) or reload_interval <= 0:
        raise ConfigurationError(
            "media relay certificate reload interval must be positive"
        )
    return host, port, transport, cert_file, key_file, reload_interval


def command_media_relay(args: argparse.Namespace) -> int:
    config = Path(args.config).expanduser().resolve()
    apply_environment(config, required=True)
    host, port, transport, cert_file, key_file, reload_interval = resolved_media_relay()
    _api_host, _api_port, storage = resolved_runtime(args)
    from .n64_runtime_media_relay import (
        AuthenticatedN64RuntimeMediaRelay,
        ReloadingTLSContext,
    )

    relay = AuthenticatedN64RuntimeMediaRelay(
        [storage, storage / "servers" / "secondary"]
    )
    if transport == NETWORK_MODE_TLS:
        if cert_file is None or key_file is None:
            raise ConfigurationError(
                "TLS media relay requires certificate and private-key paths"
            )
        reloader = ReloadingTLSContext(cert_file, key_file, reload_interval)
        tls_context = reloader.current_context()
    else:
        tls_context = None
        reloader = None
    relay.serve(
        host,
        port,
        tls_context,
        transport,
        tls_context_reloader=reloader,
    )
    return 0


def command_doctor(args: argparse.Namespace) -> int:
    checks: list[tuple[bool, str]] = []
    config = Path(args.config).expanduser().resolve()
    checks.append(
        (
            sys.version_info >= (3, 11),
            f"Python {sys.version.split()[0]} (requires 3.11+)",
        )
    )
    try:
        values = apply_environment(config, required=True)
        checks.append((True, f"configuration readable: {config}"))
    except ConfigurationError as error:
        values = {}
        checks.append((False, str(error)))

    try:
        host, port, storage = resolved_runtime(args)
        socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
        checks.append((True, f"HTTP bind setting valid: {host}:{port}"))
        writable = storage.is_dir() and os.access(storage, os.R_OK | os.W_OK | os.X_OK)
        checks.append((writable, f"storage readable and writable: {storage}"))
    except (ConfigurationError, OSError) as error:
        host, port, storage = "127.0.0.1", 8080, DEFAULT_STORAGE
        checks.append((False, f"runtime configuration invalid: {error}"))

    admin_password = os.environ.get(
        "INTEGRAL_EMULATOR_ADMIN_PASSWORD",
        values.get("INTEGRAL_EMULATOR_ADMIN_PASSWORD", ""),
    )
    checks.append((bool(admin_password), "administrator password configured"))
    try:
        relay_host, relay_port, transport, cert_file, key_file, reload_interval = resolved_media_relay()
        socket.getaddrinfo(relay_host, relay_port, type=socket.SOCK_STREAM)
        public_host = os.environ.get(
            "INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PUBLIC_HOST", ""
        ).strip()
        checks.append((bool(public_host), "media relay public host configured"))
        checks.append(
            (True, f"media relay setting valid: {relay_host}:{relay_port} ({transport})")
        )
        if transport == NETWORK_MODE_TLS:
            checks.append(
                (
                    True,
                    f"media relay certificate reload interval: {reload_interval:g} seconds",
                )
            )
            try:
                certificate_ok = bool(
                    cert_file and cert_file.is_file() and os.access(cert_file, os.R_OK)
                )
            except OSError:
                certificate_ok = False
            try:
                private_key_ok = bool(
                    key_file and key_file.is_file() and os.access(key_file, os.R_OK)
                )
            except OSError:
                private_key_ok = False
            checks.append((certificate_ok, "media relay TLS certificate readable"))
            checks.append((private_key_ok, "media relay TLS private key readable"))
        elif transport == NETWORK_MODE_PLAIN:
            checks.append(
                (
                    True,
                    "WARNING: API and media relay transport is not encrypted; trusted LAN only",
                )
            )
    except Exception as error:
        checks.append((False, f"media relay configuration invalid: {error}"))
    try:
        from .mobile.package_installation import installed_packages

        package_root = _mobile_package_root(values, storage)
        readable = package_root.is_dir() and os.access(
            package_root, os.R_OK | os.X_OK
        )
        if not readable:
            raise ConfigurationError(
                f"GB Mobile package root is not readable: {package_root}"
            )
        packages = installed_packages(package_root)
        checks.append(
            (True, f"GB Mobile packages loadable: {len(packages)} installed")
        )
    except Exception as error:
        checks.append((False, f"GB Mobile package configuration invalid: {error}"))
    try:
        from .api import environment_boolean

        allow_unlisted = environment_boolean(
            "INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS", True
        )
        checks.append(
            (
                True,
                "ROM allowlist enforcement: "
                + ("disabled" if allow_unlisted else "enabled"),
            )
        )
        if not allow_unlisted:
            from .allowed_roms import allowed_roms

            count = len(allowed_roms())
            checks.append((count > 0, f"ROM catalogs loadable: {count} entries"))
        allow_initial_save_import = environment_boolean(
            "INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT", False
        )
        checks.append(
            (
                True,
                "user initial SAV import: "
                + ("enabled" if allow_initial_save_import else "disabled"),
            )
        )
        allow_self_registration = environment_boolean(
            "INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION", False
        )
        checks.append(
            (
                True,
                "self-registration: "
                + ("enabled" if allow_self_registration else "disabled"),
            )
        )
    except Exception as error:
        checks.append((False, f"ROM policy invalid: {error}"))

    health_url = f"http://{host}:{port}/health"
    try:
        with urlopen(health_url, timeout=1.0) as response:
            healthy = response.status == 200
        checks.append((healthy, f"running HTTP health endpoint: {health_url}"))
    except (OSError, URLError):
        print(f"[INFO] HTTP health endpoint is not currently reachable: {health_url}")

    for passed, message in checks:
        print(f"[{'PASS' if passed else 'FAIL'}] {message}")
    return 0 if all(passed for passed, _message in checks) else 1


def require_system_privileges(action: str) -> bool:
    if hasattr(os, "geteuid") and os.geteuid() != 0:
        print(
            f"{action} requires system privileges; run: sudo integral-server {action}",
            file=sys.stderr,
        )
        return False
    return True


def command_systemctl(args: argparse.Namespace) -> int:
    if args.command in {"start", "stop", "restart"} and not require_system_privileges(args.command):
        return 1
    command = ["systemctl", args.command]
    if args.command == "status":
        command.append("--no-pager")
    command.extend(SERVICE_NAMES)
    return subprocess.run(command, check=False).returncode


def command_logs(args: argparse.Namespace) -> int:
    command = ["journalctl"]
    for service_name in SERVICE_NAMES:
        command.extend(("-u", service_name))
    command.extend(("-n", str(args.lines)))
    if args.follow:
        command.append("--follow")
    else:
        command.append("--no-pager")
    return subprocess.run(command, check=False).returncode


def _configured_editor(environ: Mapping[str, str]) -> list[str]:
    for name in ("SUDO_EDITOR", "VISUAL", "EDITOR"):
        raw_value = environ.get(name, "").strip()
        if not raw_value:
            continue
        try:
            command = shlex.split(raw_value)
        except ValueError as error:
            raise ConfigurationError(f"{name} is not a valid editor command: {error}") from error
        if not command:
            raise ConfigurationError(f"{name} must name an editor")
        return command
    return [DEFAULT_EDITOR]


def _user_owns_writable_file(path: Path) -> bool:
    try:
        status = path.stat()
    except OSError:
        return False
    return status.st_uid == os.geteuid() and os.access(path, os.W_OK)


def command_edit_config(args: argparse.Namespace) -> int:
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        raise ConfigurationError(
            "edit-config must not run as root; sudoを付けずに実行してください: "
            "integral-server edit-config"
        )
    config = Path(args.config).expanduser().resolve()
    if _user_owns_writable_file(config):
        command = [*_configured_editor(os.environ), str(config)]
    else:
        command = ["sudoedit", "--", str(config)]
    try:
        completed = subprocess.run(command, check=False)
    except OSError as error:
        raise ConfigurationError(f"cannot start configuration editor: {error}") from error
    if completed.returncode != 0:
        print(
            f"configuration editor exited with status {completed.returncode}",
            file=sys.stderr,
        )
        return completed.returncode or 1
    read_environment(config)
    print(f"configuration syntax valid: {config}")
    print("Next steps:")
    print(shlex.join(["integral-server", "--config", str(config), "doctor"]))
    print(shlex.join(["sudo", "integral-server", "--config", str(config), "restart"]))
    return 0


def enforce_service_identity(action: str = "user-issue") -> None:
    if not hasattr(os, "geteuid"):
        raise ConfigurationError(f"{action} requires a POSIX service account")
    effective_uid = os.geteuid()
    try:
        current_user = pwd.getpwuid(effective_uid).pw_name
    except KeyError as error:
        raise ConfigurationError("cannot identify the current operating-system user") from error
    if current_user == SERVICE_USER:
        return
    if effective_uid != 0:
        raise ConfigurationError(
            f"{action} must run as root or {SERVICE_USER}; "
            f"use sudo integral-server {action}"
        )
    try:
        service = pwd.getpwnam(SERVICE_USER)
    except KeyError as error:
        raise ConfigurationError(f"service user not found: {SERVICE_USER}") from error
    try:
        os.initgroups(SERVICE_USER, service.pw_gid)
        os.setgid(service.pw_gid)
        os.setuid(service.pw_uid)
    except OSError as error:
        raise ConfigurationError(
            f"cannot switch to service user {SERVICE_USER}: {error}"
        ) from error
    if os.geteuid() != service.pw_uid:
        raise ConfigurationError(f"failed to switch to service user {SERVICE_USER}")


def _existing_authority_database(config: Path):
    values = read_environment(config)
    storage = Path(
        values.get("INTEGRAL_EMULATOR_STORAGE_ROOT", str(DEFAULT_STORAGE))
    ).expanduser().resolve()

    from .database import AuthorityDatabase, DatabaseBaselineError

    database = AuthorityDatabase(storage)
    try:
        database_status = database.path.lstat()
    except FileNotFoundError as error:
        raise ConfigurationError(
            f"authority database not found; refusing to create it: {database.path}"
        ) from error
    except OSError as error:
        raise ConfigurationError(f"cannot inspect authority database: {error}") from error
    if stat.S_ISLNK(database_status.st_mode) or not stat.S_ISREG(database_status.st_mode):
        raise ConfigurationError(f"authority database is not a regular file: {database.path}")
    try:
        database.initialize()
    except DatabaseBaselineError as error:
        raise ConfigurationError(str(error)) from error
    return database


def command_user_issue(args: argparse.Namespace) -> int:
    if not sys.stdout.isatty():
        raise ConfigurationError(
            "user-issue requires an interactive output terminal"
        )
    config = Path(args.config).expanduser().resolve()
    enforce_service_identity("user-issue")

    from .sqlite_auth import SQLiteAuthService
    from .sqlite_repositories import SQLiteAuthRepository
    from .user_issuance import issue_user

    database = _existing_authority_database(config)
    issued = issue_user(
        SQLiteAuthService(SQLiteAuthRepository(database)),
        args.username,
        email=args.email,
    )
    print(f"username: {issued.user.username}")
    print(f"user_id: {issued.user.id}")
    if issued.user.email:
        print(f"email: {issued.user.email}")
    print(f"initial_password: {issued.initial_password}")
    return 0


def command_user_password_reset(args: argparse.Namespace) -> int:
    if not sys.stdout.isatty():
        raise ConfigurationError(
            "user-password-reset requires an interactive output terminal"
        )
    config = Path(args.config).expanduser().resolve()
    enforce_service_identity("user-password-reset")

    from .sqlite_auth import SQLiteAuthService
    from .sqlite_repositories import SQLiteAuthRepository
    from .user_issuance import reset_user_password

    database = _existing_authority_database(config)
    reset = reset_user_password(
        SQLiteAuthService(SQLiteAuthRepository(database)), args.username
    )
    print(f"username: {reset.user.username}")
    print(f"user_id: {reset.user.id}")
    print(f"temporary_password: {reset.temporary_password}")
    return 0


def command_open_admin(args: argparse.Namespace) -> int:
    if not sys.stdout.isatty():
        raise ConfigurationError(
            "open-admin refuses to display the administrator password when stdout is not a terminal"
        )
    config = Path(args.config).expanduser().resolve()
    url, password = _configured_admin_url(read_environment(config))
    print(f"Administrator URL: {url}")
    print(f"Administrator password: {password}")
    opened, reason = _open_admin_browser(url)
    if not opened:
        print(f"Browser was not opened: {reason}.")
    return 0


def _mobile_package_root(values: dict[str, str], storage: Path) -> Path:
    value = values.get(
        "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT",
        os.environ.get(
            "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT",
            str(storage / "mobile-packages"),
        ),
    )
    return Path(value).expanduser().resolve()


def command_mobile_package(args: argparse.Namespace) -> int:
    config = Path(args.config).expanduser().resolve()
    values = apply_environment(config, required=True)
    storage = Path(
        values.get("INTEGRAL_EMULATOR_STORAGE_ROOT", str(DEFAULT_STORAGE))
    ).expanduser().resolve()
    package_root = _mobile_package_root(values, storage)
    from .mobile.package_installation import install_package_archive, installed_packages

    if args.package_command == "list":
        packages = installed_packages(package_root)
        if not packages:
            print("No GB Mobile packages installed.")
        for package_id, release_id, display_name in packages:
            print(f"{package_id}\t{release_id}\t{display_name}")
        return 0

    if not require_system_privileges("mobile-package install"):
        return 1
    packages = install_package_archive(
        Path(args.archive), package_root, replace=args.replace
    )
    for package_id, release_id, display_name in packages:
        print(f"installed: {package_id} {release_id} ({display_name})")
    active = subprocess.run(
        ["systemctl", "is-active", "--quiet", API_SERVICE_NAME], check=False
    ).returncode == 0
    if active:
        restarted = subprocess.run(
            ["systemctl", "restart", API_SERVICE_NAME], check=False
        ).returncode
        if restarted:
            print("package installed, but service restart failed", file=sys.stderr)
            return restarted
        print("service restarted")
    else:
        print("service is not running; package will load on the next start")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="INTEGRAL EMULATOR API server application and service control"
    )
    parser.add_argument(
        "--config",
        default=os.environ.get("INTEGRAL_SERVER_CONFIG", str(DEFAULT_CONFIG)),
        help="environment configuration file",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="run the HTTP API in the foreground")
    run_parser.add_argument("--host")
    run_parser.add_argument("--port", type=int)
    run_parser.add_argument("--storage-root")
    run_parser.set_defaults(handler=command_run)

    relay_parser = subparsers.add_parser(
        "media-relay", help="run the configured media relay in the foreground"
    )
    relay_parser.add_argument("--storage-root")
    relay_parser.set_defaults(host=None, port=None, handler=command_media_relay)

    init_parser = subparsers.add_parser(
        "init", help="create initial configuration and storage"
    )
    init_parser.add_argument("--storage-root")
    init_parser.set_defaults(handler=command_init)

    doctor_parser = subparsers.add_parser(
        "doctor", help="check configuration and runtime prerequisites"
    )
    doctor_parser.add_argument("--host")
    doctor_parser.add_argument("--port", type=int)
    doctor_parser.add_argument("--storage-root")
    doctor_parser.set_defaults(handler=command_doctor)

    for command in ("start", "stop", "restart", "status"):
        control = subparsers.add_parser(command, help=f"{command} the system service")
        control.set_defaults(handler=command_systemctl)

    uninstall_parser = subparsers.add_parser(
        "uninstall", help="remove the application while preserving persistent data"
    )
    uninstall_parser.set_defaults(handler=command_uninstall)

    logs_parser = subparsers.add_parser("logs", help="show system service logs")
    logs_parser.add_argument("-n", "--lines", type=int, default=100)
    logs_parser.add_argument("-f", "--follow", action="store_true")
    logs_parser.set_defaults(handler=command_logs)

    edit_config_parser = subparsers.add_parser(
        "edit-config", help="safely edit and validate the server configuration"
    )
    edit_config_parser.set_defaults(handler=command_edit_config)

    user_issue_parser = subparsers.add_parser(
        "user-issue", help="issue a login ID with a generated initial password"
    )
    user_issue_parser.add_argument("username", help="login ID to issue")
    user_issue_parser.add_argument("--email", default="", help="optional email address")
    user_issue_parser.set_defaults(handler=command_user_issue)

    password_reset_parser = subparsers.add_parser(
        "user-password-reset",
        help="reset a login ID to a generated temporary password",
    )
    password_reset_parser.add_argument("username", help="login ID to reset")
    password_reset_parser.set_defaults(handler=command_user_password_reset)

    open_admin_parser = subparsers.add_parser(
        "open-admin", help="show and open the configured local administrator UI"
    )
    open_admin_parser.set_defaults(handler=command_open_admin)

    package_parser = subparsers.add_parser(
        "mobile-package", help="manage optional GB Mobile packages"
    )
    package_commands = package_parser.add_subparsers(
        dest="package_command", required=True
    )
    package_install = package_commands.add_parser(
        "install", help="validate and install a package archive"
    )
    package_install.add_argument("archive")
    package_install.add_argument(
        "--replace", action="store_true", help="replace an installed package"
    )
    package_install.set_defaults(handler=command_mobile_package)
    package_list = package_commands.add_parser(
        "list", help="list installed packages"
    )
    package_list.set_defaults(handler=command_mobile_package)
    return parser


def normalized_argv(argv: list[str]) -> list[str]:
    if not argv:
        return ["run"]
    commands = {
        "run", "media-relay", "init", "doctor", "start", "stop", "restart", "status", "logs",
        "mobile-package", "user-issue", "user-password-reset", "open-admin",
        "edit-config", "uninstall",
    }
    if argv[0] in commands or argv[0] in {"-h", "--help", "--config"}:
        return argv
    if argv[0].startswith("-"):
        return ["run", *argv]
    return argv


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(
        normalized_argv(list(sys.argv[1:] if argv is None else argv))
    )
    try:
        return int(args.handler(args))
    except (ConfigurationError, LeagueError) as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
