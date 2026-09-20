#!/usr/bin/env python3
"""Exercise the real transport using disposable loopback peers/certificates."""
from __future__ import annotations

import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading


def certificate(directory: Path, name: str, hostname: str) -> tuple[Path, Path]:
    cert, key = directory / f"{name}.crt", directory / f"{name}.key"
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-sha256",
        "-days", "1", "-nodes", "-keyout", str(key), "-out", str(cert),
        "-subj", f"/CN={hostname}", "-addext", f"subjectAltName=DNS:{hostname}",
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cert, key


def main() -> int:
    binary = str(Path(sys.argv[1]).resolve())
    no_tls = len(sys.argv) > 2 and sys.argv[2] == "--no-tls"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        good, good_key = certificate(root, "good", "localhost")
        other, _ = certificate(root, "other", "localhost")
        wrong, wrong_key = certificate(root, "wrong", "wrong.invalid")
        env = os.environ.copy()
        env["SSL_CERT_FILE"] = str(good)
        env["SSL_CERT_DIR"] = str(root / "absent")

        def invoke(url: str, capacity: int, expected: int, marker: str,
                   environment: dict[str, str] = env) -> None:
            result = subprocess.run([binary, url, str(capacity)], env=environment,
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == expected, (url, result.returncode, result.stdout, result.stderr)
            assert marker in result.stdout, (marker, result.stdout)

        def peer(chunks: list[bytes], capacity: int, expected: int, marker: str,
                 tls: bool = False, mismatch: bool = False, untrusted: bool = False,
                 reset: bool = False) -> None:
            host = "localhost" if tls else "127.0.0.1"
            family, _, _, _, address = socket.getaddrinfo(host, 0, type=socket.SOCK_STREAM)[0]
            listener = socket.socket(family)
            listener.bind(address)
            listener.listen(1)
            listener.settimeout(5)
            context = None
            if tls:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.load_cert_chain(wrong if mismatch else good,
                                        wrong_key if mismatch else good_key)
            errors: list[Exception] = []

            def serve() -> None:
                try:
                    with listener.accept()[0] as raw:
                        raw.settimeout(5)
                        conn = context.wrap_socket(raw, server_side=True) if context else raw
                        try:
                            request = b""
                            while b"\r\n\r\n" not in request:
                                part = conn.recv(1024)
                                if not part:
                                    return
                                request += part
                            for chunk in chunks:
                                conn.sendall(chunk)
                            if reset:
                                fmt = "hh" if os.name == "nt" else "ii"
                                conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                                struct.pack(fmt, 1, 0))
                            elif context:
                                try:
                                    conn.unwrap()
                                except (OSError, ssl.SSLError):
                                    pass  # capacity rejection can close without close_notify
                        finally:
                            conn.close()
                except ssl.SSLError as exc:
                    if not (mismatch or untrusted):
                        errors.append(exc)
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            environment = dict(env)
            environment["SSL_CERT_FILE"] = str(other if untrusted else wrong if mismatch else good)
            try:
                invoke(f"{'https' if tls else 'http'}://{host}:{listener.getsockname()[1]}",
                       capacity, expected, marker, environment)
            finally:
                thread.join(7)
                listener.close()
            assert not thread.is_alive() and not errors, errors

        invoke("ftp://localhost", 16, 3, "ONLY HTTP/HTTPS")
        invoke("http://", 16, 3, "INVALID SERVER HOST")
        invoke("http://localhost:x", 16, 3, "INVALID SERVER PORT")
        with socket.socket() as bound:
            bound.bind(("127.0.0.1", 0))  # reserved but not listening
            invoke(f"http://127.0.0.1:{bound.getsockname()[1]}", 16, 4, "OPEN")
        for tls in ([False] if no_tls else [False, True]):
            peer([b"a", b"bc", b"def"], 7, 0, "OK 6 abcdef", tls=tls)
            peer([b"abcdefg"], 7, 6, "READ", tls=tls)
            peer([], 1, 0, "OK 0", tls=tls)
        peer([b"partial"], 128, 6, "READ", reset=True)
        if no_tls:
            invoke("https://localhost", 16, 3, "HTTPS REQUIRES OPENSSL BUILD")
        else:
            peer([], 16, 4, "TLS", tls=True, untrusted=True)
            peer([], 16, 4, "TLS", tls=True, mismatch=True)
    print("HTTP transport: URL/connect/fragmented receive/capacity/reset/TLS rejection PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
