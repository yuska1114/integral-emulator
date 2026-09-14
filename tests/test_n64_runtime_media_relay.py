from __future__ import annotations

import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import shutil
from pathlib import Path

from integral_emulator.database import AuthorityDatabase
from integral_emulator.n64_runtime_media_sessions import MEDIA_TICKET_SCOPE, N64RuntimeMediaSessionManager
from integral_emulator.gb_runtime_fixed_host_protocol import (
    GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
    GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
    GBRuntimeFixedHostManifest,
)
from integral_emulator.gb_runtime_fixed_host_sessions import GBRuntimeFixedHostSessionManager
from integral_emulator.storage import LeagueStorage
from integral_emulator.runtime_repositories import SQLiteRuntimeRepositories
from integral_emulator.n64_runtime_media_relay import (
    CONTROL_FRAME_SIZE,
    CONTROL_INPUT,
    GB_FIXED_INPUT,
    GB_FIXED_INPUT_ACK,
    GB_FIXED_PING,
    GB_FIXED_PONG,
    GB_FIXED_TERMINAL,
    GB_FIXED_TERMINAL_ACK,
    HANDSHAKE_MAGIC,
    MEDIA_AUDIO_ADPCM,
    MEDIA_MAX_AUDIO_BYTES,
    MEDIA_H264_CONFIG,
    MEDIA_H264_FRAME,
    MEDIA_H264_KEYFRAME,
    AuthenticatedN64RuntimeMediaRelay,
    MediaFrameFilter,
    MediaRelayClient,
    ReloadingTLSContext,
)


