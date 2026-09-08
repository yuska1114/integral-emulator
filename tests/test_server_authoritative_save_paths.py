import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ServerAuthoritativeSavePathTests(unittest.TestCase):
    def test_c_client_uses_session_paths_not_rom_adjacent_paths(self) -> None:
        config = (ROOT / "c_client/client_config.h").read_text(encoding="utf-8")
        client = (ROOT / "c_client/login_client.c").read_text(encoding="utf-8")

        self.assertNotIn("char save_path[", config)
        self.assertNotIn("make_default_save_path", client)
        self.assertIn('"runtime/gb-sessions", game_session_id, NULL', client)
        self.assertIn('"runtime/gb-sessions", game_session_id, "slot1.sav"', client)
        self.assertIn("remove_reflected_session_saves", client)
        self.assertIn("write_save_upload_outbox", client)
        self.assertIn("runtime_session_id_is_path_safe", client)
        self.assertNotIn("make_safe_n64_runtime_save_name(n64_slot->save_id, game_session_id", client)

    def test_outbox_success_deletes_sav_and_n64_room_cleanup_is_exact(self) -> None:
        client = (ROOT / "c_client/login_client.c").read_text(encoding="utf-8")

        self.assertNotIn('"%s.sent", save_path', client)
        self.assertIn('"%s.complete",', client)
        self.assertIn("complete_replayed_outbox_entry", client)
        self.assertIn('"transfer/slot1.gbc"', client)
        self.assertIn('"transfer/slot2.gbc"', client)
        self.assertIn('"controller2.bin"', client)
        self.assertIn('"remote-media.ipc"', client)
        self.assertIn("cleanup_n64_room_session_files", client)
        self.assertNotIn("remove_directory_recursive", client)

    def test_ios_library_has_no_persistent_rom_tied_save(self) -> None:
        library_path = ROOT / "ios/IntegralEmulatorTest/IntegralEmulatorTest/Services/ROMLibraryStore.swift"
        if not library_path.exists():
            self.skipTest("iOS sources are not part of the public source archive")
        library = library_path.read_text(encoding="utf-8")
        session_store = (
            ROOT
            / "ios/IntegralEmulatorTest/IntegralEmulatorTest/Services/BatterySaveStore.swift"
        ).read_text(encoding="utf-8")
        service = (
            ROOT
            / "ios/IntegralEmulatorTest/IntegralEmulatorTest/Services/ROMRegistrationService.swift"
        ).read_text(encoding="utf-8")

        self.assertNotIn('appendingPathComponent("Saves"', library)
        self.assertNotIn('appendingPathComponent("SaveBackups"', library)
        self.assertIn('appendingPathComponent("RuntimeSessions"', session_store)
        self.assertIn('appendingPathComponent("SaveRecovery"', session_store)
        self.assertIn("SessionSaveStore.discard", service)
        self.assertIn("downloadServerSave", service)

    def test_runtime_entrypoints_reject_implicit_save_layouts(self) -> None:
        gb_paths = (ROOT / "runtimes/gb/src/app/menu_paths.c").read_text(
            encoding="utf-8"
        )
        gb_menu = (ROOT / "runtimes/gb/src/app/main.c").read_text(
            encoding="utf-8"
        )
        n64 = (ROOT / "runtimes/n64/src/main.c").read_text(encoding="utf-8")

        self.assertNotIn("save_path_from_rom", gb_paths)
        self.assertNotIn("default_save_for_rom", gb_paths)
        self.assertIn("START FROM INTEGRAL CLIENT", gb_menu)
        self.assertNotIn("prepare_rom_save_layout", n64)
        self.assertNotIn("backup_existing_n64_save", n64)
        self.assertIn("options->save_dir != NULL", n64)
        self.assertIn("options->save_name != NULL", n64)


if __name__ == "__main__":
    unittest.main()
