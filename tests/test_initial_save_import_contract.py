import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class InitialSaveImportContractTests(unittest.TestCase):
    def test_removed_general_save_create_and_rom_smoke_do_not_return(self) -> None:
        http_header = (ROOT / "c_client/http_client.h").read_text(encoding="utf-8")
        http_source = "\n".join(path.read_text(encoding="utf-8") for path in [ROOT / "c_client/http_client.c", *sorted((ROOT / "c_client").glob("api_*.c"))])
        client_source = (ROOT / "c_client/client_diagnostics.c").read_text(encoding="utf-8")

        self.assertNotIn("integral_api_create_save", http_header)
        self.assertNotIn("integral_api_create_save", http_source)
        self.assertNotIn("--rom-smoke", client_source)

    def test_clients_gate_initial_save_ui_on_server_capability(self) -> None:
        client_source = (ROOT / "c_client/client_view.c").read_text(encoding="utf-8")

        self.assertIn("state->catalog.allow_user_initial_save_import", client_source)
        self.assertIn("SEND SELECTED INITIAL SAV?", client_source)
        registration_source = (ROOT / "c_client/client_rom_registration.c").read_text(encoding="utf-8")
        self.assertIn("INITIAL SAV REJECTED USE ADMIN SAV REPLACE", registration_source)
        self.assertIn("state->allow_user_initial_save_import &&", registration_source)
        self.assertIn("integral_gb_runtime_initial_battery_for_rom", registration_source)
        self.assertIn("initial_save_generated ? 1 : 0", registration_source)
        api_source = (ROOT / "src/integral_emulator/api.py").read_text(encoding="utf-8")
        self.assertIn("generated_initial_save_data", api_source)
        editor_source = (ROOT / "c_client/client_rom_editor.c").read_text(encoding="utf-8")
        self.assertIn("if (!state->allow_user_initial_save_import)", editor_source)
        self.assertIn("return ROM_ACTION_REGISTER_IMPORT;", editor_source)
        self.assertIn("return ROM_ACTION_REGISTER_DELETE;", editor_source)
        self.assertNotIn("integral_api_", editor_source)

        ios_session_path = ROOT / "ios/IntegralEmulatorTest/IntegralEmulatorTest/ContentView.swift"
        if not ios_session_path.exists():
            self.skipTest("iOS sources are not part of the public source archive")
        ios_session = ios_session_path.read_text(encoding="utf-8")
        ios_view = (
            ROOT / "ios/IntegralEmulatorTest/IntegralEmulatorTest/Views/ROMRegisterView.swift"
        ).read_text(encoding="utf-8")
        self.assertIn("allowUserInitialSaveImport", ios_session)
        self.assertIn("session.allowUserInitialSaveImport", ios_view)
        self.assertIn("ローカルSAVを送信しますか？", ios_view)
        self.assertIn("管理画面のSAV置換を使用してください。", ios_view)


if __name__ == "__main__":
    unittest.main()
