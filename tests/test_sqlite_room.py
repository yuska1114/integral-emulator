from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from integral_emulator.database import AuthorityDatabase
from integral_emulator.errors import (
    RoomCodeUnavailableError,
    RoomPoolFullError,
)
from integral_emulator.room_session_policy import RoomSessionPolicy
from integral_emulator.room import parse_enabled_room_numbers
from integral_emulator.errors import ValidationError
from integral_emulator.sqlite_room import SQLiteRoomManager


class SQLiteRoomManagerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.database = AuthorityDatabase(Path(self.temporary.name))
        self.database.initialize()
        self.room_manager = SQLiteRoomManager(self.database, "primary")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def seed_user(self, name: str) -> str:
        user_id = f"user-{name}"
        token = f"token-{name}"
        now_ms = self.room_manager._now_ms()
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "INSERT INTO users VALUES (?, ?, ?, NULL, 'hash', 'active', 0, ?, ?)",
                (user_id, name, name.casefold(), now_ms, now_ms),
            )
            connection.execute(
                """
                INSERT INTO auth_sessions (
                    token_digest, user_id, server_id, created_at_ms,
                    expires_at_ms, last_access_at_ms
                ) VALUES (?, ?, 'primary', ?, ?, ?)
                """,
                (token, user_id, now_ms, now_ms + 86_400_000, now_ms),
            )
        return token

    def test_create_join_and_public_shape(self) -> None:
        owner_token = self.seed_user("owner")
        guest_token = self.seed_user("guest")
        created = self.room_manager.create_room("link_cable", "user-owner", "owner", owner_token)
        self.assertEqual(len(created.room_code or ""), 5)
        joined = self.room_manager.join_room_by_code(
            created.room_code or "", "user-guest", "guest", guest_token
        )
        self.assertEqual([user["username"] for user in joined.users], ["owner", "guest"])
        self.assertEqual(joined.users[0]["auth_session_id_digest"], owner_token)
        self.assertEqual(len(self.room_manager.list_rooms()), 32)

    def test_sparse_room_configuration_controls_listing_and_allocation(self) -> None:
        manager = SQLiteRoomManager(
            self.database,
            "primary",
            enabled_link_rooms=(2, 64),
            enabled_n64_rooms=(65, 128),
        )
        tokens = [self.seed_user(f"owner-{index}") for index in range(4)]
        link_rooms = {
            manager.create_room(
                "link_cable", f"user-owner-{index}", f"owner-{index}", tokens[index]
            ).room_number
            for index in range(2)
        }
        n64_rooms = {
            manager.create_room(
                "n64", f"user-owner-{index}", f"owner-{index}", tokens[index]
            ).room_number
            for index in range(2, 4)
        }
        self.assertEqual(link_rooms, {2, 64})
        self.assertEqual(n64_rooms, {65, 128})
        self.assertEqual(
            [room.room_number for room in manager.list_rooms()], [2, 64, 65, 128]
        )
        with self.assertRaisesRegex(ValidationError, "disabled"):
            manager.room(1)

    def test_room_configuration_parser_is_strict_and_supports_empty_pool(self) -> None:
        self.assertEqual(
            parse_enabled_room_numbers(
                "1-3, 8, 10-11", minimum=1, maximum=64, setting_name="ROOMS"
            ),
            (1, 2, 3, 8, 10, 11),
        )
        self.assertEqual(
            parse_enabled_room_numbers(
                "", minimum=65, maximum=128, setting_name="ROOMS"
            ),
            (),
        )
        for invalid in ("0", "65", "4-2", "1,1", "1-3,3-4", "one"):
            with self.subTest(invalid=invalid), self.assertRaises(ValidationError):
                parse_enabled_room_numbers(
                    invalid, minimum=1, maximum=64, setting_name="ROOMS"
                )

    def test_empty_pool_is_full_and_disabling_an_active_room_fails_closed(self) -> None:
        empty = SQLiteRoomManager(
            self.database,
            "primary",
            enabled_link_rooms=(),
            enabled_n64_rooms=(),
        )
        token = self.seed_user("empty-owner")
        with self.assertRaises(RoomPoolFullError):
            empty.create_room(
                "link_cable", "user-empty-owner", "empty-owner", token
            )

        active = SQLiteRoomManager(
            self.database,
            "primary",
            enabled_link_rooms=(2,),
            enabled_n64_rooms=(),
        )
        active.create_room("link_cable", "user-empty-owner", "empty-owner", token)
        with self.assertRaisesRegex(ValidationError, "active ROOMs are disabled"):
            SQLiteRoomManager(
                self.database,
                "primary",
                enabled_link_rooms=(1,),
                enabled_n64_rooms=(),
            )

    def test_parallel_join_has_one_winner(self) -> None:
        owner_token = self.seed_user("owner")
        room = self.room_manager.create_room("link_cable", "user-owner", "owner", owner_token)
        guests = [(f"user-guest-{index}", self.seed_user(f"guest-{index}")) for index in range(32)]

        def join(guest: tuple[str, str]) -> bool:
            try:
                self.room_manager.join_room_by_code(room.room_code or "", guest[0], guest[0], guest[1])
                return True
            except RoomCodeUnavailableError:
                return False

        with ThreadPoolExecutor(max_workers=16) as executor:
            results = list(executor.map(join, guests))
        self.assertEqual(sum(results), 1)
        self.assertEqual(len(self.room_manager.room(room.room_number).users), 2)

    def test_parallel_link_room_allocation_fills_pool_without_duplicates(self) -> None:
        owners = [
            (f"user-owner-{index}", self.seed_user(f"owner-{index}"))
            for index in range(17)
        ]

        def create(owner: tuple[str, str]):
            return self.room_manager.create_room("link_cable", owner[0], owner[0], owner[1])

        with ThreadPoolExecutor(max_workers=16) as executor:
            rooms = list(executor.map(create, owners[:16]))
        self.assertEqual(len({room.room_number for room in rooms}), 16)
        self.assertEqual(len({room.room_code for room in rooms}), 16)
        with self.assertRaises(RoomPoolFullError):
            create(owners[16])

    def test_heartbeat_does_not_slide_activity_deadline(self) -> None:
        token = self.seed_user("owner")
        room = self.room_manager.create_room("link_cable", "user-owner", "owner", token)
        before = self.room_manager.room(room.room_number).users[0]["last_activity_at"]
        self.assertTrue(self.room_manager.heartbeat("user-owner", token))
        after = self.room_manager.room(room.room_number).users[0]["last_activity_at"]
        self.assertEqual(after, before)

    def test_state_chat_end_and_creator_close(self) -> None:
        owner_token = self.seed_user("owner")
        guest_token = self.seed_user("guest")
        room = self.room_manager.create_room("n64", "user-owner", "owner", owner_token)
        self.room_manager.join_room_by_code(room.room_code or "", "user-guest", "guest", guest_token)
        changed = self.room_manager.update_user_state(
            room.room_number, "user-owner", slot="ROM2", n64_slot="ROM1", ready=True
        )
        self.assertEqual(changed.users[0]["n64_slot"], "ROM1")
        chatted = self.room_manager.add_chat(room.room_number, "user-owner", "owner", "hello")
        self.assertEqual(chatted.chat[-1]["message"], "hello")
        self.room_manager.set_game_started(room.room_number, True)
        self.assertTrue(self.room_manager.room(room.room_number).game_started)
        self.room_manager.end_game_for_room(room.room_number)
        self.assertFalse(self.room_manager.room(room.room_number).game_started)
        self.room_manager.leave_room("user-owner")
        self.assertIsNone(self.room_manager.current_room("user-guest"))
        notice = self.room_manager.recent_termination_notice("user-guest")
        self.assertEqual(notice["termination_reason"], "room_closed_by_creator")

    def test_idle_room_is_closed(self) -> None:
        token = self.seed_user("owner")
        room = self.room_manager.create_room("link_cable", "user-owner", "owner", token)
        with self.database.transaction(write=True) as connection:
            connection.execute(
                "UPDATE room_members SET last_activity_at_ms = 1 WHERE server_id = 'primary'"
            )
        policy = RoomSessionPolicy(waiting_room_idle_seconds=1)
        actions = self.room_manager.prune_idle_rooms(
            policy, datetime.now(timezone.utc) + timedelta(seconds=2)
        )
        self.assertEqual(actions[0]["room_number"], room.room_number)
        self.assertEqual(self.room_manager.room(room.room_number).users, [])

    def test_product_session_duration_defaults(self) -> None:
        policy = RoomSessionPolicy.from_environment()
        self.assertEqual(policy.waiting_room_idle_seconds, 600)
        self.assertEqual(policy.link_seconds, 3600)
        self.assertEqual(policy.n64_runtime_seconds, 7200)
        self.assertEqual(policy.gb_local_seconds, 172800)
        self.assertEqual(policy.gb_mobile_seconds, 172800)
        self.assertEqual(policy.n64_local_seconds, 172800)
        self.assertEqual(policy.finalize_seconds, 300)

    def test_product_session_durations_are_configurable(self) -> None:
        values = {
            "INTEGRAL_EMULATOR_GB_LOCAL_GAME_SECONDS": "101",
            "INTEGRAL_EMULATOR_GB_MOBILE_GAME_SECONDS": "102",
            "INTEGRAL_EMULATOR_N64_LOCAL_GAME_SECONDS": "103",
            "INTEGRAL_EMULATOR_LINK_CABLE_ROOM_GAME_SECONDS": "104",
            "INTEGRAL_EMULATOR_N64_ROOM_GAME_SECONDS": "105",
        }
        with patch.dict(os.environ, values, clear=False):
            policy = RoomSessionPolicy.from_environment()
        self.assertEqual(policy.gb_local_seconds, 101)
        self.assertEqual(policy.gb_mobile_seconds, 102)
        self.assertEqual(policy.n64_local_seconds, 103)
        self.assertEqual(policy.link_seconds, 104)
        self.assertEqual(policy.n64_runtime_seconds, 105)

    def test_long_n64_game_guest_leave_starts_fresh_waiting_deadline(self) -> None:
        owner_token = self.seed_user("long-owner")
        guest_token = self.seed_user("long-guest")
        room = self.room_manager.create_room("n64", "user-long-owner", "long-owner", owner_token)
        self.room_manager.join_room_by_code(room.room_code or "", "user-long-guest", "long-guest", guest_token)
        start_ms = self.room_manager._now_ms()
        end_ms = start_ms + 35 * 60 * 1000
        with self.database.transaction(write=True) as connection:
            connection.execute("UPDATE rooms SET game_started=1 WHERE room_number=?", (room.room_number,))
        policy = RoomSessionPolicy(waiting_room_idle_seconds=1800)
        end = datetime.fromtimestamp(end_ms / 1000, timezone.utc)
        self.assertEqual(self.room_manager.prune_idle_rooms(policy, end), [])
        with patch.object(self.room_manager, "_now_ms", return_value=end_ms):
            self.room_manager.leave_room("user-long-guest")
        self.assertEqual(self.room_manager.prune_idle_rooms(policy, end), [])
        self.assertEqual(self.room_manager.prune_idle_rooms(policy, end + timedelta(seconds=1799)), [])
        expired = self.room_manager.prune_idle_rooms(policy, end + timedelta(seconds=1801))
        self.assertEqual(expired[0]["reason"], "room_idle_timeout")

    def test_two_user_ready_and_post_game_deadlines_close_rooms(self) -> None:
        policy = RoomSessionPolicy(waiting_room_idle_seconds=1)
        now = datetime.now(timezone.utc)
        for state in ("unready", "ready", "post_game"):
            owner = f"{state}-owner"
            guest = f"{state}-guest"
            owner_token = self.seed_user(owner)
            guest_token = self.seed_user(guest)
            room = self.room_manager.create_room(
                "link_cable", f"user-{owner}", owner, owner_token
            )
            self.room_manager.join_room_by_code(
                room.room_code or "", f"user-{guest}", guest, guest_token
            )
            if state == "ready":
                self.room_manager.update_user_state(room.room_number, f"user-{owner}", ready=True)
                self.room_manager.update_user_state(room.room_number, f"user-{guest}", ready=True)
            if state == "post_game":
                self.room_manager.end_game_for_room(room.room_number)
            with self.database.transaction(write=True) as connection:
                connection.execute(
                    "UPDATE room_members SET last_activity_at_ms=1, ready_at_ms=CASE WHEN ready=1 THEN 1 ELSE ready_at_ms END WHERE server_id='primary' AND room_number=?",
                    (room.room_number,),
                )
                if state == "post_game":
                    connection.execute(
                        "UPDATE rooms SET post_game_at_ms=1 WHERE server_id='primary' AND room_number=?",
                        (room.room_number,),
                    )
            actions = self.room_manager.prune_idle_rooms(policy, now)
            self.assertEqual(actions[0]["room_number"], room.room_number)
            self.assertEqual(self.room_manager.room(room.room_number).users, [])


if __name__ == "__main__":
    unittest.main()
