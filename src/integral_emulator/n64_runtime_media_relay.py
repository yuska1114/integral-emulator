# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Authenticated, session-isolated TCP relay for N64 Runtime media traffic.

TLS is the public-server default. Explicit plain transport supports trusted
same-machine and trusted LAN installations. UDP remains disabled.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import selectors
import socket
import ssl
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path

from .database import AuthorityDatabase
from .errors import NotFoundError, ValidationError
from .gb_runtime_fixed_host_protocol import GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE
from .gb_runtime_fixed_host_sessions import GBRuntimeFixedHostSessionManager
from .n64_runtime_media_sessions import MEDIA_TICKET_SCOPE, N64RuntimeMediaSessionManager
from .network_mode import NETWORK_MODE_PLAIN, NETWORK_MODE_TLS, NETWORK_MODES
from .storage import LeagueStorage
from .runtime_repositories import SQLiteRuntimeRepositories


HANDSHAKE_MAGIC = "N64RUNTIME1"
MAX_HANDSHAKE_BYTES = 1024
DEFAULT_PENDING_TIMEOUT_SECONDS = 60.0
SESSION_RECHECK_SECONDS = 1.0
CONTROL_MAGIC = b"N64R"
CONTROL_VERSION = 2
CONTROL_INPUT = 1
MEDIA_H264_CONFIG = 2
MEDIA_H264_FRAME = 3
MEDIA_AUDIO_ADPCM = 4
GB_FIXED_INPUT = 5
GB_FIXED_INPUT_ACK = 6
GB_FIXED_PING = 7
GB_FIXED_PONG = 8
GB_FIXED_SESSION_STATE = 9
GB_FIXED_TERMINAL = 12
GB_FIXED_TERMINAL_ACK = 13
GB_FIXED_STATE_PAUSED = 1
GB_FIXED_STATE_RESUMED = 2
GB_FIXED_STATE_TERMINATED = 3
CONTROL_HEADER_SIZE = 16
CONTROL_INPUT_SIZE = 8
GB_FIXED_INPUT_SIZE = 16
GB_FIXED_INPUT_ACK_SIZE = 24
GB_FIXED_PING_SIZE = 8
GB_FIXED_TERMINAL_SIZE = 40
CONTROL_FRAME_SIZE = CONTROL_HEADER_SIZE + CONTROL_INPUT_SIZE
CONTROL_BUTTON_MASK = 0x3FFFF
CONTROL_MAX_FRAMES_PER_SECOND = 120
MEDIA_H264_KEYFRAME = 1
MEDIA_MAX_H264_CONFIG_BYTES = 64 * 1024
MEDIA_MAX_H264_FRAME_BYTES = 2 * 1024 * 1024
MEDIA_MAX_AUDIO_BYTES = 8192
MEDIA_MAX_HOST_FRAMES_PER_SECOND = 180
MEDIA_MAX_HOST_BYTES_PER_SECOND = 8 * 1024 * 1024
MEDIA_MAX_BUFFER_BYTES = CONTROL_HEADER_SIZE + MEDIA_MAX_H264_FRAME_BYTES
DEFAULT_FORWARD_QUEUE_BYTES = 8 * 1024 * 1024
# A cold N64 Runtime can spend more than five seconds initializing graphics
# and audio before its client drains the first forwarded packets. Keep the
# bounded queue as the hard memory limit, but allow that normal startup gap.
DEFAULT_WRITE_IDLE_SECONDS = 15.0
DEFAULT_CERT_RELOAD_INTERVAL_SECONDS = 60.0

# Independent message budgets: an audio callback/ACK must not spend a video
# token. Capacities permit two seconds of ordinary scheduling coalescence.
MESSAGE_RATES = {
    CONTROL_INPUT: 120, MEDIA_H264_CONFIG: 8, MEDIA_H264_FRAME: 60,
    MEDIA_AUDIO_ADPCM: 240, GB_FIXED_INPUT: 120, GB_FIXED_INPUT_ACK: 120,
    GB_FIXED_PING: 20, GB_FIXED_PONG: 20,
    GB_FIXED_TERMINAL: 8, GB_FIXED_TERMINAL_ACK: 8,
}


class TokenBucket:
    def __init__(self, rate: float, capacity: float):
        self.rate, self.capacity = rate, capacity
        self.tokens = capacity
        self.updated_at: float | None = None

    def take(self, amount: float, now: float) -> bool:
        if self.updated_at is not None:
            self.tokens = min(self.capacity, self.tokens + max(0, now - self.updated_at) * self.rate)
        self.updated_at = now
        if amount > self.tokens:
            return False
        self.tokens -= amount
        return True


def create_server_tls_context(cert_file: Path, key_file: Path) -> ssl.SSLContext:
    if not cert_file.is_file():
        raise ValueError(f"TLS certificate not found: {cert_file}")
    if not key_file.is_file():
        raise ValueError(f"TLS private key not found: {key_file}")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.load_cert_chain(certfile=cert_file, keyfile=key_file)
    return context


