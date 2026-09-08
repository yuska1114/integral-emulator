# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Atomic file operations for SAV data and runtime artifacts."""

from __future__ import annotations

from contextlib import contextmanager
import os
import tempfile
import time
from pathlib import Path

from .observability import TimingMetrics

if os.name == "nt":
    import msvcrt
else:
    import fcntl


class LeagueStorage:
    def __init__(self, root: Path | str, *, metrics: TimingMetrics | None = None):
        self.root = Path(root)
        self.metrics = metrics
        self.data_dir = self.root / "data"
        self.saves_dir = self.root / "saves"
        self.backups_dir = self.root / "save_backups"
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.saves_dir.mkdir(parents=True, exist_ok=True)
        self.backups_dir.mkdir(parents=True, exist_ok=True)

    @contextmanager
    def exclusive_lock(self, name: str):
        path = self.data_dir / f"{name}.lock"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a+b") as handle:
            wait_started = time.perf_counter()
            if os.name == "nt":
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK, 1)
            else:
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            self._record("file_lock_wait", name, wait_started)
            hold_started = time.perf_counter()
            try:
                yield
            finally:
                self._record("file_lock_hold", name, hold_started)
                if os.name == "nt":
                    handle.seek(0)
                    msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    fcntl.flock(handle.fileno(), fcntl.LOCK_UN)

    def atomic_write_bytes(
        self, path: Path, data: bytes, *, metric_name: str = "binary"
    ) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        handle = tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{path.name}.",
            suffix=".tmp",
            dir=path.parent,
            delete=False,
        )
        temp_path = Path(handle.name)
        try:
            started = time.perf_counter()
            handle.write(data)
            self._record("file_write", metric_name, started, byte_count=len(data))
            started = time.perf_counter()
            handle.flush()
            os.fsync(handle.fileno())
            self._record("file_fsync", metric_name, started, byte_count=len(data))
            handle.close()
            started = time.perf_counter()
            os.replace(temp_path, path)
            self._record("file_replace", metric_name, started, byte_count=len(data))
        finally:
            if not handle.closed:
                handle.close()
            if temp_path.exists():
                temp_path.unlink()

    def read_bytes(self, relative_path: str) -> bytes:
        return self.resolve_relative(relative_path).read_bytes()

    def resolve_relative(self, relative_path: str) -> Path:
        path = (self.root / relative_path).resolve()
        root = self.root.resolve()
        if root not in path.parents and path != root:
            raise ValueError("path escapes storage root")
        return path

    def _record(
        self,
        category: str,
        name: str,
        started: float,
        *,
        byte_count: int = 0,
    ) -> None:
        if self.metrics is not None:
            self.metrics.record(
                category,
                name,
                (time.perf_counter() - started) * 1000.0,
                byte_count=byte_count,
            )