class N64RuntimeMediaRelayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        database = AuthorityDatabase(self.root)
        database.initialize()
        with database.transaction(write=True) as connection:
            for user_id in ("user-host", "user-remote"):
                connection.execute(
                    "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, 1, 1)",
                    (user_id, user_id, user_id),
                )
        self.storage = SQLiteRuntimeRepositories(
            LeagueStorage(self.root), database, "primary"
        )
        self.manager = N64RuntimeMediaSessionManager(self.storage)
        self.session = self.manager.create_or_get(
            room_number=65,
            host_user_id="user-host",
            remote_user_id="user-remote",
            host_n64_slot="ROM1",
            host_gb_slot="ROM2",
            remote_gb_slot="ROM3",
            host_n64_rom_id="n64-rom",
            host_gb_rom_id="host-gb-rom",
            remote_gb_rom_id="remote-gb-rom",
            host_n64_save_id="n64-save",
            host_save_id="host-save",
            remote_save_id="remote-save",
            game_type="sample_alpha",
        )

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def _create_certificate_pair(self, name: str) -> tuple[Path, Path]:
        if shutil.which("openssl") is None:
            self.skipTest("openssl is required for TLS reload tests")
        cert = self.root / f"{name}.crt"
        key = self.root / f"{name}.key"
        completed = subprocess.run(
            [
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-sha256",
                "-days",
                "1",
                "-nodes",
                "-subj",
                f"/CN={name}.example",
                "-keyout",
                str(key),
                "-out",
                str(cert),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return cert, key

    def _tls_socket_pair(
        self, server_context: ssl.SSLContext
    ) -> tuple[ssl.SSLSocket, ssl.SSLSocket]:
        server_raw, client_raw = socket.socketpair()
        result: dict[str, object] = {}

        def wrap_server() -> None:
            try:
                result["socket"] = server_context.wrap_socket(
                    server_raw, server_side=True
                )
            except BaseException as error:
                result["error"] = error

        thread = threading.Thread(target=wrap_server, daemon=True)
        thread.start()
        client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        client_context.minimum_version = ssl.TLSVersion.TLSv1_3
        client_context.check_hostname = False
        client_context.verify_mode = ssl.CERT_NONE
        try:
            client = client_context.wrap_socket(
                client_raw, server_hostname="localhost"
            )
        except BaseException:
            server_raw.close()
            client_raw.close()
            thread.join(timeout=2)
            raise
        thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        if "error" in result:
            client.close()
            raise result["error"]  # type: ignore[misc]
        server = result["socket"]
        self.assertIsInstance(server, ssl.SSLSocket)
        return server, client  # type: ignore[return-value]

    def test_tls_context_reloader_automatically_accepts_a_valid_pair(self) -> None:
        first_cert, first_key = self._create_certificate_pair("first")
        second_cert, second_key = self._create_certificate_pair("second")
        active_cert = self.root / "active.crt"
        active_key = self.root / "active.key"
        shutil.copyfile(first_cert, active_cert)
        shutil.copyfile(first_key, active_key)
        reloader = ReloadingTLSContext(active_cert, active_key, 0.02)
        original = reloader.current_context()
        reloader.start()
        try:
            shutil.copyfile(second_cert, active_cert)
            shutil.copyfile(second_key, active_key)
            deadline = time.monotonic() + 2
            while (
                reloader.current_context() is original
                and time.monotonic() < deadline
            ):
                time.sleep(0.02)
            self.assertIsNot(reloader.current_context(), original)
        finally:
            reloader.stop()

    def test_tls_context_reloader_rejects_a_mismatched_pair(self) -> None:
        first_cert, first_key = self._create_certificate_pair("first")
        second_cert, second_key = self._create_certificate_pair("second")
        active_cert = self.root / "active.crt"
        active_key = self.root / "active.key"
        shutil.copyfile(first_cert, active_cert)
        shutil.copyfile(first_key, active_key)
        reloader = ReloadingTLSContext(active_cert, active_key)
        original = reloader.current_context()

        shutil.copyfile(second_cert, active_cert)
        self.assertFalse(reloader.reload_if_changed())
        self.assertIs(reloader.current_context(), original)

        shutil.copyfile(second_key, active_key)
        self.assertTrue(reloader.reload_if_changed())
        self.assertIsNot(reloader.current_context(), original)

    def test_tls_context_reloader_keeps_existing_connections(self) -> None:
        first_cert, first_key = self._create_certificate_pair("first")
        second_cert, second_key = self._create_certificate_pair("second")
        active_cert = self.root / "active.crt"
        active_key = self.root / "active.key"
        shutil.copyfile(first_cert, active_cert)
        shutil.copyfile(first_key, active_key)
        reloader = ReloadingTLSContext(active_cert, active_key)
        old_server, old_client = self._tls_socket_pair(reloader.current_context())
        self.addCleanup(old_server.close)
        self.addCleanup(old_client.close)
        old_peer_certificate = old_client.getpeercert(binary_form=True)

        shutil.copyfile(second_cert, active_cert)
        shutil.copyfile(second_key, active_key)
        self.assertTrue(reloader.reload_if_changed())

        old_client.sendall(b"existing")
        self.assertEqual(old_server.recv(8), b"existing")
        new_server, new_client = self._tls_socket_pair(reloader.current_context())
        self.addCleanup(new_server.close)
        self.addCleanup(new_client.close)
        self.assertNotEqual(
            new_client.getpeercert(binary_form=True), old_peer_certificate
        )

    def test_tls_context_reloader_keeps_context_on_read_failure(self) -> None:
        cert, key = self._create_certificate_pair("first")
        reloader = ReloadingTLSContext(cert, key)
        original = reloader.current_context()
        key.unlink()
        self.assertFalse(reloader.reload_if_changed())
        self.assertIs(reloader.current_context(), original)

    def test_handshake_requires_magic_role_scope_and_ticket(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        server, client = socket.socketpair()
        self.addCleanup(server.close)
        self.addCleanup(client.close)
        ticket = "s64ticket_example"
        client.sendall(
            f"{HANDSHAKE_MAGIC} {self.session.id} host {MEDIA_TICKET_SCOPE} {ticket}\n".encode("ascii")
        )
        self.assertEqual(
            relay.read_handshake(server),
            (self.session.id, "host", MEDIA_TICKET_SCOPE, ticket),
        )

    def test_ticket_is_consumed_once_across_manager_instances(self) -> None:
        _, _, ticket = self.manager.issue_ticket(self.session.id, "user-host")
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        results: list[bool] = []
        barrier = threading.Barrier(3)

        def validate() -> None:
            barrier.wait()
            results.append(relay.validate_and_consume_ticket(self.session.id, "host", ticket))

        threads = [threading.Thread(target=validate) for _ in range(2)]
        for thread in threads:
            thread.start()
        barrier.wait()
        for thread in threads:
            thread.join(timeout=2)
        self.assertEqual(sorted(results), [False, True])

    def test_secondary_environment_ticket_is_accepted_by_shared_relay(self) -> None:
        database = AuthorityDatabase(self.root)
        secondary_storage = SQLiteRuntimeRepositories(
            LeagueStorage(self.root / "servers" / "secondary"),
            database,
            "secondary",
        )
        secondary_manager = N64RuntimeMediaSessionManager(secondary_storage)
        session = secondary_manager.create_or_get(
            room_number=65,
            host_user_id="user-host",
            remote_user_id="user-remote",
            host_n64_slot="ROM1",
            host_gb_slot="ROM2",
            remote_gb_slot="ROM3",
            host_n64_rom_id="secondary-n64-rom",
            host_gb_rom_id="secondary-host-gb-rom",
            remote_gb_rom_id="secondary-remote-gb-rom",
            host_n64_save_id="secondary-n64-save",
            host_save_id="secondary-host-save",
            remote_save_id="secondary-remote-save",
            game_type="sample_secondary",
        )
        _, _, ticket = secondary_manager.issue_ticket(session.id, "user-host")
        relay = AuthenticatedN64RuntimeMediaRelay(
            [self.root, self.root / "servers" / "secondary"]
        )
        self.assertTrue(
            relay.validate_credentials(
                session.id, "host", MEDIA_TICKET_SCOPE, ticket
            )
        )

    def test_plain_transport_uses_the_same_authenticated_pairing(self) -> None:
        _, _, host_ticket = self.manager.issue_ticket(
            self.session.id, "user-host"
        )
        _, _, remote_ticket = self.manager.issue_ticket(
            self.session.id, "user-remote"
        )
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        host_server, host_peer = socket.socketpair()
        remote_server, remote_peer = socket.socketpair()
        self.addCleanup(host_peer.close)
        self.addCleanup(remote_peer.close)
        threads = [
            threading.Thread(
                target=relay.handle_client,
                args=(host_server, ("127.0.0.1", 1), None, "plain"),
                daemon=True,
            ),
            threading.Thread(
                target=relay.handle_client,
                args=(remote_server, ("127.0.0.1", 2), None, "plain"),
                daemon=True,
            ),
        ]
        threads[0].start()
        host_peer.sendall(
            f"{HANDSHAKE_MAGIC} {self.session.id} host {MEDIA_TICKET_SCOPE} {host_ticket}\n".encode(
                "ascii"
            )
        )
        self.assertIn(b"AUTHENTICATED", self._recv_line(host_peer))
        threads[1].start()
        remote_peer.sendall(
            f"{HANDSHAKE_MAGIC} {self.session.id} remote {MEDIA_TICKET_SCOPE} {remote_ticket}\n".encode(
                "ascii"
            )
        )
        self.assertIn(b"AUTHENTICATED", self._recv_line(remote_peer))
        self.assertIn(b"PAIRED", self._recv_line(host_peer))
        self.assertIn(b"PAIRED", self._recv_line(remote_peer))
        host_peer.close()
        remote_peer.close()
        for thread in threads:
            thread.join(timeout=2)

    def test_transport_context_mismatch_is_rejected(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        with self.assertRaisesRegex(ValueError, "requires a TLS context"):
            relay.serve("127.0.0.1", 0, None, "tls")
        context = object()
        with self.assertRaisesRegex(ValueError, "must not receive"):
            relay.serve("127.0.0.1", 0, context, "plain")

    def test_product_scope_is_fail_closed_and_does_not_consume_other_tickets(self) -> None:
        invalid_ticket = "invalid-ticket-0123456789abcdef"
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        self.assertFalse(
            relay.validate_credentials(
                self.session.id, "host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, invalid_ticket
            )
        )

        _, role, media_ticket = self.manager.issue_ticket(
            self.session.id, self.session.host_user_id
        )
        self.assertEqual(role, "host")
        self.assertFalse(
            relay.validate_credentials(
                self.session.id, "host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, media_ticket
            )
        )
        self.assertTrue(
            relay.validate_credentials(
                self.session.id, "host", MEDIA_TICKET_SCOPE, media_ticket
            )
        )

    def test_product_gb_runtime_fixed_host_ticket_is_consumed_by_role_and_session(self) -> None:
        manager = GBRuntimeFixedHostSessionManager(self.storage)
        manifest = GBRuntimeFixedHostManifest(
            session_id="link-fixed-product",
            session_epoch=1,
            host_user_id="user-host",
            remote_user_id="user-remote",
            host_save_id="host-save",
            remote_save_id="remote-save",
            host_game_type="sample_alpha",
            remote_game_type="sample_beta",
            host_platform="gb", remote_platform="gb",
            host_rom_header_title="ALPHA CORE", remote_rom_header_title="BETA CORE",
            host_base_revision=1,
            remote_base_revision=1,
            requested_mode="battle",
            save_policy="discard",
            runtime_build_id="integral-gb-runtime-fixed-host-dev",
        )
        manager.create(16, manifest)
        available = [
            {"game_type": "sample_alpha", "platform": "gb", "rom_header_title": "ALPHA CORE"},
            {"game_type": "sample_beta", "platform": "gb", "rom_header_title": "BETA CORE"},
        ]
        for user_id, roms in (
            ("user-host", available),
            ("user-remote", []),
        ):
            manager.submit_preflight(
                manifest.session_id,
                user_id,
                manifest_digest=manifest.digest,
                protocol_id=GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
                runtime_build_id=manifest.runtime_build_id,
                available_roms=roms,
            )
        _, _, host_ticket = manager.issue_ticket(manifest.session_id, "user-host")
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        self.assertFalse(
            relay.validate_credentials(
                manifest.session_id, "remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, host_ticket
            )
        )
        self.assertFalse(
            relay.validate_credentials(
                "another-session", "host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, host_ticket
            )
        )
        self.assertTrue(
            relay.validate_credentials(
                manifest.session_id, "host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, host_ticket
            )
        )
        self.assertFalse(
            relay.validate_credentials(
                manifest.session_id, "host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, host_ticket
            )
        )
        gb_input_payload = struct.pack("!QQ", 0x91, 123456)
        gb_input = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_INPUT, 0, 1, len(gb_input_payload)
        ) + gb_input_payload
        self.assertEqual(
            MediaFrameFilter("remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(gb_input),
            [gb_input],
        )

    def test_product_remote_disconnect_pauses_and_reconnect_resumes_host_socket(self) -> None:
        manager = GBRuntimeFixedHostSessionManager(self.storage)
        manifest = GBRuntimeFixedHostManifest(
            session_id="link-fixed-resume", session_epoch=1,
            host_user_id="resume-host", remote_user_id="resume-remote",
            host_save_id="resume-save-a", remote_save_id="resume-save-b",
            host_game_type="sample_alpha", remote_game_type="sample_beta",
            host_platform="gb", remote_platform="gb",
            host_rom_header_title="ALPHA CORE", remote_rom_header_title="BETA CORE",
            host_base_revision=1, remote_base_revision=1,
            requested_mode="battle", save_policy="discard",
            runtime_build_id="integral-gb-runtime-fixed-host-dev",
        )
        manager.create(16, manifest)
        available = [
            {"game_type": "sample_alpha", "platform": "gb", "rom_header_title": "ALPHA CORE"},
            {"game_type": "sample_beta", "platform": "gb", "rom_header_title": "BETA CORE"},
        ]
        for user_id, roms in (
            ("resume-host", available),
            ("resume-remote", []),
        ):
            manager.submit_preflight(
                manifest.session_id, user_id, manifest_digest=manifest.digest,
                protocol_id=GB_RUNTIME_FIXED_HOST_PROTOCOL_ID,
                runtime_build_id=manifest.runtime_build_id,
                available_roms=roms,
            )
        _, _, host_ticket = manager.issue_ticket(manifest.session_id, "resume-host")
        _, _, remote_ticket = manager.issue_ticket(manifest.session_id, "resume-remote")
        self.assertTrue(manager.validate_ticket(
            manifest.session_id, "host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, host_ticket
        ))
        self.assertTrue(manager.validate_ticket(
            manifest.session_id, "remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, remote_ticket
        ))
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        host_server, host_peer = socket.socketpair()
        remote_server, remote_peer = socket.socketpair()
        self.addCleanup(host_peer.close)
        now = time.monotonic()
        host = MediaRelayClient(
            host_server, ("127.0.0.1", 1), manifest.session_id, "host", now,
            GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
        )
        remote = MediaRelayClient(
            remote_server, ("127.0.0.1", 2), manifest.session_id, "remote", now,
            GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
        )
        bridge = threading.Thread(target=relay.bridge, args=(host, remote), daemon=True)
        bridge.start()
        self.assertIn(b"PAIRED", self._recv_line(host_peer))
        self.assertIn(b"PAIRED", self._recv_line(remote_peer))
        remote_peer.close()
        bridge.join(timeout=2)
        self.assertFalse(bridge.is_alive())
        self.assertEqual(manager.get(manifest.session_id).state, "PAUSED_REMOTE")
        paused = host_peer.recv(24)
        self.assertEqual(paused[5], 9)
        self.assertEqual(struct.unpack("!II", paused[16:24]), (1, 20000))
        _, _, resumed_ticket = manager.issue_ticket(
            manifest.session_id, "resume-remote"
        )
        self.assertTrue(manager.validate_ticket(
            manifest.session_id, "remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE, resumed_ticket
        ))
        remote2_server, remote2_peer = socket.socketpair()
        self.addCleanup(remote2_peer.close)
        remote2 = MediaRelayClient(
            remote2_server, ("127.0.0.1", 3), manifest.session_id, "remote",
            time.monotonic(), GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
        )
        retained_host = relay.register_pending(remote2)
        self.assertIsNotNone(retained_host)
        bridge2 = threading.Thread(
            target=relay.bridge, args=(remote2, retained_host), daemon=True
        )
        bridge2.start()
        # The retained Host is already paired; a second text handshake would
        # corrupt its binary control stream.  It receives only RESUMED.
        resumed_frame = host_peer.recv(24)
        self.assertEqual(resumed_frame[5], 9)
        self.assertEqual(struct.unpack("!II", resumed_frame[16:24]), (2, 0))
        self.assertIn(b"PAIRED", self._recv_line(remote2_peer))
        self.assertEqual(manager.get(manifest.session_id).state, "RUNNING")
        host_peer.close()
        bridge2.join(timeout=2)

    def test_product_terminal_ack_closes_transport_without_aborting_control(self) -> None:
        manager = GBRuntimeFixedHostSessionManager(self.storage)
        manifest = GBRuntimeFixedHostManifest(
            session_id="link-fixed-terminal", session_epoch=1,
            host_user_id="terminal-host", remote_user_id="terminal-remote",
            host_save_id="terminal-save-a", remote_save_id="terminal-save-b",
            host_game_type="sample_alpha", remote_game_type="sample_beta",
            host_platform="gb", remote_platform="gb",
            host_rom_header_title="ALPHA CORE", remote_rom_header_title="BETA CORE",
            host_base_revision=1, remote_base_revision=1,
            requested_mode="trade", save_policy="commit_pair",
            runtime_build_id="integral-gb-runtime-fixed-host-dev",
        )
        manager.create(16, manifest)
        records = self.storage.load_gb_runtime_fixed_host_sessions()
        records[manifest.session_id]["state"] = "RUNNING"
        record = records[manifest.session_id]
        self.storage.update_gb_runtime_fixed_host_session(
            record, int(record["__row_version"])
        )
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        host_server, host_peer = socket.socketpair()
        remote_server, remote_peer = socket.socketpair()
        self.addCleanup(remote_peer.close)
        now = time.monotonic()
        host = MediaRelayClient(
            host_server, ("127.0.0.1", 1), manifest.session_id, "host", now,
            GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
        )
        remote = MediaRelayClient(
            remote_server, ("127.0.0.1", 2), manifest.session_id, "remote", now,
            GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE,
        )
        bridge = threading.Thread(target=relay.bridge, args=(host, remote), daemon=True)
        bridge.start()
        self.assertIn(b"PAIRED", self._recv_line(host_peer))
        self.assertIn(b"PAIRED", self._recv_line(remote_peer))

        terminal_payload = struct.pack("!Q", 4321) + b"t" * 32
        terminal = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_TERMINAL, 0, 4,
            len(terminal_payload),
        ) + terminal_payload
        terminal_ack = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_TERMINAL_ACK, 0, 4,
            len(terminal_payload),
        ) + terminal_payload
        host_peer.sendall(terminal)
        self.assertEqual(self._recv_exact(remote_peer, len(terminal)), terminal)
        remote_peer.sendall(terminal_ack)
        self.assertEqual(self._recv_exact(host_peer, len(terminal_ack)), terminal_ack)
        host_peer.close()
        bridge.join(timeout=2)
        self.assertFalse(bridge.is_alive())
        self.assertEqual(manager.get(manifest.session_id).state, "RUNNING")

    @staticmethod
    def _recv_line(sock: socket.socket) -> bytes:
        data = bytearray()
        sock.settimeout(2)
        while not data.endswith(b"\n"):
            chunk = sock.recv(1)
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)

    @staticmethod
    def _recv_exact(sock: socket.socket, size: int) -> bytes:
        data = bytearray()
        sock.settimeout(2)
        while len(data) < size:
            chunk = sock.recv(size - len(data))
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)

    def test_gb_runtime_fixed_host_directional_control_frames(self) -> None:
        gb_input_payload = struct.pack("!QQ", 0x91, 123456)
        gb_input = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_INPUT, 0, 1, len(gb_input_payload)
        ) + gb_input_payload
        ping_payload = struct.pack("!Q", 123456)
        ping = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_PING, 0, 2, len(ping_payload)
        ) + ping_payload
        self.assertEqual(MediaFrameFilter("remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(gb_input + ping), [gb_input, ping])

        ack_payload = struct.pack("!IIQQ", 1, 0, 123456, 77)
        ack = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_INPUT_ACK, 0, 1, len(ack_payload)
        ) + ack_payload
        pong = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_PONG, 0, 1, len(ping_payload)
        ) + ping_payload
        self.assertEqual(MediaFrameFilter("host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(ack + pong), [ack, pong])

        turbo_payload = struct.pack("!II", 1, 0)
        turbo = struct.pack(
            "!4sBBHII", b"N64R", 2, 10, 0, 3, len(turbo_payload)
        ) + turbo_payload
        with self.assertRaisesRegex(ValueError, "remote may send"):
            MediaFrameFilter("remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(turbo)
        state_payload = struct.pack("!IIII", 2, 3, 7, 0)
        state = struct.pack(
            "!4sBBHII", b"N64R", 2, 11, 0, 3, len(state_payload)
        ) + state_payload
        with self.assertRaisesRegex(ValueError, "host may send"):
            MediaFrameFilter("host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(state)
        terminal_payload = struct.pack("!Q", 4321) + b"t" * 32
        terminal = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_TERMINAL, 0, 4,
            len(terminal_payload),
        ) + terminal_payload
        terminal_ack = struct.pack(
            "!4sBBHII", b"N64R", 2, GB_FIXED_TERMINAL_ACK, 0, 4,
            len(terminal_payload),
        ) + terminal_payload
        self.assertEqual(
            MediaFrameFilter("host", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(terminal),
            [terminal],
        )
        self.assertEqual(
            MediaFrameFilter("remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(terminal_ack),
            [terminal_ack],
        )

        with self.assertRaisesRegex(ValueError, "controller input only"):
            MediaFrameFilter("remote").feed(gb_input)

        with self.assertRaisesRegex(ValueError, "invalid GB input"):
            bad_payload = struct.pack("!QQ", 0x100, 123456)
            MediaFrameFilter("remote", GB_RUNTIME_FIXED_HOST_MEDIA_SCOPE).feed(
                struct.pack("!4sBBHII", b"N64R", 2, GB_FIXED_INPUT, 0, 3, len(bad_payload))
                + bad_payload
            )

    def test_paired_session_activity_tracks_cancel(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        self.assertTrue(relay.session_is_active(self.session.id))
        self.manager.cancel(self.session.id)
        self.assertFalse(relay.session_is_active(self.session.id))

    def test_pairing_never_crosses_session_ids(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        sockets = [socket.socketpair() for _ in range(4)]
        self.addCleanup(lambda: [sock.close() for pair in sockets for sock in pair])
        now = time.monotonic()
        session_a_host = MediaRelayClient(sockets[0][0], ("127.0.0.1", 1), "session-a", "host", now)
        session_b_remote = MediaRelayClient(sockets[1][0], ("127.0.0.1", 2), "session-b", "remote", now)
        session_a_remote = MediaRelayClient(sockets[2][0], ("127.0.0.1", 3), "session-a", "remote", now)

        self.assertIsNone(relay.register_pending(session_a_host))
        self.assertIsNone(relay.register_pending(session_b_remote))
        self.assertIs(relay.register_pending(session_a_remote), session_a_host)
        self.assertNotIn("session-a", relay.pending)
        self.assertIs(relay.pending["session-b"]["remote"], session_b_remote)

    def test_expired_pending_client_is_closed(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay([self.root], pending_timeout_seconds=1.0)
        expired_pair = socket.socketpair()
        current_pair = socket.socketpair()
        self.addCleanup(lambda: [sock.close() for sock in (*expired_pair, *current_pair)])
        expired = MediaRelayClient(
            expired_pair[0], ("127.0.0.1", 1), "old-session", "host", time.monotonic() - 2.0
        )
        current = MediaRelayClient(
            current_pair[0], ("127.0.0.1", 2), "new-session", "host", time.monotonic()
        )
        relay.pending = {"old-session": {"host": expired}}

        self.assertIsNone(relay.register_pending(current))
        self.assertEqual(expired.sock.fileno(), -1)
        self.assertNotIn("old-session", relay.pending)

    def test_tls_context_refuses_missing_credentials(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay([self.root])
        with self.assertRaisesRegex(ValueError, "certificate not found"):
            relay.create_tls_context(self.root / "missing.crt", self.root / "missing.key")

    def test_controller_filter_accepts_split_frames_and_rejects_replay(self) -> None:
        import struct

        first = struct.pack("!4sBBHIIQ", b"N64R", 2, 1, 0, 1, 8, 0x153)
        second = struct.pack("!4sBBHIIQ", b"N64R", 2, 1, 0, 2, 8, 0)
        self.assertEqual(len(first), CONTROL_FRAME_SIZE)
        frame_filter = MediaFrameFilter("remote")
        self.assertEqual(frame_filter.feed(first[:7], now=1.0), [])
        self.assertEqual(frame_filter.feed(first[7:] + second, now=1.05), [first, second])
        with self.assertRaisesRegex(ValueError, "replayed"):
            frame_filter.feed(second, now=1.1)

        analog = struct.pack("!4sBBHIIQ", b"N64R", 2, 1, 0, 3, 8, 1 << 14)
        self.assertEqual(frame_filter.feed(analog, now=1.2), [analog])

    def test_controller_filter_rejects_host_payload_and_unknown_buttons(self) -> None:
        import struct

        frame = struct.pack("!4sBBHIIQ", b"N64R", 2, 1, 0, 1, 8, 1 << 20)
        with self.assertRaisesRegex(ValueError, "H.264 or ADPCM"):
            MediaFrameFilter("host").feed(frame)
        with self.assertRaisesRegex(ValueError, "button mask"):
            MediaFrameFilter("remote").feed(frame)

    def test_host_filter_accepts_split_h264_and_adpcm_frames(self) -> None:
        import struct

        config_payload = struct.pack("!HHHH", 640, 480, 4, 3) + b"sps1pps"
        config = struct.pack(
            "!4sBBHII", b"N64R", 2, MEDIA_H264_CONFIG, 0, 1, len(config_payload)
        ) + config_payload
        video_payload = struct.pack("!Q", 123456) + b"encoded-h264"
        video = struct.pack(
            "!4sBBHII",
            b"N64R",
            2,
            MEDIA_H264_FRAME,
            MEDIA_H264_KEYFRAME,
            2,
            len(video_payload),
        ) + video_payload
        audio_payload = struct.pack("!IHHQ", 44100, 2, 4, 123456) + b"\x00" * 11
        audio = struct.pack(
            "!4sBBHII", b"N64R", 2, MEDIA_AUDIO_ADPCM, 0, 1, len(audio_payload)
        ) + audio_payload
        frame_filter = MediaFrameFilter("host")
        self.assertEqual(frame_filter.feed(config[:9], now=1.0), [])
        self.assertEqual(frame_filter.feed(config[9:] + video + audio, now=1.01), [config, video, audio])

    def test_host_filter_accepts_full_n64_runtime_audio_callback(self) -> None:
        import struct

        frame_count = 4096
        audio_payload = (
            struct.pack("!IHHQ", 44100, 2, frame_count, 123456)
            + b"\x00" * (8 + frame_count - 1)
        )
        self.assertEqual(len(audio_payload), 4119)
        self.assertLessEqual(len(audio_payload), MEDIA_MAX_AUDIO_BYTES)
        audio = struct.pack(
            "!4sBBHII", b"N64R", 2, MEDIA_AUDIO_ADPCM, 0, 1, len(audio_payload)
        ) + audio_payload
        self.assertEqual(MediaFrameFilter("host").feed(audio), [audio])

    def test_direction_and_media_payload_validation_fail_closed(self) -> None:
        import struct

        video_payload = struct.pack("!Q", 1) + b"frame"
        video = struct.pack(
            "!4sBBHII", b"N64R", 2, MEDIA_H264_FRAME, 0, 1, len(video_payload)
        ) + video_payload
        with self.assertRaisesRegex(ValueError, "controller input only"):
            MediaFrameFilter("remote").feed(video)

        bad_audio_payload = struct.pack("!IHHQ", 44100, 1, 4, 1) + b"\x00" * 11
        bad_audio = struct.pack(
            "!4sBBHII", b"N64R", 2, MEDIA_AUDIO_ADPCM, 0, 1, len(bad_audio_payload)
        ) + bad_audio_payload
        with self.assertRaisesRegex(ValueError, "invalid ADPCM"):
            MediaFrameFilter("host").feed(bad_audio)

    def test_stalled_receiver_does_not_block_session_cancel(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay(
            [self.root], forward_queue_bytes=32 * 1024, write_idle_seconds=5.0
        )
        host_relay, host_client = socket.socketpair()
        remote_relay, remote_client = socket.socketpair()
        for sock in (host_relay, host_client, remote_relay, remote_client):
            self.addCleanup(sock.close)
        remote_relay.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024)
        now = time.monotonic()
        host = MediaRelayClient(host_relay, ("127.0.0.1", 1), self.session.id, "host", now)
        remote = MediaRelayClient(remote_relay, ("127.0.0.1", 2), self.session.id, "remote", now)
        bridge = threading.Thread(target=relay.bridge, args=(host, remote))
        bridge.start()
        self.assertIn(b"PAIRED", host_client.recv(256))
        self.assertIn(b"PAIRED", remote_client.recv(256))

        video_payload = struct.pack("!Q", 1) + b"x" * 2048
        frames = b"".join(
            struct.pack(
                "!4sBBHII",
                b"N64R",
                2,
                MEDIA_H264_FRAME,
                0,
                sequence,
                len(video_payload),
            )
            + video_payload
            for sequence in range(1, 17)
        )
        host_client.sendall(frames)
        time.sleep(0.1)
        started = time.monotonic()
        self.manager.cancel(self.session.id)
        bridge.join(timeout=2.0)

        self.assertFalse(bridge.is_alive())
        self.assertLess(time.monotonic() - started, 2.0)

    def test_idle_pair_starts_write_stall_timer_when_first_frame_is_queued(self) -> None:
        relay = AuthenticatedN64RuntimeMediaRelay(
            [self.root], forward_queue_bytes=32 * 1024, write_idle_seconds=0.05
        )
        host_relay, host_client = socket.socketpair()
        remote_relay, remote_client = socket.socketpair()
        for sock in (host_relay, host_client, remote_relay, remote_client):
            self.addCleanup(sock.close)
        now = time.monotonic()
        host = MediaRelayClient(
            host_relay, ("127.0.0.1", 1), self.session.id, "host", now
        )
        remote = MediaRelayClient(
            remote_relay, ("127.0.0.1", 2), self.session.id, "remote", now
        )
        bridge = threading.Thread(target=relay.bridge, args=(host, remote))
        bridge.start()
        self.assertIn(b"PAIRED", host_client.recv(256))
        self.assertIn(b"PAIRED", remote_client.recv(256))

        time.sleep(0.1)
        input_payload = struct.pack("!Q", 0x91)
        input_frame = struct.pack(
            "!4sBBHII", b"N64R", 2, CONTROL_INPUT, 0, 1, len(input_payload)
        ) + input_payload
        remote_client.sendall(input_frame)
        host_client.settimeout(1.0)
        self.assertEqual(host_client.recv(len(input_frame)), input_frame)

        self.manager.cancel(self.session.id)
        bridge.join(timeout=2.0)
        self.assertFalse(bridge.is_alive())


if __name__ == "__main__":
    unittest.main()
