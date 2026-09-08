from __future__ import annotations

import base64
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from integral_emulator.api import LeagueApplication
from integral_emulator.errors import (
    MobileCreateAuthSessionConflictError,
    RevisionConflictError,
    ValidationError,
)


ROM_SHA256 = "27a07a1d3faf9c6a0b1b60d5e88ee3a4159a751a47b4c46ab09f1202d52bac3e"
ROM_SHA1 = "a222402235d484ee8e39f3f31bae57cf13daf585"
FIXTURE_PACKAGE_ROOT = Path(__file__).resolve().parents[1] / "test_fixtures" / "gb_mobile" / "packages"


def encoded(value: bytes) -> str:
    return base64.b64encode(value).decode("ascii")


class MobileSessionApiV2Tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.package_environment = patch.dict(os.environ, {
            "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_PACKAGE_ROOT": str(FIXTURE_PACKAGE_ROOT),
        })
        self.package_environment.start()
        self.addCleanup(self.package_environment.stop)
        self.app = LeagueApplication(Path(self.temporary.name))
        self.app.allow_self_registration = True
        self.app.handle_request("POST", "/auth/register", {"username": "Mobile_V2", "password": "mobile v2 test password"})
        self.token = self.app.handle_request("POST", "/auth/login", {"username": "mobile_v2", "password": "mobile v2 test password"})["token"]["token"]
        result = self.request("POST", "/rom-slots/apply", {"slots": [{
            "slot": 1,
            "filename": "owned-test.gbc",
            "sha256": ROM_SHA256,
            "sha1": ROM_SHA1,
            "platform": "gb",
            "region": "JP",
            "rom_header_title": "INTEGRAL DEMO A",
        }]})
        self.slot = result["slots"][0]
        self.create_sequence = 0

    def tearDown(self):
        self.temporary.cleanup()

    def request(self, method, path, body=None):
        return self.app.handle_request(method, path, body or {}, self.token)

    def create(self):
        self.create_sequence += 1
        result = self.request("POST", "/mobile-sessions", {
            "save_id": self.slot["save_id"], "rom_id": self.slot["rom_id"],
            "request_id": f"mobile-create:test-{self.create_sequence}",
        })
        save = self.app.saves.get_save(self.slot["save_id"])
        result["mobile_session"]["_test_base_revision"] = save.revision
        result["mobile_session"]["_test_base_sha256"] = save.sha256
        return result

    def rows(self, table):
        with self.app.authority_database.transaction() as connection:
            return [dict(row) for row in connection.execute(f"SELECT * FROM {table}")]

    def expire(self, *session_ids):
        expired_ms = int((datetime.now(timezone.utc) - timedelta(seconds=1)).timestamp() * 1000)
        with self.app.authority_database.transaction(write=True) as connection:
            connection.executemany(
                "UPDATE mobile_sessions SET lease_expires_at_ms = ? WHERE session_id = ?",
                [(expired_ms, session_id) for session_id in session_ids],
            )

    def test_mobile_scenarios_are_server_selected_and_bound_to_session(self):
        listed = self.request("POST", "/mobile-scenarios", {
            "save_id": self.slot["save_id"], "rom_id": self.slot["rom_id"],
        })
        self.assertEqual(listed["rom_header_title"], "INTEGRAL DEMO A")
        self.assertEqual(listed["scenarios"], [{
            "scenario_id": "default",
            "display_name": "DEFAULT",
            "release_id": "2026-09-02.1",
            "default": True,
        }])
        created = self.request("POST", "/mobile-sessions", {
            "save_id": self.slot["save_id"], "rom_id": self.slot["rom_id"],
            "request_id": "mobile-create:scenario-default",
            "scenario_id": "default",
        })
        self.assertEqual(created["mobile_session"]["scenario_id"], "default")
        self.assertEqual(created["mobile_session"]["scenario_display_name"], "DEFAULT")

    def test_mobile_create_rejects_scenario_from_another_title(self):
        with self.assertRaisesRegex(ValidationError, "scenario is not available"):
            self.request("POST", "/mobile-sessions", {
                "save_id": self.slot["save_id"], "rom_id": self.slot["rom_id"],
                "request_id": "mobile-create:wrong-scenario",
                "scenario_id": "unavailable_scenario",
            })

    def test_create_response_loss_replays_same_session_without_new_authority(self):
        body = {
            "save_id": self.slot["save_id"],
            "rom_id": self.slot["rom_id"],
            "request_id": "mobile-create:response-loss",
        }
        raised = False

        def lose_once(point):
            nonlocal raised
            if point == "before_response" and not raised:
                raised = True
                raise RuntimeError("injected response loss")

        self.app.mobile_sessions.fault_injector = lose_once
        with self.assertRaisesRegex(RuntimeError, "response loss"):
            self.request("POST", "/mobile-sessions", body)
        records_before = self.rows("mobile_sessions")
        replay = self.request("POST", "/mobile-sessions", body)
        self.assertTrue(replay["idempotent_replay"])
        self.assertEqual(len(records_before), 1)
        self.assertEqual(
            replay["mobile_session"]["id"], records_before[0]["session_id"]
        )
        self.assertEqual(len(self.rows("mobile_sessions")), 1)
        self.assertNotIn("gateway_token", replay)

    def test_create_replay_is_bound_to_the_original_login_session(self):
        body = {
            "save_id": self.slot["save_id"],
            "rom_id": self.slot["rom_id"],
            "request_id": "mobile-create:auth-session-boundary",
        }
        created = self.request("POST", "/mobile-sessions", body)
        other_token = self.app.handle_request(
            "POST", "/auth/login",
            {"username": "mobile_v2", "password": "mobile v2 test password"},
        )["token"]["token"]
        before = {
            "sessions": self.rows("mobile_sessions"),
            "locks": self.rows("game_session_locks"),
        }
        with self.assertRaises(MobileCreateAuthSessionConflictError) as caught:
            self.app.handle_request(
                "POST", "/mobile-sessions", body, other_token
            )
        self.assertNotIn(created["mobile_session"]["id"], str(caught.exception))
        self.assertEqual(
            before["sessions"], self.rows("mobile_sessions")
        )
        self.assertEqual(
            before["locks"], self.rows("game_session_locks")
        )

    def test_terminal_create_replay_returns_terminal_result_only(self):
        body = {
            "save_id": self.slot["save_id"],
            "rom_id": self.slot["rom_id"],
            "request_id": "mobile-create:terminal-replay",
        }
        created = self.request("POST", "/mobile-sessions", body)
        session = created["mobile_session"]
        self.request("POST", f"/mobile-sessions/{session['id']}/cancel", {
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
        })
        replay = self.request("POST", "/mobile-sessions", body)
        self.assertTrue(replay["idempotent_replay"])
        self.assertEqual(replay["mobile_session"]["status"], "CANCELLED")
        self.assertNotIn("working_save_data", replay)
        self.assertNotIn("runtime_contract", replay)

    def test_periodic_uploads_share_local_save_path_and_complete_releases_lock(self):
        created = self.create()
        session = created["mobile_session"]
        contract = created["runtime_contract"]
        self.assertEqual(contract["schema_version"], 2)
        self.assertEqual(contract["adapter_id"], "gb_mobile_v2")
        self.assertEqual(contract["package_id"], "synthetic_numbers_a")
        self.assertNotIn("profile_id", contract)
        self.assertNotIn("receipt_schema_id", contract)
        for artifact in contract["artifacts"]:
            payload = base64.b64decode(artifact["data"])
            self.assertEqual(len(payload), artifact["size"])
            self.assertEqual(hashlib.sha256(payload).hexdigest(), artifact["sha256"])

        changed = bytes([0x22]) * 32768
        upload_body = {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(changed),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-periodic-r1",
        }
        uploaded = self.request("PUT", f"/saves/{session['save_id']}", upload_body)
        replay = self.request("PUT", f"/saves/{session['save_id']}", upload_body)
        self.assertEqual(uploaded["save"]["revision"], 2)
        self.assertTrue(replay["idempotent_replay"])
        self.assertIsNone(uploaded["save"]["lock_owner"])

        changed_again = bytes([0x23]) * 32768
        second = self.request("PUT", f"/saves/{session['save_id']}", {
            **upload_body,
            "expected_revision": 2,
            "save_data": encoded(changed_again),
            "request_id": "save-periodic-r2",
        })
        self.assertEqual(second["save"]["revision"], 3)

        complete_body = {
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
        }
        completed = self.request("POST", f"/mobile-sessions/{session['id']}/complete", complete_body)
        completed_replay = self.request("POST", f"/mobile-sessions/{session['id']}/complete", complete_body)
        self.assertEqual(completed["mobile_session"]["status"], "COMPLETED")
        self.assertEqual(completed_replay["mobile_session"]["status"], "COMPLETED")
        with self.assertRaisesRegex(ValidationError, "stale Mobile session fence"):
            self.request("POST", f"/mobile-sessions/{session['id']}/complete", {
                **complete_body,
                "fencing_token": session["fencing_token"] + 1,
            })
        self.assertIsNone(self.app.saves.get_save(session["save_id"]).lock_owner)
        self.assertEqual(self.rows("game_session_locks"), [])

    def test_mobile_authority_persists_only_auth_session_digest(self):
        created = self.create()
        session = created["mobile_session"]
        expected_digest = hashlib.sha256(self.token.encode("utf-8")).hexdigest()
        mobile_records = self.rows("mobile_sessions")
        lock_records = self.rows("game_session_locks")
        serialized = json.dumps(
            {"mobile_sessions": mobile_records, "game_session_locks": lock_records},
            sort_keys=True,
        )
        self.assertNotIn(self.token, serialized)
        self.assertEqual(
            mobile_records[0]["auth_session_id"], expected_digest
        )
        self.assertEqual(lock_records[0]["auth_session_id"], expected_digest)

    def test_unchanged_complete(self):
        created = self.create()
        session = created["mobile_session"]
        completed = self.request("POST", f"/mobile-sessions/{session['id']}/complete", {
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
        })
        self.assertEqual(completed["mobile_session"]["status"], "COMPLETED")

    def test_heartbeat_then_cancel_releases_save_and_game_locks(self):
        session = self.create()["mobile_session"]
        heartbeat = self.request("POST", f"/mobile-sessions/{session['id']}/heartbeat", {
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
        })["mobile_session"]
        self.assertEqual(heartbeat["status"], "RUNNING")
        cancelled = self.request("POST", f"/mobile-sessions/{session['id']}/cancel", {
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "reason": "acceptance cancellation",
        })["mobile_session"]
        self.assertEqual(cancelled["status"], "CANCELLED")
        self.assertIsNone(self.app.saves.get_save(session["save_id"]).lock_owner)
        self.assertIsNone(
            self.app.sessions.active_game_session_for_user(cancelled["user_id"])
        )
        self.assertEqual(self.rows("game_session_locks"), [])

    def test_expiry_without_upload_releases_authority_without_save_change(self):
        session = self.create()["mobile_session"]
        self.expire(session["id"])
        expired = self.app.mobile_sessions.expire_due(datetime.now(timezone.utc))
        self.assertEqual(expired[0].status, "EXPIRED")
        save = self.app.saves.get_save(session["save_id"])
        self.assertEqual(save.revision, session["_test_base_revision"])
        self.assertEqual(save.sha256, session["_test_base_sha256"])
        self.assertIsNone(save.lock_owner)
        self.assertEqual(self.rows("game_session_locks"), [])

    def test_upload_request_id_cannot_change_authority_or_payload(self):
        session = self.create()["mobile_session"]
        body = {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(bytes([0x33]) * 32768),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-reused-request",
        }
        self.request("PUT", f"/saves/{session['save_id']}", body)
        body["save_data"] = encoded(bytes([0x44]) * 32768)
        with self.assertRaisesRegex(ValidationError, "different save upload"):
            self.request("PUT", f"/saves/{session['save_id']}", body)

    def test_mobile_session_accepts_successive_revisioned_uploads(self):
        session = self.create()["mobile_session"]
        first = {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(bytes([0x31]) * 32768),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-successive-r1",
        }
        self.request("PUT", f"/saves/{session['save_id']}", first)
        before = self.app.saves.get_save(session["save_id"])
        second = {
            **first,
            "expected_revision": before.revision,
            "save_data": encoded(bytes([0x32]) * 32768),
            "request_id": "save-successive-r2",
        }
        result = self.request("PUT", f"/saves/{session['save_id']}", second)
        after = self.app.saves.get_save(session["save_id"])
        self.assertEqual(result["save"]["revision"], before.revision + 1)
        self.assertEqual(after.sha256, hashlib.sha256(bytes([0x32]) * 32768).hexdigest())

    def test_mobile_upload_requires_current_revision(self):
        session = self.create()["mobile_session"]
        with self.assertRaisesRegex(RevisionConflictError, "current revision"):
            self.request("PUT", f"/saves/{session['save_id']}", {
                "expected_revision": session["_test_base_revision"] + 1,
                "save_data": encoded(bytes([0x33]) * 32768),
                "game_session_id": session["game_session_id"],
                "fencing_token": session["fencing_token"],
                "request_id": "save-stale-revision",
            })

    def test_expiry_never_reconciles_or_rolls_back_committed_upload(self):
        session = self.create()["mobile_session"]
        changed = bytes([0x55]) * 32768
        self.request("PUT", f"/saves/{session['save_id']}", {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(changed),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-before-expiry",
        })
        self.expire(session["id"])
        expired = self.app.mobile_sessions.expire_due(datetime.now(timezone.utc))
        self.assertEqual(expired[0].status, "EXPIRED")
        self.assertEqual(self.app.saves.get_save(session["save_id"]).sha256,
                         hashlib.sha256(changed).hexdigest())

    def test_multiple_mobile_sessions_expire_independently(self):
        first = self.create()["mobile_session"]
        first_bytes = bytes([0x56]) * 32768
        self.request("PUT", f"/saves/{first['save_id']}", {
            "expected_revision": first["_test_base_revision"],
            "save_data": encoded(first_bytes),
            "game_session_id": first["game_session_id"],
            "fencing_token": first["fencing_token"],
            "request_id": "save-first-before-expiry",
        })

        self.app.handle_request("POST", "/auth/register", {
            "username": "Mobile_V2_Second", "password": "mobile v2 second password",
        })
        second_token = self.app.handle_request("POST", "/auth/login", {
            "username": "mobile_v2_second", "password": "mobile v2 second password",
        })["token"]["token"]
        second_slot = self.app.handle_request("POST", "/rom-slots/apply", {"slots": [{
            "slot": 1,
            "filename": "owned-test.gbc",
            "sha256": ROM_SHA256,
            "sha1": ROM_SHA1,
            "platform": "gb",
            "region": "JP",
            "rom_header_title": "INTEGRAL DEMO A",
        }]}, second_token)["slots"][0]
        second = self.app.handle_request("POST", "/mobile-sessions", {
            "save_id": second_slot["save_id"], "rom_id": second_slot["rom_id"],
            "request_id": "mobile-create:second-user",
        }, second_token)["mobile_session"]

        self.expire(first["id"], second["id"])

        results = self.app.mobile_sessions.expire_due(datetime.now(timezone.utc))
        self.assertIn(second["id"], {record.id for record in results})
        self.assertEqual(
            self.app.mobile_sessions.get(second["id"], second["user_id"]).status,
            "EXPIRED",
        )
        self.assertEqual(
            self.app.mobile_sessions.get(first["id"], first["user_id"]).status,
            "EXPIRED",
        )

    def test_lifecycle_action_reports_expiry_without_save_reconciliation(self):
        session = self.create()["mobile_session"]
        changed = bytes([0x57]) * 32768
        self.request("PUT", f"/saves/{session['save_id']}", {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(changed),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-lifecycle-expiry",
        })
        self.expire(session["id"])
        actions = self.app.reconcile_session_lifecycle(datetime.now(timezone.utc))
        action = next(
            item for item in actions if item.get("mobile_session_id") == session["id"]
        )
        self.assertEqual(action["status"], "EXPIRED")
        self.assertNotIn("completion_source", action)

    def test_prepared_journal_retry_converges_to_one_commit(self):
        session = self.create()["mobile_session"]
        changed = bytes([0x66]) * 32768
        body = {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(changed),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-prepared-retry",
        }
        raised = False

        def fail_once(point):
            nonlocal raised
            if point == "after_prepare" and not raised:
                raised = True
                raise RuntimeError("injected after PREPARED")

        self.app.save_uploads.fault_injector = fail_once
        with self.assertRaisesRegex(RuntimeError, "PREPARED"):
            self.request("PUT", f"/saves/{session['save_id']}", body)
        result = self.request("PUT", f"/saves/{session['save_id']}", body)
        self.assertEqual(result["save"]["revision"], 2)
        self.assertEqual(self.app.saves.get_save(session["save_id"]).revision, 2)

    def test_lost_upload_response_replays_committed_result(self):
        session = self.create()["mobile_session"]
        changed = bytes([0x77]) * 32768
        body = {
            "expected_revision": session["_test_base_revision"],
            "save_data": encoded(changed),
            "game_session_id": session["game_session_id"],
            "fencing_token": session["fencing_token"],
            "request_id": "save-response-retry",
        }
        raised = False

        def lose_once(point):
            nonlocal raised
            if point == "before_response" and not raised:
                raised = True
                raise RuntimeError("injected response loss")

        self.app.save_uploads.fault_injector = lose_once
        with self.assertRaisesRegex(RuntimeError, "response loss"):
            self.request("PUT", f"/saves/{session['save_id']}", body)
        result = self.request("PUT", f"/saves/{session['save_id']}", body)
        self.assertTrue(result["idempotent_replay"])
        self.assertEqual(result["save"]["revision"], 2)

    def test_atomic_replace_and_metadata_faults_roll_forward_once(self):
        for fault_point in ("after_atomic_replace", "after_metadata_update"):
            with self.subTest(fault_point=fault_point):
                self.tearDown()
                self.setUp()
                session = self.create()["mobile_session"]
                changed = bytes([0x88]) * 32768
                body = {
                    "expected_revision": session["_test_base_revision"],
                    "save_data": encoded(changed),
                    "game_session_id": session["game_session_id"],
                    "fencing_token": session["fencing_token"],
                    "request_id": f"save-atomic-{fault_point}",
                }
                raised = False

                def fail_once(point):
                    nonlocal raised
                    if point == fault_point and not raised:
                        raised = True
                        raise RuntimeError(f"injected {fault_point}")

                self.app.save_uploads.fault_injector = fail_once
                with self.assertRaisesRegex(RuntimeError, fault_point):
                    self.request("PUT", f"/saves/{session['save_id']}", body)
                result = self.request("PUT", f"/saves/{session['save_id']}", body)
                self.assertEqual(result["save"]["revision"], 2)
                self.assertEqual(self.app.saves.get_save(session["save_id"]).revision, 2)

if __name__ == "__main__":
    unittest.main()
