from __future__ import annotations

from datetime import datetime, timedelta, timezone
from concurrent.futures import ThreadPoolExecutor
import hashlib
import tempfile
import unittest
from pathlib import Path

from integral_emulator.api import LeagueApplication
from integral_emulator.gb_runtime_fixed_host_protocol import GBRuntimeFixedHostManifest
from integral_emulator.sessions import LinkSessionStatus
from integral_emulator.errors import ValidationError


class SQLiteRuntimeStorageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.app = LeagueApplication(self.root)
        self.app.allow_self_registration = True
        self.tokens: dict[str, str] = {}
        self.users = {}
        for name in ("runtime-a", "runtime-b"):
            self.app.handle_request(
                "POST", "/auth/register",
                {"username": name, "password": "correct horse battery staple"},
            )
            token = self.app.handle_request(
                "POST", "/auth/login",
                {"username": name, "password": "correct horse battery staple"},
            )["token"]["token"]
            self.tokens[name] = token
            self.users[name] = self.app.auth.require_user(token)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_runtime_authority_survives_restart_without_dynamic_json(self) -> None:
        user_a = self.users["runtime-a"]
        user_b = self.users["runtime-b"]
        save_a = self.app.saves.create_save(user_a.id, "sample_alpha", b"a" * 32768)
        save_b = self.app.saves.create_save(user_b.id, "sample_beta", b"b" * 32768)
        expires = (datetime.now(timezone.utc) + timedelta(minutes=5)).isoformat()
        auth_ids = {
            user_a.id: hashlib.sha256(self.tokens["runtime-a"].encode()).hexdigest(),
            user_b.id: hashlib.sha256(self.tokens["runtime-b"].encode()).hexdigest(),
        }
        link = self.app.sessions.create_session(
            user_a.id, user_b.id, save_a.id, save_b.id, expires,
            room_number=1, link_mode="battle", protocol_id="gb_runtime_fixed_host_v1",
            auth_session_ids=auth_ids,
        )
        running = self.app.sessions.transition(link.id, LinkSessionStatus.RUNNING)
        self.assertEqual(running.status, "RUNNING")
        self.assertTrue(self.app.sessions.list_events(link.id))

        manifest = GBRuntimeFixedHostManifest(
            session_id=link.id, session_epoch=1,
            host_user_id=user_a.id, remote_user_id=user_b.id,
            host_save_id=save_a.id, remote_save_id=save_b.id,
            host_game_type="sample_alpha", remote_game_type="sample_beta",
            host_platform="gb", remote_platform="gb",
            host_rom_header_title="ALPHA CORE", remote_rom_header_title="BETA CORE",
            host_base_revision=1, remote_base_revision=1,
            requested_mode="battle", save_policy="discard",
            runtime_build_id="integral-gb-runtime-fixed-host-v2",
        )
        fixed = self.app.gb_runtime_fixed_host_sessions.create(1, manifest)
        self.assertEqual(fixed.state, "PREFLIGHT")
        media = self.app.n64_runtime_media_sessions.create_or_get(
            room_number=33, host_user_id=user_a.id, remote_user_id=user_b.id,
            host_n64_slot="N64ROM1", host_gb_slot="ROM1", remote_gb_slot="ROM1",
            host_n64_rom_id="n64-rom", host_gb_rom_id="host-rom",
            remote_gb_rom_id="remote-rom", host_n64_save_id="n64-save",
            host_save_id=save_a.id, remote_save_id=save_b.id, game_type="sample_alpha",
        )
        process = self.app.host_processes.create_run(link.id, ["/bin/true"])

        restarted = LeagueApplication(self.root)
        self.assertEqual(restarted.sessions.get_session(link.id).status, "RUNNING")
        self.assertEqual(restarted.gb_runtime_fixed_host_sessions.get(link.id).manifest_digest, manifest.digest)
        self.assertEqual(restarted.n64_runtime_media_sessions.get(media.id).room_number, 33)
        self.assertEqual(restarted.host_processes.get(process.id).status, "PLANNED")
        for name in (
            "link_sessions.json", "gb_runtime_fixed_host_sessions.json",
            "n64_runtime_media_sessions.json", "host_processes.json",
            "session_events.json",
        ):
            self.assertFalse((self.root / "data" / name).exists())

    def test_distinct_records_do_not_erase_each_other_under_concurrent_writes(self) -> None:
        timestamp = datetime.now(timezone.utc).isoformat()

        def fixed_record(session_id: str, room_number: int) -> dict:
            return {
                "id": session_id,
                "room_number": room_number,
                "manifest": {"session_id": session_id},
                "manifest_digest": hashlib.sha256(session_id.encode()).hexdigest(),
                "state": "PREFLIGHT",
                "created_at": timestamp,
                "updated_at": timestamp,
                "termination_reason": None,
                "preflights": {},
            }

        records = (fixed_record("concurrent-a", 14), fixed_record("concurrent-b", 15))
        with ThreadPoolExecutor(max_workers=2) as executor:
            list(executor.map(self.app.storage.insert_gb_runtime_fixed_host_session, records))
        loaded_a = self.app.storage.get_gb_runtime_fixed_host_session("concurrent-a")
        loaded_b = self.app.storage.get_gb_runtime_fixed_host_session("concurrent-b")
        self.assertIsNotNone(loaded_a)
        self.assertIsNotNone(loaded_b)

        loaded_a["state"] = "READY"
        loaded_b["state"] = "WAITING_PEER"
        with ThreadPoolExecutor(max_workers=2) as executor:
            futures = (
                executor.submit(
                    self.app.storage.update_gb_runtime_fixed_host_session,
                    loaded_a,
                    int(loaded_a["__row_version"]),
                ),
                executor.submit(
                    self.app.storage.update_gb_runtime_fixed_host_session,
                    loaded_b,
                    int(loaded_b["__row_version"]),
                ),
            )
            for future in futures:
                future.result()
        current_a = self.app.storage.get_gb_runtime_fixed_host_session("concurrent-a")
        current_b = self.app.storage.get_gb_runtime_fixed_host_session("concurrent-b")
        self.assertEqual(current_a["state"], "READY")
        self.assertEqual(current_b["state"], "WAITING_PEER")

        current_b["state"] = "RUNNING"
        with ThreadPoolExecutor(max_workers=2) as executor:
            delete = executor.submit(
                self.app.storage.delete_gb_runtime_fixed_host_session,
                "concurrent-a",
                int(current_a["__row_version"]),
            )
            update = executor.submit(
                self.app.storage.update_gb_runtime_fixed_host_session,
                current_b,
                int(current_b["__row_version"]),
            )
            delete.result()
            update.result()
        self.assertIsNone(self.app.storage.get_gb_runtime_fixed_host_session("concurrent-a"))
        self.assertEqual(
            self.app.storage.get_gb_runtime_fixed_host_session("concurrent-b")["state"],
            "RUNNING",
        )

    def test_update_rejects_a_stale_row_version(self) -> None:
        timestamp = datetime.now(timezone.utc).isoformat()
        record = {
            "id": "cas-session", "room_number": 13,
            "manifest": {"session_id": "cas-session"},
            "manifest_digest": hashlib.sha256(b"cas-session").hexdigest(),
            "state": "PREFLIGHT", "created_at": timestamp, "updated_at": timestamp,
            "termination_reason": None, "preflights": {},
        }
        original = self.app.storage.insert_gb_runtime_fixed_host_session(record)
        first = dict(original)
        first["state"] = "READY"
        self.app.storage.update_gb_runtime_fixed_host_session(
            first, int(original["__row_version"])
        )
        stale = dict(original)
        stale["state"] = "ABORTED"
        with self.assertRaisesRegex(ValidationError, "changed concurrently"):
            self.app.storage.update_gb_runtime_fixed_host_session(
                stale, int(original["__row_version"])
            )


if __name__ == "__main__":
    unittest.main()
