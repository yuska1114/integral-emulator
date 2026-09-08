from __future__ import annotations

import inspect
import tempfile
import unittest
from pathlib import Path

import integral_emulator
from integral_emulator.sessions import LinkSessionManager
from integral_emulator.sqlite_assets import SQLiteRomRegistry
from integral_emulator.sqlite_auth import SQLiteAuthService
from integral_emulator.sqlite_room import SQLiteRoomManager
from integral_emulator.storage import LeagueStorage


class SQLiteOnlyServiceTests(unittest.TestCase):
    def test_json_service_classes_are_not_exported_or_inherited(self) -> None:
        self.assertFalse(hasattr(integral_emulator, "AuthService"))
        self.assertFalse(hasattr(integral_emulator, "RomRegistry"))
        self.assertEqual(SQLiteAuthService.__bases__, (object,))
        self.assertEqual(SQLiteRomRegistry.__bases__, (object,))
        self.assertEqual(SQLiteRoomManager.__bases__, (object,))

    def test_file_storage_keeps_atomic_bytes_without_collection_crud(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            storage = LeagueStorage(directory)
            target = Path(directory) / "saves" / "sample.sav"
            storage.atomic_write_bytes(target, b"save-data")
            self.assertEqual(target.read_bytes(), b"save-data")
            self.assertFalse(hasattr(storage, "load_collection"))
            self.assertFalse(hasattr(storage, "save_collection"))

    def test_link_session_manager_requires_sqlite_game_authority(self) -> None:
        parameter = inspect.signature(LinkSessionManager).parameters[
            "game_session_authority"
        ]
        self.assertEqual(parameter.default, inspect.Parameter.empty)
        self.assertEqual(parameter.kind, inspect.Parameter.KEYWORD_ONLY)


if __name__ == "__main__":
    unittest.main()
