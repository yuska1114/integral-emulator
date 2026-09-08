# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bounded, secret-free timing aggregates for API storage diagnostics."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
import threading


MAX_METRIC_NAME_LENGTH = 96
MAX_SAMPLES_PER_METRIC = 2048


@dataclass
class _Aggregate:
    count: int = 0
    total_ms: float = 0.0
    max_ms: float = 0.0
    total_bytes: int = 0
    samples_ms: deque[float] = field(
        default_factory=lambda: deque(maxlen=MAX_SAMPLES_PER_METRIC)
    )


class TimingMetrics:
    """Collects bounded aggregates without request or account identifiers."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._aggregates: dict[tuple[str, str], _Aggregate] = {}

    def record(
        self,
        category: str,
        name: str,
        duration_ms: float,
        *,
        byte_count: int = 0,
    ) -> None:
        safe_category = self._safe_name(category)
        safe_name = self._safe_name(name)
        value = max(0.0, float(duration_ms))
        size = max(0, int(byte_count))
        with self._lock:
            aggregate = self._aggregates.setdefault(
                (safe_category, safe_name), _Aggregate()
            )
            aggregate.count += 1
            aggregate.total_ms += value
            aggregate.max_ms = max(aggregate.max_ms, value)
            aggregate.total_bytes += size
            aggregate.samples_ms.append(value)

    def snapshot(self) -> dict[str, dict[str, dict[str, float | int]]]:
        with self._lock:
            items = [
                (
                    category,
                    name,
                    aggregate.count,
                    aggregate.total_ms,
                    aggregate.max_ms,
                    aggregate.total_bytes,
                    sorted(aggregate.samples_ms),
                )
                for (category, name), aggregate in self._aggregates.items()
            ]
        result: dict[str, dict[str, dict[str, float | int]]] = {}
        for category, name, count, total_ms, max_ms, total_bytes, samples in items:
            result.setdefault(category, {})[name] = {
                "count": count,
                "total_ms": round(total_ms, 3),
                "average_ms": round(
                    total_ms / count if count else 0.0, 3
                ),
                "p50_ms": round(self._percentile(samples, 0.50), 3),
                "p95_ms": round(self._percentile(samples, 0.95), 3),
                "p99_ms": round(self._percentile(samples, 0.99), 3),
                "max_ms": round(max_ms, 3),
                "total_bytes": total_bytes,
                "sample_count": len(samples),
            }
        return result

    @staticmethod
    def _percentile(samples: list[float], ratio: float) -> float:
        if not samples:
            return 0.0
        index = min(len(samples) - 1, max(0, int((len(samples) - 1) * ratio)))
        return samples[index]

    @staticmethod
    def _safe_name(value: str) -> str:
        text = str(value)
        if not text or len(text) > MAX_METRIC_NAME_LENGTH:
            raise ValueError("metric name is empty or too long")
        if not all(char.isalnum() or char in "._{}-/" for char in text):
            raise ValueError("metric name contains an unsafe character")
        return text