class ReloadingTLSContext:
    """Atomically replaces the TLS context after a stable certificate reload."""

    def __init__(
        self,
        cert_file: Path,
        key_file: Path,
        interval_seconds: float = DEFAULT_CERT_RELOAD_INTERVAL_SECONDS,
    ):
        if not math.isfinite(interval_seconds) or interval_seconds <= 0:
            raise ValueError("TLS certificate reload interval must be positive")
        self.cert_file = cert_file
        self.key_file = key_file
        self.interval_seconds = interval_seconds
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._context, self._accepted_fingerprint = self._load_stable_candidate()

    @staticmethod
    def _file_fingerprint(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(64 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def _pair_fingerprint(self) -> tuple[str, str]:
        return (
            self._file_fingerprint(self.cert_file),
            self._file_fingerprint(self.key_file),
        )

    def _load_stable_candidate(self) -> tuple[ssl.SSLContext, tuple[str, str]]:
        before = self._pair_fingerprint()
        candidate = create_server_tls_context(self.cert_file, self.key_file)
        after = self._pair_fingerprint()
        if before != after:
            raise ValueError("TLS certificate files changed while loading")
        return candidate, after

    def current_context(self) -> ssl.SSLContext:
        with self._lock:
            return self._context

    def reload_if_changed(self) -> bool:
        try:
            observed = self._pair_fingerprint()
            with self._lock:
                if observed == self._accepted_fingerprint:
                    return False
            candidate, fingerprint = self._load_stable_candidate()
        except (OSError, ValueError, ssl.SSLError) as error:
            print(
                f"WARNING: TLS certificate reload rejected; keeping previous context: {error}",
                flush=True,
            )
            return False
        with self._lock:
            self._context = candidate
            self._accepted_fingerprint = fingerprint
        print("TLS certificate context reloaded", flush=True)
        return True

    def start(self) -> None:
        with self._lock:
            if self._thread is not None and self._thread.is_alive():
                return
            self._stop_event.clear()
            self._thread = threading.Thread(
                target=self._watch,
                name="media-relay-certificate-reloader",
                daemon=True,
            )
            self._thread.start()

    def _watch(self) -> None:
        while not self._stop_event.wait(self.interval_seconds):
            self.reload_if_changed()

    def stop(self) -> None:
        self._stop_event.set()
        with self._lock:
            thread = self._thread
            self._thread = None
        if thread is not None:
            thread.join(timeout=5.0)


@dataclass
class MediaRelayClient:
    sock: socket.socket
    addr: tuple[str, int]
    session_id: str
    role: str
    authenticated_at: float
    scope: str = MEDIA_TICKET_SCOPE
    pending_expires_at: float | None = None
    resume_pending: bool = False


class MediaFrameFilter:
    """Incrementally validates the directional media application boundary."""

    def __init__(self, role: str, scope: str = MEDIA_TICKET_SCOPE):
        self.role = role
        self.scope = scope
        self.buffer = bytearray()
        self.last_sequences: dict[str, int] = {}
        self.buckets = {kind: TokenBucket(rate, rate * 2) for kind, rate in MESSAGE_RATES.items()}
        self.byte_bucket = TokenBucket(MEDIA_MAX_HOST_BYTES_PER_SECOND, MEDIA_MAX_HOST_BYTES_PER_SECOND)
        self.message_counts: dict[int, int] = {}

    def feed(self, data: bytes, now: float | None = None) -> list[bytes]:
        if not data:
            return []
        self.buffer.extend(data)
        if len(self.buffer) > MEDIA_MAX_BUFFER_BYTES:
            raise ValueError("media buffer limit exceeded")
        frames: list[bytes] = []
        timestamp = time.monotonic() if now is None else now
        while len(self.buffer) >= CONTROL_HEADER_SIZE:
            magic, version, message_type, flags, sequence, payload_size = struct.unpack(
                "!4sBBHII", self.buffer[:CONTROL_HEADER_SIZE]
            )
            if magic != CONTROL_MAGIC or version != CONTROL_VERSION:
                raise ValueError("invalid media frame header")
            maximum = self._validate_header(message_type, flags, payload_size)
            if payload_size > maximum:
                raise ValueError("media payload limit exceeded")
            frame_size = CONTROL_HEADER_SIZE + payload_size
            if len(self.buffer) < frame_size:
                break
            frame = bytes(self.buffer[:frame_size])
            del self.buffer[:frame_size]
            self._validate_payload(message_type, frame[CONTROL_HEADER_SIZE:])
            sequence_channel = "video" if message_type in {MEDIA_H264_CONFIG, MEDIA_H264_FRAME} else str(message_type)
            last_sequence = self.last_sequences.get(sequence_channel)
            if last_sequence is not None:
                delta = (sequence - last_sequence) & 0xFFFFFFFF
                if delta == 0 or delta >= 0x80000000:
                    raise ValueError("replayed media sequence")
            self.last_sequences[sequence_channel] = sequence
            if not self.buckets[message_type].take(1, timestamp):
                raise ValueError(f"media type rate exceeded type={message_type}")
            if not self.byte_bucket.take(frame_size, timestamp):
                raise ValueError("media bitrate exceeded")
            self.message_counts[message_type] = self.message_counts.get(message_type, 0) + 1
            frames.append(frame)
        return frames

    def _validate_header(self, message_type: int, flags: int, payload_size: int) -> int:
        if self.role == "remote":
            remote_sizes = (
                {
                    GB_FIXED_INPUT: GB_FIXED_INPUT_SIZE,
                    GB_FIXED_PING: GB_FIXED_PING_SIZE,
                    GB_FIXED_TERMINAL_ACK: GB_FIXED_TERMINAL_SIZE,
                }
                if self.scope == GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE
                else {CONTROL_INPUT: CONTROL_INPUT_SIZE}
            )
            if flags != 0 or remote_sizes.get(message_type) != payload_size:
                raise ValueError("remote may send controller input only (or GB prototype input/ping)")
            return remote_sizes[message_type]
        if self.role != "host":
            raise ValueError("invalid media role")
        if message_type == MEDIA_H264_CONFIG and flags == 0:
            return MEDIA_MAX_H264_CONFIG_BYTES
        if message_type == MEDIA_H264_FRAME and flags & ~MEDIA_H264_KEYFRAME == 0:
            return MEDIA_MAX_H264_FRAME_BYTES
        if message_type == MEDIA_AUDIO_ADPCM and flags == 0:
            return MEDIA_MAX_AUDIO_BYTES
        host_sizes = (
            {
                GB_FIXED_INPUT_ACK: GB_FIXED_INPUT_ACK_SIZE,
                GB_FIXED_PONG: GB_FIXED_PING_SIZE,
                GB_FIXED_TERMINAL: GB_FIXED_TERMINAL_SIZE,
            }
            if self.scope == GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE
            else {}
        )
        if flags == 0 and host_sizes.get(message_type) == payload_size:
            return host_sizes[message_type]
        raise ValueError("host may send H.264 or ADPCM only (plus GB prototype acknowledgement/pong)")

    @staticmethod
    def _validate_payload(message_type: int, payload: bytes) -> None:
        if message_type == CONTROL_INPUT:
            buttons = struct.unpack("!Q", payload)[0]
            if buttons & ~CONTROL_BUTTON_MASK:
                raise ValueError("invalid controller button mask")
            return
        if message_type == GB_FIXED_INPUT:
            buttons, sent_us = struct.unpack("!QQ", payload)
            if buttons & ~0xFF or sent_us == 0:
                raise ValueError("invalid GB input")
            return
        if message_type == GB_FIXED_INPUT_ACK:
            input_sequence, reserved, sent_us, applied_frame = struct.unpack("!IIQQ", payload)
            if input_sequence == 0 or reserved != 0 or sent_us == 0 or applied_frame == 0:
                raise ValueError("invalid GB input acknowledgement")
            return
        if message_type in {GB_FIXED_PING, GB_FIXED_PONG}:
            if struct.unpack("!Q", payload)[0] == 0:
                raise ValueError("invalid GB latency probe")
            return
        if message_type in {GB_FIXED_TERMINAL, GB_FIXED_TERMINAL_ACK}:
            final_frame = struct.unpack("!Q", payload[:8])[0]
            if final_frame == 0 or payload[8:] == b"\0" * 32:
                raise ValueError("invalid GB terminal receipt")
            return
        if message_type == MEDIA_H264_CONFIG:
            if len(payload) < 8:
                raise ValueError("invalid H.264 config")
            width, height, sps_size, pps_size = struct.unpack("!HHHH", payload[:8])
            if not (1 <= width <= 1920 and 1 <= height <= 1080 and sps_size and pps_size):
                raise ValueError("invalid H.264 config")
            if len(payload) != 8 + sps_size + pps_size:
                raise ValueError("invalid H.264 config")
            return
        if message_type == MEDIA_H264_FRAME:
            if len(payload) <= 8:
                raise ValueError("invalid H.264 frame")
            return
        if message_type == MEDIA_AUDIO_ADPCM:
            if len(payload) < 24:
                raise ValueError("invalid ADPCM audio")
            sample_rate, channels, frame_count = struct.unpack("!IHH", payload[:8])
            expected_size = 16 + 8 + frame_count - 1
            if not (8000 <= sample_rate <= 192000 and channels == 2 and frame_count > 0):
                raise ValueError("invalid ADPCM audio")
            if len(payload) != expected_size:
                raise ValueError("invalid ADPCM audio")


@dataclass
class ForwardFrame:
    data: bytes
    created_at: float
    offset: int = 0

    @property
    def kind(self) -> int:
        return self.data[5]

    @property
    def keyframe(self) -> bool:
        return self.kind == MEDIA_H264_FRAME and bool(self.data[7] & MEDIA_H264_KEYFRAME)


class MediaForwardQueue:
    """Bounded complete-message queue; never truncate a TCP/TLS frame.

    A False append means backpressure, not permission to lose a control frame.
    After a delta is discarded, dependent deltas wait for the next keyframe.
    """

    def __init__(self, limit: int):
        self.limit = limit
        self.frames: deque[ForwardFrame] = deque()
        self.bytes = 0
        self.wait_keyframe = False
        self.dropped: dict[int, int] = {}

    def _drop(self, frame: ForwardFrame) -> None:
        self.bytes -= len(frame.data)
        self.dropped[frame.kind] = self.dropped.get(frame.kind, 0) + 1

    def prune(self, now: float, pressure: bool = False) -> None:
        kept: deque[ForwardFrame] = deque()
        broken_video = False
        for frame in self.frames:
            if frame.offset:
                kept.append(frame)
                continue
            if frame.keyframe:
                broken_video = False
            stale = now - frame.created_at > 0.25
            if ((frame.kind == MEDIA_AUDIO_ADPCM and (stale or pressure)) or
                (frame.kind == MEDIA_H264_FRAME and not frame.keyframe and
                 (stale or pressure or broken_video))):
                self._drop(frame)
                if frame.kind == MEDIA_H264_FRAME:
                    broken_video = True
            else:
                kept.append(frame)
        self.frames = kept
        if broken_video:
            self.wait_keyframe = True

    def append(self, data: bytes, now: float) -> bool:
        frame = ForwardFrame(data, now)
        self.prune(now)
        if frame.keyframe and (self.wait_keyframe or self.bytes + len(data) > self.limit):
            # Supersede an obsolete GOP, but retain every control message,
            # the newest config, and any frame whose send already started.
            configs = [item for item in self.frames if item.kind == MEDIA_H264_CONFIG]
            latest_config = configs[-1] if configs else None
            kept = deque()
            for item in self.frames:
                if not item.offset and (item.kind == MEDIA_H264_FRAME or
                    (item.kind == MEDIA_H264_CONFIG and item is not latest_config)):
                    self._drop(item)
                else:
                    kept.append(item)
            self.frames = kept
        if self.wait_keyframe and frame.kind == MEDIA_H264_FRAME and not frame.keyframe:
            self.dropped[frame.kind] = self.dropped.get(frame.kind, 0) + 1
            return True
        if self.bytes + len(data) > self.limit:
            self.prune(now, pressure=True)
        if self.wait_keyframe and frame.kind == MEDIA_H264_FRAME and not frame.keyframe:
            self.dropped[frame.kind] = self.dropped.get(frame.kind, 0) + 1
            return True
        if self.bytes + len(data) > self.limit:
            if frame.kind == MEDIA_AUDIO_ADPCM or (frame.kind == MEDIA_H264_FRAME and not frame.keyframe):
                self.dropped[frame.kind] = self.dropped.get(frame.kind, 0) + 1
                if frame.kind == MEDIA_H264_FRAME:
                    self.wait_keyframe = True
                return True
            return False
        if frame.keyframe:
            self.wait_keyframe = False
        self.frames.append(frame)
        self.bytes += len(data)
        return True

    def sent(self, count: int) -> None:
        frame = self.frames[0]
        frame.offset += count
        self.bytes -= count
        if frame.offset == len(frame.data):
            self.frames.popleft()


class AuthenticatedN64RuntimeMediaRelay:
    def __init__(
        self,
        storage_roots: list[Path],
        *,
        pending_timeout_seconds: float = DEFAULT_PENDING_TIMEOUT_SECONDS,
        forward_queue_bytes: int = DEFAULT_FORWARD_QUEUE_BYTES,
        write_idle_seconds: float = DEFAULT_WRITE_IDLE_SECONDS,
    ):
        if not storage_roots:
            raise ValueError("at least one storage root is required")
        database = AuthorityDatabase(storage_roots[0])
        database.initialize()
        repositories = [
            SQLiteRuntimeRepositories(
                LeagueStorage(root),
                database,
                "primary" if index == 0 else root.name,
            )
            for index, root in enumerate(storage_roots)
        ]
        self.session_managers = [
            N64RuntimeMediaSessionManager(repository) for repository in repositories
        ]
        self.gb_runtime_fixed_host_session_managers = [
            GBRuntimeFixedHostSessionManager(repository) for repository in repositories
        ]
        self.pending_timeout_seconds = pending_timeout_seconds
        if forward_queue_bytes <= 0 or write_idle_seconds <= 0:
            raise ValueError("relay queue and write idle limits must be positive")
        self.forward_queue_bytes = forward_queue_bytes
        self.write_idle_seconds = write_idle_seconds
        self.pending: dict[str, dict[str, MediaRelayClient]] = {}
        self.active: dict[str, tuple[MediaRelayClient, MediaRelayClient]] = {}
        self.lock = threading.RLock()

    @staticmethod
    def create_tls_context(cert_file: Path, key_file: Path) -> ssl.SSLContext:
        return create_server_tls_context(cert_file, key_file)

    def serve(
        self,
        host: str,
        port: int,
        tls_context: ssl.SSLContext | None,
        transport: str = NETWORK_MODE_TLS,
        *,
        tls_context_reloader: ReloadingTLSContext | None = None,
    ) -> None:
        if transport not in NETWORK_MODES:
            raise ValueError("media relay transport must be tls or plain")
        if transport == NETWORK_MODE_TLS and tls_context is None:
            raise ValueError("TLS media relay requires a TLS context")
        if transport == NETWORK_MODE_PLAIN and tls_context is not None:
            raise ValueError("plain media relay must not receive a TLS context")
        if transport == NETWORK_MODE_PLAIN and tls_context_reloader is not None:
            raise ValueError("plain media relay must not receive a TLS context reloader")
        for manager in self.gb_runtime_fixed_host_session_managers:
            for session_id in manager.pause_relay_orphans():
                print(
                    f"N64 Runtime fixed Host session paused after Relay restart "
                    f"session={session_id}",
                    flush=True,
                )
        if tls_context_reloader is not None:
            tls_context_reloader.start()
        try:
            with socket.create_server((host, port), reuse_port=False) as server:
                server.listen(64)
                print(
                    f"N64 Runtime {transport} media relay listening on {host}:{port}; UDP disabled",
                    flush=True,
                )
                if transport == NETWORK_MODE_PLAIN:
                    print(
                        "WARNING: media relay traffic is not encrypted; use only on a trusted LAN",
                        flush=True,
                    )
                while True:
                    raw_sock, addr = server.accept()
                    connection_context = (
                        tls_context_reloader.current_context()
                        if tls_context_reloader is not None
                        else tls_context
                    )
                    thread = threading.Thread(
                        target=self.handle_client,
                        args=(raw_sock, addr, connection_context, transport),
                        daemon=True,
                    )
                    thread.start()
        finally:
            if tls_context_reloader is not None:
                tls_context_reloader.stop()

    def handle_client(
        self,
        raw_sock: socket.socket,
        addr: tuple[str, int],
        tls_context: ssl.SSLContext | None,
        transport: str = NETWORK_MODE_TLS,
    ) -> None:
        sock: socket.socket = raw_sock
        try:
            raw_sock.settimeout(10.0)
            if transport == NETWORK_MODE_TLS:
                if tls_context is None:
                    raise ValueError("TLS context is missing")
                sock = tls_context.wrap_socket(raw_sock, server_side=True)
            elif transport != NETWORK_MODE_PLAIN:
                raise ValueError("invalid media relay transport")
            session_id, role, scope, ticket = self.read_handshake(sock)
            if not self.validate_credentials(session_id, role, scope, ticket):
                raise ValueError("invalid media relay credentials")
            client = MediaRelayClient(sock, addr, session_id, role, time.monotonic(), scope)
            sock.sendall(f"{HANDSHAKE_MAGIC} AUTHENTICATED {session_id} {role}\n".encode("ascii"))
            peer = self.register_pending(client)
            if peer is None:
                return
            self.bridge(client, peer)
        except Exception as error:
            print(f"N64 Runtime media relay rejected {addr}: {error}", flush=True)
            self._close_socket(sock)

    def read_handshake(self, sock: socket.socket) -> tuple[str, str, str, str]:
        data = bytearray()
        while b"\n" not in data:
            chunk = sock.recv(1)
            if not chunk:
                raise ValueError("handshake closed")
            data.extend(chunk)
            if len(data) > MAX_HANDSHAKE_BYTES:
                raise ValueError("handshake too large")
        try:
            line = bytes(data).decode("ascii", errors="strict").strip()
        except UnicodeDecodeError as error:
            raise ValueError("handshake must be ASCII") from error
        parts = line.split(" ")
        if len(parts) != 5 or parts[0] != HANDSHAKE_MAGIC:
            raise ValueError("bad handshake")
        session_id, role, scope, ticket = parts[1:]
        if role not in {"host", "remote"}:
            raise ValueError("bad role")
        if not session_id or not scope or not ticket:
            raise ValueError("incomplete handshake")
        return session_id, role, scope, ticket

    def validate_and_consume_ticket(self, session_id: str, role: str, ticket: str) -> bool:
        for manager in self.session_managers:
            if manager.validate_ticket(session_id, role, ticket, consume=True):
                return True
        return False

    def validate_credentials(self, session_id: str, role: str, scope: str, ticket: str) -> bool:
        if scope == MEDIA_TICKET_SCOPE:
            return self.validate_and_consume_ticket(session_id, role, ticket)
        if scope == GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE:
            return any(
                manager.validate_ticket(session_id, role, scope, ticket, consume=True)
                for manager in self.gb_runtime_fixed_host_session_managers
            )
        return False

    def session_is_active(self, session_id: str) -> bool:
        if any(
            manager.session_is_active(session_id)
            for manager in self.gb_runtime_fixed_host_session_managers
        ):
            return True
        for manager in self.session_managers:
            try:
                if manager.get(session_id).status in {"CREATED", "WAITING_PEER", "READY", "RUNNING"}:
                    return True
            except (NotFoundError, ValidationError):
                continue
        return False

    def register_pending(self, client: MediaRelayClient) -> MediaRelayClient | None:
        with self.lock:
            # A freshly authenticated transport replaces, never runs beside,
            # an old pair for this session. Old bridge cleanup cannot cancel it.
            previous = self.active.pop(client.session_id, None)
            if previous:
                for old_client in previous:
                    self._close_socket(old_client.sock)
            self._expire_pending_locked(time.monotonic())
            session_pending = self.pending.setdefault(client.session_id, {})
            old = session_pending.pop(client.role, None)
            if old:
                self._close_socket(old.sock)
            peer_role = "remote" if client.role == "host" else "host"
            peer = session_pending.pop(peer_role, None)
            if peer:
                self.pending.pop(client.session_id, None)
                return peer
            session_pending[client.role] = client
            timeout = (
                max(0.0, client.pending_expires_at - time.monotonic())
                if client.pending_expires_at is not None
                else self.pending_timeout_seconds
            )
            timer = threading.Timer(min(timeout, SESSION_RECHECK_SECONDS) if client.resume_pending else timeout,
                                    self.expire_pending_client, args=(client,))
            timer.daemon = True
            timer.start()
            print(
                f"N64 Runtime media relay waiting session={client.session_id} role={client.role} addr={client.addr}",
                flush=True,
            )
            return None

    def expire_pending_client(self, client: MediaRelayClient) -> None:
        with self.lock:
            roles = self.pending.get(client.session_id)
            if not roles or roles.get(client.role) is not client:
                return
            deadline = client.pending_expires_at
            if deadline is None:
                deadline = client.authenticated_at + self.pending_timeout_seconds
            terminated = client.resume_pending and self._fixed_host_terminated(client.session_id)
            if not terminated and time.monotonic() < deadline:
                if client.resume_pending:
                    timer = threading.Timer(min(SESSION_RECHECK_SECONDS, deadline - time.monotonic()),
                                            self.expire_pending_client, args=(client,))
                    timer.daemon = True
                    timer.start()
                return
            roles.pop(client.role, None)
            if not roles:
                self.pending.pop(client.session_id, None)
            if terminated:
                self._notify_fixed_host_terminated(client)
            self._close_socket(client.sock)
            if client.resume_pending:
                self._expire_gb_runtime_fixed_host_pause(client.session_id)

    def bridge(self, left: MediaRelayClient, right: MediaRelayClient) -> None:
        if left.session_id != right.session_id or left.role == right.role or left.scope != right.scope:
            self._close_socket(left.sock)
            self._close_socket(right.sock)
            raise ValueError("media relay pairing invariant failed")
        with self.lock:
            self.active[left.session_id] = (left, right)
        print(
            f"N64 Runtime media relay paired session={left.session_id} {left.addr}<->{right.addr}",
            flush=True,
        )
        fixed_product = left.scope == GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE
        resumed = fixed_product and (left.resume_pending or right.resume_pending)
        paired = f"{HANDSHAKE_MAGIC} PAIRED {left.session_id}\n".encode("ascii")
        if not (resumed and left.role == "host"):
            left.sock.sendall(paired)
        if not (resumed and right.role == "host"):
            right.sock.sendall(paired)
        left.sock.setblocking(False)
        right.sock.setblocking(False)
        selector = selectors.DefaultSelector()
        clients = {left.sock: left, right.sock: right}
        peers = {left.sock: right.sock, right.sock: left.sock}
        queues = {
            left.sock: MediaForwardQueue(self.forward_queue_bytes),
            right.sock: MediaForwardQueue(self.forward_queue_bytes),
        }
        write_progress_at = {left.sock: time.monotonic(), right.sock: time.monotonic()}
        selector.register(left.sock, selectors.EVENT_READ)
        selector.register(right.sock, selectors.EVENT_READ)
        filters = {
            "host": MediaFrameFilter("host", left.scope),
            "remote": MediaFrameFilter("remote", left.scope),
        }
        if resumed:
            host_client = left if left.role == "host" else right
            host_client.sock.sendall(
                self._gb_runtime_fixed_host_state_frame(GB_FIXED_STATE_RESUMED, 0)
            )
        disconnected_role: str | None = None
        fixed_terminal_payload: bytes | None = None
        fixed_terminal_acknowledged = False
        close_reason = "session_ended"
        last_metrics_at = time.monotonic()
        previous_counts = {"host": {}, "remote": {}}

        def update_interest(sock):
            target = peers[sock]
            capacity = self.forward_queue_bytes - queues[target].bytes - len(filters[clients[sock].role].buffer)
            events = (selectors.EVENT_READ if capacity > 0 else 0)
            if queues[sock].frames:
                events |= selectors.EVENT_WRITE
            try:
                selector.get_key(sock)
            except KeyError:
                if events:
                    selector.register(sock, events)
            else:
                if events:
                    selector.modify(sock, events)
                else:
                    selector.unregister(sock)

        try:
            while True:
                if not self.session_is_active(left.session_id):
                    return
                now = time.monotonic()
                for sock, queue in queues.items():
                    queue.prune(now)
                    update_interest(sock)
                    if queue.frames and now - write_progress_at[sock] >= self.write_idle_seconds:
                        disconnected_role = clients[sock].role
                        close_reason = "receiver_stalled"
                        return
                if now - last_metrics_at >= 5.0:
                    for sock, queue in queues.items():
                        age = now - queue.frames[0].created_at if queue.frames else 0
                        source_role = clients[peers[sock]].role
                        counts = filters[source_role].message_counts
                        rates = {kind: round((count - previous_counts[source_role].get(kind, 0)) /
                                            (now - last_metrics_at), 2)
                                 for kind, count in counts.items()}
                        previous_counts[source_role] = counts.copy()
                        type_queue = {}
                        for frame in queue.frames:
                            size, oldest = type_queue.get(frame.kind, (0, 0.0))
                            type_queue[frame.kind] = (size + len(frame.data) - frame.offset,
                                                     round(max(oldest, now - frame.created_at), 3))
                        print(f"media relay metrics session={left.session_id} target_role={clients[sock].role} "
                              f"queued_bytes={queue.bytes} oldest_seconds={age:.3f} drops={queue.dropped} "
                              f"source_type_rates={rates} type_queue_bytes_age={type_queue}", flush=True)
                    last_metrics_at = now
                for key, mask in selector.select(SESSION_RECHECK_SECONDS):
                    source: socket.socket = key.fileobj
                    if mask & selectors.EVENT_READ:
                        target = peers[source]
                        capacity = self.forward_queue_bytes - queues[target].bytes - len(filters[clients[source].role].buffer)
                        if capacity <= 0:
                            update_interest(source)
                            continue
                        try:
                            data = source.recv(min(65536, capacity))
                        except (BlockingIOError, ssl.SSLWantReadError, ssl.SSLWantWriteError):
                            data = None
                        except (ConnectionError, OSError, ssl.SSLError):
                            # A killed Remote usually resets TLS instead of
                            # producing a clean EOF.  Preserve the role so the
                            # fixed-Host product can enter its resume window.
                            disconnected_role = clients[source].role
                            close_reason = "read_error"
                            return
                        if data == b"":
                            disconnected_role = clients[source].role
                            close_reason = "peer_eof"
                            return
                        if data:
                            target = peers[source]
                            queue_was_empty = not queues[target].frames
                            for frame in filters[clients[source].role].feed(data):
                                if fixed_product:
                                    message_type = frame[5]
                                    payload = frame[CONTROL_HEADER_SIZE:]
                                    if (
                                        clients[source].role == "host"
                                        and message_type == GB_FIXED_TERMINAL
                                    ):
                                        fixed_terminal_payload = payload
                                    elif (
                                        clients[source].role == "remote"
                                        and message_type == GB_FIXED_TERMINAL_ACK
                                        and fixed_terminal_payload is not None
                                        and payload == fixed_terminal_payload
                                    ):
                                        fixed_terminal_acknowledged = True
                                if not queues[target].append(frame, now):
                                    # Read capacity above reserves all buffered bytes,
                                    # so protected frames always fit without dropping.
                                    raise RuntimeError("media relay queue reservation invariant failed")
                            if queues[target].frames:
                                if queue_was_empty:
                                    write_progress_at[target] = time.monotonic()
                                update_interest(target)
                            update_interest(source)
                    if mask & selectors.EVENT_WRITE:
                        queue = queues[source]
                        while queue.frames:
                            try:
                                frame = queue.frames[0]
                                remaining = len(frame.data) - frame.offset
                                sent = source.send(memoryview(frame.data)[frame.offset:])
                            except (BlockingIOError, ssl.SSLWantReadError, ssl.SSLWantWriteError):
                                break
                            except (ConnectionError, OSError, ssl.SSLError):
                                disconnected_role = clients[source].role
                                close_reason = "write_error"
                                return
                            if sent <= 0:
                                disconnected_role = clients[source].role
                                close_reason = "write_closed"
                                return
                            queue.sent(sent)
                            write_progress_at[source] = time.monotonic()
                            if sent < remaining:
                                break
                        update_interest(source)
                        update_interest(peers[source])
        except Exception as error:
            close_reason = f"{type(error).__name__}:{error}"
            raise
        finally:
            selector.close()
            with self.lock:
                current_pair = self.active.get(left.session_id) == (left, right)
                if current_pair:
                    self.active.pop(left.session_id, None)
            host_client = left if left.role == "host" else right
            remote_client = right if left.role == "host" else left
            terminated = fixed_product and current_pair and self._fixed_host_terminated(left.session_id)
            if terminated:
                close_reason = "authoritative_terminal"
                for client in (left, right):
                    # Complete only an already-started frame before the notice.
                    # Unsent video must not delay terminal delivery.
                    queue = queues[client.sock]
                    prefix = b""
                    if queue.frames and queue.frames[0].offset:
                        frame = queue.frames[0]
                        prefix = frame.data[frame.offset:]
                    self._notify_fixed_host_terminated(client, prefix)
            if (
                fixed_product
                and current_pair
                and not terminated
                and disconnected_role == "remote"
                and not fixed_terminal_acknowledged
            ):
                self._close_socket(remote_client.sock)
                self._retain_gb_runtime_fixed_host_for_resume(host_client)
            else:
                if not fixed_product and current_pair:
                    for manager in self.session_managers:
                        try:
                            manager.recover(left.session_id)
                            break
                        except NotFoundError:
                            continue
                self._close_socket(left.sock)
                self._close_socket(right.sock)
                if (
                    fixed_product
                    and current_pair
                    and not fixed_terminal_acknowledged
                    and disconnected_role in {"host", "remote"}
                ):
                    self._gb_runtime_fixed_host_peer_disconnected(left.session_id, disconnected_role)
            print(f"N64 Runtime media relay closed session={left.session_id} role={disconnected_role} reason={close_reason}", flush=True)

    def _retain_gb_runtime_fixed_host_for_resume(self, host: MediaRelayClient) -> None:
        # The small grace keeps the monotonic Relay timer strictly after the
        # persisted wall-clock authority deadline.
        deadline = time.monotonic() + 25.25
        host.authenticated_at = time.monotonic()
        host.pending_expires_at = deadline
        host.resume_pending = True
        try:
            if self._fixed_host_terminated(host.session_id):
                self._notify_fixed_host_terminated(host)
                self._close_socket(host.sock)
                return
            host.sock.setblocking(True)
            host.sock.sendall(self._gb_runtime_fixed_host_state_frame(GB_FIXED_STATE_PAUSED, 25000))
            host.sock.settimeout(None)
            self._gb_runtime_fixed_host_peer_disconnected(host.session_id, "remote")
            self.register_pending(host)
        except Exception as error:
            print(
                f"N64 Runtime fixed Host resume retention failed "
                f"session={host.session_id}: {error}", flush=True
            )
            self._close_socket(host.sock)
            self._gb_runtime_fixed_host_peer_disconnected(host.session_id, "host")

    def _fixed_host_terminated(self, session_id: str) -> bool:
        for manager in self.gb_runtime_fixed_host_session_managers:
            try:
                return manager.get(session_id).state in {"ABORTED", "CANCELLED"}
            except NotFoundError:
                continue
        return False

    def _notify_fixed_host_terminated(self, client: MediaRelayClient, prefix: bytes = b"") -> None:
        # Delivery is bounded and best effort. The parent independently reaps
        # on authoritative ROOM removal, including EOF-before-notice races.
        try:
            client.sock.settimeout(0.1)
            client.sock.sendall(prefix + self._gb_runtime_fixed_host_state_frame(GB_FIXED_STATE_TERMINATED, 0))
        except (OSError, ssl.SSLError):
            pass

    @staticmethod
    def _gb_runtime_fixed_host_state_frame(state: int, remaining_ms: int) -> bytes:
        payload = struct.pack("!II", state, remaining_ms)
        return struct.pack(
            "!4sBBHII", CONTROL_MAGIC, CONTROL_VERSION,
            GB_FIXED_SESSION_STATE, 0, state, len(payload)
        ) + payload

    def _gb_runtime_fixed_host_peer_disconnected(self, session_id: str, role: str) -> None:
        for manager in self.gb_runtime_fixed_host_session_managers:
            try:
                manager.peer_disconnected(session_id, role)
                return
            except (NotFoundError, ValidationError):
                continue

    def _expire_gb_runtime_fixed_host_pause(self, session_id: str) -> None:
        for manager in self.gb_runtime_fixed_host_session_managers:
            try:
                manager.expire_remote_pause(session_id)
                return
            except (NotFoundError, ValidationError):
                continue

    def _expire_pending_locked(self, now: float) -> None:
        empty_sessions: list[str] = []
        for session_id, roles in self.pending.items():
            expired_roles = [
                role
                for role, client in roles.items()
                if now >= (
                    client.pending_expires_at
                    if client.pending_expires_at is not None
                    else client.authenticated_at + self.pending_timeout_seconds
                )
            ]
            for role in expired_roles:
                client = roles.pop(role)
                self._close_socket(client.sock)
                if client.resume_pending:
                    self._expire_gb_runtime_fixed_host_pause(client.session_id)
            if not roles:
                empty_sessions.append(session_id)
        for session_id in empty_sessions:
            self.pending.pop(session_id, None)

    @staticmethod
    def _close_socket(sock: socket.socket) -> None:
        try:
            sock.close()
        except OSError:
            pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Authenticated N64 Runtime media relay (TCP only)")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=25164)
    parser.add_argument("--transport", choices=sorted(NETWORK_MODES), default=NETWORK_MODE_TLS)
    parser.add_argument("--cert-file", type=Path)
    parser.add_argument("--key-file", type=Path)
    parser.add_argument(
        "--cert-reload-interval-seconds",
        type=float,
        default=DEFAULT_CERT_RELOAD_INTERVAL_SECONDS,
    )
    parser.add_argument(
        "--storage-root",
        action="append",
        type=Path,
        required=True,
        help="INTEGRAL EMULATOR storage root containing data/n64_runtime_media_sessions.json; repeat as needed",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    relay = AuthenticatedN64RuntimeMediaRelay(args.storage_root)
    if args.transport == NETWORK_MODE_TLS:
        if args.cert_file is None or args.key_file is None:
            raise SystemExit("TLS transport requires --cert-file and --key-file")
        try:
            reloader = ReloadingTLSContext(
                args.cert_file,
                args.key_file,
                args.cert_reload_interval_seconds,
            )
        except ValueError as error:
            raise SystemExit(str(error)) from error
        tls_context = reloader.current_context()
    else:
        if args.cert_file is not None or args.key_file is not None:
            raise SystemExit("plain transport does not accept --cert-file or --key-file")
        tls_context = None
        reloader = None
    relay.serve(
        args.host,
        args.port,
        tls_context,
        args.transport,
        tls_context_reloader=reloader,
    )


if __name__ == "__main__":
    main()
