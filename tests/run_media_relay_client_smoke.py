#!/usr/bin/env python3
"""End-to-end TLS/auth/pair smoke test for the C media relay client."""

from __future__ import annotations

import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


SESSION_ID = "n64runtime_media_smoke"
SCOPE = "n64_runtime_media"
TICKETS = {"host": "s64ticket_host_smoke", "remote": "s64ticket_remote_smoke"}


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} MEDIA_RELAY_SMOKE_BINARY")
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temp_name:
        temp = Path(temp_name)
        cert = temp / "relay.crt"
        key = temp / "relay.key"
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "1", "-nodes",
                "-keyout", str(key), "-out", str(cert), "-subj", "/CN=localhost",
                "-addext", "subjectAltName=DNS:localhost",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.load_cert_chain(cert, key)
        server = socket.create_server(("127.0.0.1", 0))
        port = server.getsockname()[1]
        barrier = threading.Barrier(2)
        input_ready = threading.Event()
        input_frame: list[bytes] = []
        media_ready = threading.Event()
        media_frames: list[bytes] = []
        failures: list[BaseException] = []

        def recv_exact(client: ssl.SSLSocket, size: int) -> bytes:
            data = b""
            while len(data) < size:
                chunk = client.recv(size - len(data))
                if not chunk:
                    raise RuntimeError("client closed during controller frame")
                data += chunk
            return data

        def recv_frame(client: ssl.SSLSocket) -> bytes:
            header = recv_exact(client, 16)
            if header[:4] != b"N64R" or header[4] != 2:
                raise RuntimeError("invalid media frame header")
            payload_size = struct.unpack("!I", header[12:16])[0]
            if payload_size > 2 * 1024 * 1024:
                raise RuntimeError("oversized media frame")
            return header + recv_exact(client, payload_size)

        def handle(raw: socket.socket) -> None:
            try:
                with context.wrap_socket(raw, server_side=True) as client:
                    line = b""
                    while not line.endswith(b"\n") and len(line) <= 1024:
                        chunk = client.recv(1)
                        if not chunk:
                            raise RuntimeError("client closed during handshake")
                        line += chunk
                    parts = line.decode("ascii").strip().split(" ")
                    if len(parts) != 5 or parts[0] != "N64RUNTIME1" or parts[1] != SESSION_ID:
                        raise RuntimeError(f"invalid handshake: {parts}")
                    role, scope, ticket = parts[2:]
                    if scope != SCOPE or TICKETS.get(role) != ticket:
                        raise RuntimeError("invalid role/scope/ticket")
                    client.sendall(f"N64RUNTIME1 AUTHENTICATED {SESSION_ID} {role}\n".encode("ascii"))
                    barrier.wait(timeout=5)
                    client.sendall(f"N64RUNTIME1 PAIRED {SESSION_ID}\n".encode("ascii"))
                    if role == "remote":
                        input_frame.append(recv_exact(client, 24))
                        input_ready.set()
                        if not media_ready.wait(timeout=5):
                            raise RuntimeError("media frame timeout")
                        for frame in media_frames:
                            client.sendall(frame)
                    else:
                        for _ in range(3):
                            media_frames.append(recv_frame(client))
                        media_ready.set()
                        if not input_ready.wait(timeout=5):
                            raise RuntimeError("controller frame timeout")
                        invalid_input = struct.pack("!4sBBHIIQ", b"N64R", 2, 1, 0, 0, 8, 1 << 63)
                        client.sendall(invalid_input)
                        client.sendall(input_frame[0])
            except BaseException as error:
                failures.append(error)

        processes = [
            subprocess.Popen(
                [str(binary), "localhost", str(port), SESSION_ID, role, SCOPE, TICKETS[role], str(cert)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            for role in ("host", "remote")
        ]
        threads: list[threading.Thread] = []
        for _ in range(2):
            raw, _ = server.accept()
            thread = threading.Thread(target=handle, args=(raw,))
            thread.start()
            threads.append(thread)
        server.close()

        outputs = [process.communicate(timeout=10) for process in processes]
        for thread in threads:
            thread.join(timeout=10)
        if failures:
            raise failures[0]
        for process, (stdout, stderr) in zip(processes, outputs):
            expected_input = "MEDIA INPUT SENT" if "remote" in process.args else "MEDIA INPUT RECEIVED"
            expected_media = "MEDIA FRAME RECEIVED" if "remote" in process.args else None
            if (process.returncode != 0 or "MEDIA TLS PAIRED" not in stdout or
                    expected_input not in stdout or (expected_media and stdout.count(expected_media) != 3)):
                raise RuntimeError(f"media relay smoke failed: rc={process.returncode} stdout={stdout!r} stderr={stderr!r}")
            if "host" in process.args and "MEDIA INPUT DROPPED" not in stdout:
                raise RuntimeError(f"host did not drop invalid controller frame: stdout={stdout!r}")
        print("C media relay TLS/auth/pair smoke: OK")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
