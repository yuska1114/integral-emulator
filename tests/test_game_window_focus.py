from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameWindowFocusTests(unittest.TestCase):
    def test_shared_sdl_request_is_ordered_nonfatal_and_skips_hidden(self):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "SDL.h").write_text("""typedef struct SDL_Window { unsigned flags; } SDL_Window;
#define SDL_WINDOW_HIDDEN 8u
unsigned SDL_GetWindowFlags(SDL_Window *w);
void SDL_RaiseWindow(SDL_Window *w);
int SDL_SetWindowInputFocus(SDL_Window *w);
""", encoding="utf-8")
            source = root / "test.c"
            source.write_text('''#include <assert.h>
#include <stddef.h>
#include "window_focus.h"
static int raised, focused;
unsigned SDL_GetWindowFlags(SDL_Window *w) { return w->flags; }
void SDL_RaiseWindow(SDL_Window *w) { (void)w; assert(raised == focused); raised++; }
int SDL_SetWindowInputFocus(SDL_Window *w) { (void)w; assert(raised == focused + 1); focused++; return -1; }
int main(void) {
    SDL_Window visible = {0}, hidden = {SDL_WINDOW_HIDDEN};
    integral_focus_new_game_window(NULL);
    integral_focus_new_game_window(&hidden);
    assert(raised == 0 && focused == 0);
    integral_focus_new_game_window(&visible);
    assert(raised == 1 && focused == 1);
    return 0;
}
''', encoding="utf-8")
            executable = root / ("test.exe" if os.name == "nt" else "test")
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(root), "-I" + str(ROOT / "runtimes/common"),
                            str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def assert_creation_only(self, path, window):
        source = (ROOT / path).read_text(encoding="utf-8")
        call = f"integral_focus_new_game_window({window});"
        self.assertEqual(source.count(call), 1)
        self.assertNotIn("SDL_RaiseWindow(", source)
        self.assertNotIn("SDL_SetWindowInputFocus(", source)
        creation = source.index("SDL_CreateWindow(")
        activation = source.index(call)
        self.assertGreater(activation, creation)
        self.assertLess(activation - creation, 1300)

    def test_local_gb_and_server2(self):
        self.assert_creation_only("runtimes/gb/src/server/video_window.c", "window->window")
        source = (ROOT / "runtimes/gb/src/server/main.c").read_text(encoding="utf-8")
        self.assertIn("integral_gb_runtime_video_window_open_titled_sized(", source)

    def test_mobile(self):
        source = (ROOT / "runtimes/gb/src/mobile/mobile_runtime.c").read_text(encoding="utf-8")
        self.assertIn("integral_gb_runtime_video_window_open", source)
        self.assert_creation_only("runtimes/gb/src/server/video_window.c", "window->window")

    def test_link_host(self):
        self.assert_creation_only("c_client/integral_gb_runtime_fixed_host.c", "video->window")

    def test_link_remote_and_n64_remote(self):
        self.assert_creation_only("c_client/n64_runtime_media_stream.c", "stream->video_window")
        source = (ROOT / "c_client/n64_runtime_media_stream.c").read_text(encoding="utf-8")
        self.assertEqual(source.count("stream->startup_focus_requested = true;"), 1)
        self.assertNotIn("startup_focus_requested = false", source)

    def test_n64_local_and_room_host(self):
        with tempfile.TemporaryDirectory() as directory:
            api_dir = Path(directory) / "src/api"
            api_dir.mkdir(parents=True)
            for name in ("vidext.c", "vidext_sdl2_compat.h"):
                shutil.copyfile(ROOT / "runtimes/n64/third_party/mupen64plus-core/src/api" / name, api_dir / name)
            patch = ROOT / "runtimes/n64/patches/mupen64plus-core-startup-focus.patch"
            with patch.open("rb") as stream:
                subprocess.run(["patch", "-p1", "-d", directory], stdin=stream,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            source = (api_dir / "vidext_sdl2_compat.h").read_text(encoding="utf-8")
            api = (api_dir / "vidext.c").read_text(encoding="utf-8")
        self.assertEqual(source.count("integral_focus_new_game_window(SDL_VideoWindow);"), 1)
        self.assertLess(source.index("SDL_CreateWindow("), source.index("integral_focus_new_game_window("))
        self.assertIn("if (!integral_startup_focus_requested)", source)
        destroy = source.split("static void SDL2_DestroyWindow(void)", 1)[1].split("static SDL_Surface", 1)[0]
        self.assertNotIn("integral_startup_focus_requested", destroy)
        self.assertIn("integral_startup_focus_requested = 0;", api)
        build = (ROOT / "runtimes/n64/tools/build_source_stack.sh").read_text(encoding="utf-8")
        self.assertIn('cp "$project_root/../common/window_focus.h" "$core_build/src/api/integral_window_focus.h"', build)

    def test_wait_and_n64_menu(self):
        self.assert_creation_only("runtimes/gb/src/server/wait_window.c", "window->window")
        self.assert_creation_only("runtimes/n64/src/gui/menu.c", "window")
