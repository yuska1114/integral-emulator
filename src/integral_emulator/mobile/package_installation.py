# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Install validated GB Mobile package archives into an operator-owned root."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import tarfile
import tempfile

from ..errors import ValidationError
from .package_catalog import MobilePackageCatalog, PACKAGE_ID


def installed_packages(root: Path) -> tuple[tuple[str, str, str], ...]:
    catalog = MobilePackageCatalog([root])
    return tuple(
        (release.package_id, release.release_id, release.display_name)
        for release in catalog.active_releases()
    )


def install_package_archive(
    archive_path: Path, package_root: Path, *, replace: bool = False
) -> tuple[tuple[str, str, str], ...]:
    archive_path = archive_path.expanduser().resolve()
    package_root = package_root.expanduser().resolve()
    if not archive_path.is_file():
        raise ValidationError(f"GB Mobile package archive not found: {archive_path}")
    package_root.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix=".mobile-package-", dir=package_root) as directory:
        staging_root = Path(directory)
        _extract_archive(archive_path, staging_root)
        package_dirs = tuple(
            path for path in sorted(staging_root.iterdir()) if path.is_dir()
        )
        if not package_dirs or any(
            not PACKAGE_ID.fullmatch(path.name) or not (path / "package.json").is_file()
            for path in package_dirs
        ):
            raise ValidationError(
                "GB Mobile package archive must contain package_id directories"
            )
        if any(path.is_file() for path in staging_root.iterdir()):
            raise ValidationError("GB Mobile package archive has files outside a package")

        validated = installed_packages(staging_root)
        validated_ids = {package_id for package_id, _release_id, _name in validated}
        staged_ids = {path.name for path in package_dirs}
        if validated_ids != staged_ids:
            raise ValidationError("GB Mobile package archive contains an inactive package")

        conflicts = [path.name for path in package_dirs if (package_root / path.name).exists()]
        if conflicts and not replace:
            raise ValidationError(
                "GB Mobile package already installed: " + ", ".join(conflicts)
            )
        for source in package_dirs:
            destination = package_root / source.name
            replacement = package_root / f".{source.name}.new"
            backup = package_root / f".{source.name}.old"
            if replacement.exists() or replacement.is_symlink():
                shutil.rmtree(replacement)
            if backup.exists() or backup.is_symlink():
                shutil.rmtree(backup)
            shutil.copytree(source, replacement)
            if destination.exists():
                os.replace(destination, backup)
            try:
                os.replace(replacement, destination)
            except Exception:
                if backup.exists() and not destination.exists():
                    os.replace(backup, destination)
                raise
            if backup.exists():
                shutil.rmtree(backup)
        return validated


def _extract_archive(archive_path: Path, destination: Path) -> None:
    try:
        archive = tarfile.open(archive_path, "r:gz")
    except (OSError, tarfile.TarError) as error:
        raise ValidationError(f"cannot read GB Mobile package archive: {error}") from error
    with archive:
        members = archive.getmembers()
        if not members:
            raise ValidationError("GB Mobile package archive is empty")
        if len(members) > 2048:
            raise ValidationError("GB Mobile package archive has too many members")
        seen: set[tuple[str, ...]] = set()
        total_size = 0
        for member in members:
            relative = Path(member.name)
            if (
                relative.is_absolute()
                or not relative.parts
                or any(part in {"", ".", ".."} for part in relative.parts)
                or not PACKAGE_ID.fullmatch(relative.parts[0])
                or not (member.isdir() or member.isfile())
            ):
                raise ValidationError(f"invalid GB Mobile package archive member: {member.name}")
            if relative.parts in seen:
                raise ValidationError(f"duplicate GB Mobile package archive member: {member.name}")
            seen.add(relative.parts)
            total_size += member.size
            if total_size > 64 * 1024 * 1024:
                raise ValidationError("GB Mobile package archive exceeds 64 MiB")
            target = destination.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                target.chmod(0o750)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise ValidationError(f"cannot read archive member: {member.name}")
            with source, target.open("wb") as stream:
                shutil.copyfileobj(source, stream)
            target.chmod(0o640)
