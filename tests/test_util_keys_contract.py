# SPDX-License-Identifier: GPL-3.0-or-later
"""Launch/dispatch matrix wiring; C tests separately exercise input and files."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


class UtilKeysContract(unittest.TestCase):
    def test_local_one_and_two_slots(self):
        text = source("runtimes/gb/src/server/main.c")
        self.assertIn("if (!options.self_mode || slot2_ready || options.lan_remote_enabled)", text)
        self.assertIn("integral_gb_runtime_input_router_disable_speed_controls(&input_router)", text)
        self.assertIn("if (integral_gb_runtime_input_router_take_screenshot_request(&input_router))", text)
        reset = text.split("if (integral_gb_runtime_input_router_take_reset_request(&input_router))", 1)[1]
        reset = reset.split("if (stream_server_open)", 1)[0]
        self.assertIn("integral_gb_runtime_slot_reset(slot1)", reset)
        self.assertIn("integral_gb_runtime_slot_reset(slot2)", reset)
        self.assertNotIn("self_mode", reset)
        self.assertIn("slot2_ready ? slot2 : NULL", text)

    def test_mobile_scope_preserved(self):
        text = source("runtimes/gb/src/mobile/mobile_runtime.c")
        self.assertIn("integral_gb_runtime_input_router_disable_speed_controls", text)
        self.assertIn("take_screenshot_request", text)
        self.assertIn("take_reset_request", text)
        self.assertIn("RETURN_MENU", text)
        self.assertIn('"gb_mobile", "local"', text)

    def test_link_passes_only_two_utils(self):
        text = source("c_client/client_room_link.c")
        for key in ("SCREENSHOT", "ESCAPE"):
            self.assertIn("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_" + key + "_KEY", text)
        for key in ("RESET", "FAST", "TURBO"):
            self.assertNotIn("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_" + key + "_KEY", text)
        runtime = source("c_client/integral_gb_runtime_fixed_host.c")
        self.assertIn("integral_gb_runtime_key_config_binding_rising", runtime)
        self.assertIn("options->escape_key, &event, &product->escape_held", runtime)
        self.assertNotIn("event.key.keysym.sym == options->escape_key", runtime)
        self.assertIn('surface, "link_cable", "host"', runtime)
        self.assertIn('save_screenshot(media, "link_cable")', runtime)

    def test_n64_local_and_room(self):
        local = source("c_client/client_local.c")
        room = source("c_client/client_room_n64.c")
        self.assertIn("make_n64_runtime_util_hotkeys(&state->keys, true,", local)
        self.assertIn("make_n64_runtime_util_hotkeys(state->keys, false,", room)
        self.assertEqual(room.count('"--hotkeys", hotkeys'), 2)  # Windows/POSIX
        self.assertIn('state->n64.n64_runtime_media_stream, "n64_room"', room)
        self.assertIn('= "screenshot";', room)
        self.assertIn('= "screenshot";', source("c_client/client_launch_n64_local.c"))
        self.assertIn("request_runtime_exit_confirmation(state, false)", room)

    def test_visible_matrix(self):
        text = source("c_client/client_view.c")
        for label in ("FAST / TURBO: LOCAL GB 1P ONLY", "RESET: LOCAL MODES",
                      "SCREENSHOT / ESCAPE: ALL MODES"):
            self.assertIn(label, text)


if __name__ == "__main__":
    unittest.main()
