from __future__ import annotations

import tempfile
import unittest
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

from integral_emulator.api import LeagueApplication
from integral_emulator.gb_runtime_link_modes import GB_RUNTIME_LINK_RUNTIME
from integral_emulator.n64_runtime_media_sessions import MEDIA_TICKET_SCOPE
from integral_emulator.n64_runtime_media_relay import CONTROL_MAGIC, HANDSHAKE_MAGIC as MEDIA_HANDSHAKE_MAGIC
from integral_emulator.sessions import LinkSessionStatus
from integral_emulator.ui import ADMIN_HTML, DASHBOARD_HTML


class NamingContractTests(unittest.TestCase):
    def test_public_product_and_server_names(self) -> None:
        self.assertIn("INTEGRAL EMULATOR", ADMIN_HTML)
        self.assertIn("INTEGRAL EMULATOR", DASHBOARD_HTML)
        with tempfile.TemporaryDirectory() as directory:
            app = LeagueApplication(Path(directory))
            self.assertEqual(app.environments["primary"].name, "PRIMARY")
            self.assertEqual(app.environments["secondary"].name, "SECONDARY")

    def test_server_environment_contract_is_static(self) -> None:
        root = Path(__file__).resolve().parents[1]
        client = (root / "c_client" / "login_client.c").read_text(encoding="utf-8")
        http_header = (root / "c_client" / "http_client.h").read_text(encoding="utf-8")
        http_source = (root / "c_client" / "http_client.c").read_text(encoding="utf-8")
        self.assertIn('#define INTEGRAL_PRIMARY_SERVER_ID "primary"', client)
        self.assertIn('#define INTEGRAL_SECONDARY_SERVER_ID "secondary"', client)
        self.assertIn('draw_field(renderer, 88, "SERVER", mutable_state.server', client)
        self.assertIn('draw_field(renderer, 142, "ENV", login_server_label(state)', client)
        self.assertIn("save_login_form_config(&state)", client)
        self.assertNotIn("integral_api_get_servers", http_header)
        self.assertNotIn("integral_api_get_servers", http_source)
        self.assertNotIn('"/servers"', http_source)

        with tempfile.TemporaryDirectory() as directory:
            app = LeagueApplication(Path(directory))
            self.assertEqual(tuple(app.environments), ("primary", "secondary"))

    def test_wire_and_persisted_identifiers_remain_stable(self) -> None:
        self.assertEqual(MEDIA_HANDSHAKE_MAGIC, "N64RUNTIME1")
        self.assertEqual(CONTROL_MAGIC, b"N64R")
        self.assertEqual(MEDIA_TICKET_SCOPE, "n64_runtime_media")
        self.assertEqual(GB_RUNTIME_LINK_RUNTIME, "gb_runtime")

    def test_distribution_and_canonical_import_package_are_rebranded(self) -> None:
        root = Path(__file__).resolve().parents[1]
        pyproject = (root / "pyproject.toml").read_text(encoding="utf-8")
        self.assertIn('name = "integral-emulator"', pyproject)
        self.assertTrue((root / "src" / "integral_emulator" / "__init__.py").is_file())

    def test_rom_metadata_has_no_unused_platform_logo_contract(self) -> None:
        root = Path(__file__).resolve().parents[1]
        source = (root / "c_client" / "rom_metadata.c").read_text(encoding="utf-8")
        header = (root / "c_client" / "rom_metadata.h").read_text(encoding="utf-8")
        self.assertNotIn("NINTENDO_LOGO", source)
        self.assertNotIn("logo_ok", source)
        self.assertNotIn("logo_ok", header)

    def test_canonical_module_entry_point(self) -> None:
        root = Path(__file__).resolve().parents[1]
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(root / "src")
        completed = subprocess.run(
            [sys.executable, "-m", "integral_emulator", "--help"],
            cwd=root,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("INTEGRAL EMULATOR API server", completed.stdout)

    def test_product_icons_are_independent_from_gb_runtime_assets(self) -> None:
        root = Path(__file__).resolve().parents[1]
        if not (root / "assets" / "product" / "integral_emulator_icon.png").is_file():
            self.skipTest("unapproved product assets are excluded from the public candidate")
        self.assertTrue((root / "assets" / "product" / "integral_emulator_icon.png").is_file())
        self.assertTrue((root / "assets" / "product" / "integral_emulator_icon.ico").is_file())

    def test_integral_client_is_primary_build_output(self) -> None:
        root = Path(__file__).resolve().parents[1]
        makefile = (root / "c_client" / "Makefile").read_text(encoding="utf-8")
        client = (root / "c_client" / "login_client.c").read_text(encoding="utf-8")
        self.assertIn("TARGET := $(BUILD_DIR)/integral_client$(EXEEXT)", makefile)
        self.assertNotIn("gsc_login_client", makefile)
        self.assertIn('"integral_client.log"', client)
        self.assertNotIn('"' + "gsc" + '_client.log"', client)
        self.assertNotIn("UPLOADING" + " ROM", client)

    def test_retired_link_rom_upload_lifecycle_state_is_absent(self) -> None:
        root = Path(__file__).resolve().parents[1]
        retired_state = "UPLOADING" + "_ROMS"
        self.assertNotIn(retired_state, {status.value for status in LinkSessionStatus})
        for relative_path in (
            "src/integral_emulator/sessions.py",
            "src/integral_emulator/api.py",
            "src/integral_emulator/database.py",
            "c_client/login_client.c",
        ):
            source = (root / relative_path).read_text(encoding="utf-8")
            self.assertNotIn(retired_state, source, relative_path)

    def test_c_client_documentation_matches_supported_build_paths(self) -> None:
        root = Path(__file__).resolve().parents[1]
        document = (root / "docs" / "C_CLIENT.md").read_text(encoding="utf-8")
        config_example = (
            root / "c_client" / "integral_client.conf.example"
        ).read_text(encoding="utf-8")

        self.assertIn("Ubuntu 24.04 LTSでのビルド", document)
        for package in (
            "build-essential",
            "cmake",
            "pkg-config",
            "libsdl2-dev",
            "libsdl2-ttf-dev",
            "libssl-dev",
            "libgl1-mesa-dev",
            "libpng-dev",
            "libsamplerate0-dev",
        ):
            self.assertIn(package, document)
        self.assertIn("make -C c_client", document)
        self.assertIn("cd c_client", document)
        self.assertIn("config/integral_client.conf", document)
        self.assertIn("./build/integral_client", document)
        self.assertNotIn("./c_client/build/integral_client --config", document)
        self.assertIn("scripts/build_c_client_release_macos.sh", document)
        self.assertIn("scripts/build_c_client_release_windows_msys2.sh", document)
        self.assertTrue((root / "scripts" / "build_c_client_release_macos.sh").is_file())
        self.assertTrue(
            (root / "scripts" / "build_c_client_release_windows_msys2.sh").is_file()
        )
        self.assertNotIn("make -C c_client release-macos", document)
        self.assertNotIn("Mac workspace", document)
        self.assertIn("初回公開の公式サポート対象ではありません", document)

        for key in ("login.server", "login.server_id", "login.remember"):
            self.assertIn(key, config_example)
            self.assertIn(key, document)
        self.assertNotIn("INTEGRAL_EMULATOR_RELEASE_ROOT", document)
        self.assertNotIn("INTEGRAL_EMULATOR_RELEASE_SKIP_CODESIGN", document)

    def test_login_text_renderer_preserves_ascii_letter_case(self) -> None:
        root = Path(__file__).resolve().parents[1]
        renderer = (root / "c_client" / "sdl_text.c").read_text(encoding="utf-8")
        self.assertIn("['A'] =", renderer)
        self.assertIn("['a'] =", renderer)
        self.assertNotIn("toupper((unsigned char)*p)", renderer)

    def test_room_protocol_is_checked_directly_and_fails_closed(self) -> None:
        root = Path(__file__).resolve().parents[1]
        client = (root / "c_client" / "login_client.c").read_text(encoding="utf-8")
        http_client = (root / "c_client" / "http_client.c").read_text(encoding="utf-8")
        makefile = (root / "c_client" / "Makefile").read_text(encoding="utf-8")
        self.assertIn('strcmp(protocol_id, "gb_runtime_fixed_host_v1") != 0', client)
        self.assertIn('"CLIENT CAPABILITY MISMATCH"', client)
        self.assertNotIn("room_protocol_route", client)
        self.assertNotIn("room_protocol_route", makefile)
        self.assertFalse((root / "c_client" / "room_protocol_route.c").exists())
        self.assertFalse((root / "c_client" / "room_protocol_route.h").exists())
        self.assertNotIn("integral_api_commit_client_save", http_client)
        self.assertNotIn("room_save_policy", makefile)
        self.assertFalse((root / "c_client" / "room_save_policy.c").exists())
        self.assertFalse((root / "c_client" / "room_save_policy.h").exists())
        self.assertFalse((root / "c_client" / "room_save_policy_test.c").exists())

    def test_release_executable_contract_uses_neutral_runtime_paths(self) -> None:
        root = Path(__file__).resolve().parents[1]
        module_path = root / "scripts" / "write_release_manifest.py"
        spec = importlib.util.spec_from_file_location("write_release_manifest", module_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        macos = module.executable_contract("macos")
        windows = module.executable_contract("windows")
        self.assertEqual(
            macos["client"]["canonical"],
            "INTEGRAL EMULATOR.app/Contents/Resources/client/integral_client",
        )
        self.assertEqual(
            macos["n64_frontend"]["canonical"],
            "INTEGRAL EMULATOR.app/Contents/Resources/runtimes/n64/build/integral_n64_runtime_frontend",
        )
        self.assertEqual(windows["client"]["canonical"], "client/integral_client.exe")
        self.assertEqual(windows["client"]["compatibility"], [])
        self.assertNotIn("gb_stream_client", macos)
        self.assertNotIn("gb_link_node", macos)
        self.assertNotIn("gb_stream_client", windows)
        self.assertNotIn("gb_link_node", windows)
        self.assertEqual(
            macos["gb_fixed_host"]["canonical"],
            "INTEGRAL EMULATOR.app/Contents/Resources/runtimes/gb/integral_gb_runtime_fixed_host",
        )
        self.assertEqual(
            macos["gb_mobile_runtime"]["canonical"],
            "INTEGRAL EMULATOR.app/Contents/Resources/runtimes/gb/integral_gb_runtime_mobile_runtime",
        )
        for role in (
            "gb_frontend",
            "gb_dual_server",
            "gb_fixed_host",
            "gb_mobile_runtime",
            "n64_frontend",
        ):
            self.assertEqual(windows[role]["compatibility"], [])
        self.assertEqual(
            windows["gb_frontend"]["canonical"],
            "runtimes/gb/integral_gb_runtime_frontend.exe",
        )

        with tempfile.TemporaryDirectory() as directory:
            handoff_root = Path(directory)
            (handoff_root / "SOURCE_HANDOFF_MANIFEST.json").write_text(
                json.dumps({"source_commit": "abc123", "dirty": True}),
                encoding="utf-8",
            )
            self.assertEqual(module.source_state(handoff_root), ("abc123", True))

    def test_release_launchers_use_neutral_package_layout(self) -> None:
        root = Path(__file__).resolve().parents[1]
        macos = (root / "scripts" / "build_c_client_release_macos.sh").read_text(
            encoding="utf-8"
        )
        windows_release = (
            root / "scripts" / "build_c_client_release_windows_msys2.sh"
        ).read_text(encoding="utf-8")
        windows = (root / "c_client" / "windows_launcher.c").read_text(encoding="utf-8")
        self.assertIn('CLIENT_DIR="${RESOURCES_DIR}/client"', macos)
        self.assertIn('GB_RUNTIME_DIR="${RESOURCES_DIR}/runtimes/gb"', macos)
        self.assertIn('N64_RUNTIME_DIR="${RESOURCES_DIR}/runtimes/n64"', macos)
        self.assertIn('"${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server"', macos)
        self.assertIn('"${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host"', macos)
        self.assertIn('"${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime"', macos)
        self.assertNotIn("integral_gb_runtime_stream_client", macos)
        self.assertNotIn("integral_gb_runtime_link_node", macos)
        self.assertIn('"${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host.exe"', windows_release)
        self.assertIn('"${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime.exe"', windows_release)
        self.assertNotIn("integral_gb_runtime_stream_client", windows_release)
        self.assertNotIn("integral_gb_runtime_link_node", windows_release)
        self.assertIn('loader_ref="@loader_path/../../Frameworks"', macos)
        self.assertIn('loader_ref="@loader_path/../../../Frameworks"', macos)
        self.assertIn(
            'cp -f "${PROJECT_ROOT}/scripts/unlock_macos.sh" '
            '"${PACKAGE_DIR}/unlock_macos.sh"',
            macos,
        )
        self.assertIn('chmod +x "${PACKAGE_DIR}/unlock_macos.sh"', macos)
        self.assertIn('chmod -R u+w "${PACKAGE_DIR}"', macos)
        self.assertIn('L"client\\\\integral_client.exe"', windows)
        self.assertNotIn('L"client\\\\gsc_login_client.exe"', windows)
        self.assertIn('L"runtimes\\\\gb\\\\integral_gb_runtime_fixed_host.exe"', windows)
        self.assertNotIn('L"runtimes\\\\gb\\\\integral_gb_runtime_link_node.exe"', windows)

    def test_macos_release_unlock_removes_only_quarantine_recursively(self) -> None:
        root = Path(__file__).resolve().parents[1]
        source = root / "scripts" / "unlock_macos.sh"
        script = source.read_text(encoding="utf-8")
        readme = (root / "README.md").read_text(encoding="utf-8")

        self.assertIn('xattr -dr com.apple.quarantine "$RELEASE_ROOT"', script)
        self.assertNotIn("xattr -c", script)
        self.assertNotIn("spctl", script)
        self.assertNotIn("sudo", script)
        self.assertIn("sh unlock_macos.sh", readme)
        self.assertIn("Developer ID", readme)
        self.assertIn("Apple Notarization", readme)
        self.assertIn("Gatekeeper全体を無効化するものではなく", readme)

        if sys.platform != "darwin":
            return
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            release = base / "Release Folder With Spaces"
            nested = release / "nested folder"
            elsewhere = base / "other cwd"
            nested.mkdir(parents=True)
            elsewhere.mkdir()
            copied_script = release / "unlock_macos.sh"
            copied_script.write_text(script, encoding="utf-8")
            copied_script.chmod(0o755)
            payload = nested / "helper binary"
            payload.write_bytes(b"fixture")
            subprocess.run(
                ["xattr", "-w", "com.apple.quarantine", "0081;fixture", str(payload)],
                check=True,
            )
            subprocess.run(
                ["xattr", "-w", "com.integral.test", "preserved", str(payload)],
                check=True,
            )

            completed = subprocess.run(
                ["sh", str(copied_script)],
                cwd=elsewhere,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn(str(release), completed.stdout)
            quarantine = subprocess.run(
                ["xattr", "-p", "com.apple.quarantine", str(payload)],
                check=False,
                capture_output=True,
            )
            self.assertNotEqual(quarantine.returncode, 0)
            preserved = subprocess.run(
                ["xattr", "-p", "com.integral.test", str(payload)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(preserved.stdout.strip(), "preserved")

    def test_source_handoff_uses_integral_default_archive_name(self) -> None:
        root = Path(__file__).resolve().parents[1]
        module_path = root / "scripts" / "build_source_handoff.py"
        if not module_path.is_file():
            self.skipTest("internal handoff builder is not in the public candidate")
        source = module_path.read_text(encoding="utf-8")
        self.assertIn(
            'DEFAULT_ARCHIVE_NAME = "INTEGRAL_EMULATOR_SOURCE_HANDOFF.zip"',
            source,
        )
        spec = importlib.util.spec_from_file_location("build_source_handoff", module_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertIn("runtimes", module.ALLOWED_TOP_LEVEL)
        self.assertNotIn("desk", module.ALLOWED_TOP_LEVEL)
        self.assertNotIn("GB Runtime", module.ALLOWED_TOP_LEVEL)
        self.assertNotIn("N64 Runtime", module.ALLOWED_TOP_LEVEL)
        executable = module.archive_info(module.PurePosixPath("scripts/build.sh"), executable=True)
        regular = module.archive_info(module.PurePosixPath("README.md"))
        self.assertEqual((executable.external_attr >> 16) & 0o777, 0o755)
        self.assertEqual((regular.external_attr >> 16) & 0o777, 0o644)
        self.assertEqual(executable.create_system, 3)

    def test_n64_has_no_separate_public_source_packager(self) -> None:
        root = Path(__file__).resolve().parents[1]
        makefile = (root / "runtimes" / "n64" / "Makefile").read_text(
            encoding="utf-8"
        )
        macos_readme_path = root / "runtimes" / "n64" / "README_BUILD_MACOS.md"

        self.assertNotIn("source-macos", makefile)
        if macos_readme_path.is_file():
            self.assertNotIn(
                "source-macos", macos_readme_path.read_text(encoding="utf-8")
            )
        self.assertFalse(
            (
                root
                / "runtimes"
                / "n64"
                / "tools"
                / "build_source_macos_arm64_package.sh"
            ).exists()
        )
        self.assertTrue((root / "scripts" / "build_public_source.py").is_file())

    def test_runtime_source_directories_are_canonical(self) -> None:
        root = Path(__file__).resolve().parents[1]
        self.assertTrue((root / "runtimes" / "gb" / "src" / "Makefile").is_file())
        self.assertTrue((root / "runtimes" / "n64" / "Makefile").is_file())
        gb_makefile = (root / "runtimes" / "gb" / "src" / "Makefile").read_text(
            encoding="utf-8"
        )
        n64_makefile = (root / "runtimes" / "n64" / "Makefile").read_text(
            encoding="utf-8"
        )
        self.assertIn("APP_BIN := $(BUILD_DIR)/integral_gb_runtime_frontend$(EXEEXT)", gb_makefile)
        self.assertIn("SERVER_BIN := $(BUILD_DIR)/integral_gb_runtime_dual_server$(EXEEXT)", gb_makefile)
        self.assertNotIn("integral_gb_runtime_stream_client", gb_makefile)
        self.assertNotIn("integral_gb_runtime_link_node", gb_makefile)
        self.assertNotIn("APP_BIN := $(BUILD_DIR)/gb_runtime_", gb_makefile)
        self.assertNotIn("SERVER_BIN := $(BUILD_DIR)/gb_runtime_", gb_makefile)
        self.assertNotIn("$(BUILD_DIR)/remote_dual_", gb_makefile)
        self.assertIn("frontend: source-stack build/integral_n64_runtime_frontend$(EXEEXT)", n64_makefile)
        self.assertNotIn("build/N64 Runtime", n64_makefile)
        client_makefile = (root / "c_client" / "Makefile").read_text(encoding="utf-8")
        self.assertIn("../runtimes/n64/src/remote_media_ipc.c", client_makefile)

if __name__ == "__main__":
    unittest.main()
