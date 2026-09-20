"""Entry/module boundaries kept by the final C Client extraction."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "c_client"


class ClientEntryContractTests(unittest.TestCase):
    def test_headers_have_no_local_include_cycle(self):
        graph = {}
        for path in CLIENT.glob("*.h"):
            graph[path.name] = [
                name for name in re.findall(r'^#include "([^"]+)"', path.read_text(encoding="utf-8"), re.M)
                if (CLIENT / name).is_file() and "/" not in name
            ]
        def visit(name, stack):
            self.assertNotIn(name, stack, " -> ".join(stack + [name]))
            for child in graph.get(name, []):
                visit(child, stack + [name])
        for name in graph:
            visit(name, [])

    def test_diagnostics_and_test_entry_are_separate(self):
        entry = (CLIENT / "login_client.c").read_text(encoding="utf-8")
        diagnostics = (CLIENT / "client_diagnostics.c").read_text(encoding="utf-8")
        makefile = (CLIENT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("client_diagnostics_startup(argc, argv", entry)
        for flag in ("--smoke-test", "--outbox-smoke", "--config-smoke",
                     "--n64-stop-process-test", "--local-navigation-smoke"):
            self.assertNotIn(flag, entry)
            self.assertIn(flag, diagnostics)
        self.assertNotIn("client_room_test_app.o", makefile)
        self.assertNotIn("-Dmain=", makefile)

    def test_view_does_not_mutate_session_or_save_state(self):
        view = (CLIENT / "client_view.c").read_text(encoding="utf-8")
        self.assertNotRegex(view, r"(?<!const )AppState \*")
        self.assertNotRegex(view, r"state->[^;\n]*?(?<![=!<>])=(?!=)")
        for operation in ("integral_api_", "integral_save_upload", "memset(state", "free("):
            self.assertNotIn(operation, view)

    def test_release_builders_read_shared_version(self):
        version = (CLIENT / "client_version.h").read_text(encoding="utf-8")
        self.assertRegex(version, re.compile(r'^#define INTEGRAL_CLIENT_VERSION "[^"]+"', re.M))
        for script in ("build_c_client_release_macos.sh",
                       "build_c_client_release_windows_msys2.sh",
                       "package_c_client_linux_windows_release.sh"):
            source = (ROOT / "scripts" / script).read_text(encoding="utf-8")
            self.assertIn("/c_client/client_version.h", source)
            self.assertNotIn("/c_client/login_client.c", source)


if __name__ == "__main__":
    unittest.main()
