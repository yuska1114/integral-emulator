# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Process supervision records for GB Runtime Link Host runs."""

from __future__ import annotations

import subprocess
import os
import signal
import time
from dataclasses import dataclass, replace
from typing import Any

from .errors import NotFoundError, ValidationError
from .models import now_iso
from .security import issue_secret
from .storage import LeagueStorage


@dataclass(frozen=True)
class GBRuntimeHostProcessRecord:
    id: str
    session_id: str
    command: list[str]
    status: str
    created_at: str
    updated_at: str
    pid: int | None = None
    exit_code: int | None = None
    dry_run: bool = True
    row_version: int = 0

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "GBRuntimeHostProcessRecord":
        normalized = {key: data.get(key) for key in cls.__dataclass_fields__}
        normalized["row_version"] = int(data.get("__row_version", data.get("row_version", 0)))
        return cls(**normalized)

    def to_dict(self) -> dict[str, Any]:
        data = self.__dict__.copy()
        data.pop("row_version", None)
        return data

    def to_public_dict(self) -> dict[str, Any]:
        data = self.to_dict()
        if data.get("command"):
            data["command"] = ["<redacted>"]
        return data


class GBRuntimeHostProcessManager:
    def __init__(self, storage: LeagueStorage):
        self.storage = storage
        self._processes: dict[str, subprocess.Popen] = {}

    def create_run(self, session_id: str, command: list[str], start: bool = False) -> GBRuntimeHostProcessRecord:
        self._validate_command(command)
        timestamp = now_iso()
        record = GBRuntimeHostProcessRecord(
            id=issue_secret("host"),
            session_id=session_id,
            command=self._redact_command(command),
            status="PLANNED",
            created_at=timestamp,
            updated_at=timestamp,
            dry_run=not start,
        )
        if start:
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            record = replace(record, status="RUNNING", pid=process.pid, dry_run=False, updated_at=now_iso())
        self._save(record)
        if start:
            self._processes[record.id] = process
        return record

    def get(self, host_process_id: str) -> GBRuntimeHostProcessRecord:
        data = self.storage.get_host_process(host_process_id)
        if not data:
            raise NotFoundError("host process not found")
        return GBRuntimeHostProcessRecord.from_dict(data)

    def mark_exited(self, host_process_id: str, exit_code: int) -> GBRuntimeHostProcessRecord:
        current = self.get(host_process_id)
        status = "COMPLETED" if exit_code == 0 else "FAILED"
        next_record = replace(current, status=status, exit_code=exit_code, updated_at=now_iso())
        self._save(next_record)
        self._processes.pop(host_process_id, None)
        return next_record

    def poll(self, host_process_id: str) -> GBRuntimeHostProcessRecord:
        current = self.get(host_process_id)
        process = self._processes.get(host_process_id)
        if not process:
            if current.status == "RUNNING" and current.pid is not None and not self._pid_is_running(current.pid):
                next_record = replace(current, status="UNKNOWN", updated_at=now_iso())
                self._save(next_record)
                return next_record
            return current
        exit_code = process.poll()
        if exit_code is None:
            return current
        return self.mark_exited(host_process_id, exit_code)

    def terminate(self, host_process_id: str) -> GBRuntimeHostProcessRecord:
        current = self.get(host_process_id)
        process = self._processes.get(host_process_id)
        if not process:
            if current.status == "RUNNING" and current.pid is not None and self._pid_is_running(current.pid):
                try:
                    os.kill(current.pid, signal.SIGTERM)
                except OSError:
                    next_record = replace(current, status="UNKNOWN", updated_at=now_iso())
                    self._save(next_record)
                    return next_record
                deadline = time.monotonic() + 5.0
                while time.monotonic() < deadline:
                    if not self._pid_is_running(current.pid):
                        next_record = replace(current, status="TERMINATED", exit_code=-signal.SIGTERM, updated_at=now_iso())
                        self._save(next_record)
                        return next_record
                    time.sleep(0.05)
                next_record = replace(current, status="UNKNOWN", updated_at=now_iso())
                self._save(next_record)
                return next_record
            if current.status == "RUNNING":
                next_record = replace(current, status="UNKNOWN", updated_at=now_iso())
                self._save(next_record)
                return next_record
            return current
        process.terminate()
        try:
            exit_code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            exit_code = process.wait(timeout=5)
        return self.mark_exited(host_process_id, exit_code)

    def _save(self, record: GBRuntimeHostProcessRecord) -> None:
        if record.row_version:
            self.storage.update_host_process(record.to_dict(), record.row_version)
        else:
            self.storage.insert_host_process(record.to_dict())

    def _pid_is_running(self, pid: int) -> bool:
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True

    def _validate_command(self, command: list[str]) -> None:
        if not command:
            raise ValidationError("host command is empty")
        if any(not isinstance(part, str) or not part for part in command):
            raise ValidationError("host command contains an invalid argument")

    def _redact_command(self, command: list[str]) -> list[str]:
        redacted: list[str] = []
        redact_next = False
        secret_flags = {"--slot1-auth-token", "--slot2-auth-token", "--auth-token"}
        for part in command:
            if redact_next:
                redacted.append("<redacted>")
                redact_next = False
                continue
            redacted.append(part)
            if part in secret_flags:
                redact_next = True
        return redacted
