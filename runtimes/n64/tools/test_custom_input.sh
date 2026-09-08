#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
root="$project_root/runtime/custom-input-test/$(date +%Y%m%d-%H%M%S)"
config="$root/config"
hotkey_config="$root/hotkey-config"
screenshots="$root/screenshots"
log="$root/n64_runtime.log"
hotkey_log="$root/hotkeys.log"
# SDL_SCANCODE_INTERNATIONAL1 is the JIS physical `ろ` key on standard maps.
map='-1|1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0,1:135:0'

case $(uname -s) in
    Darwin)
        extension=dylib
        core="$prefix/lib/libmupen64plus.dylib"
        ;;
    *)
        extension=so
        core="$prefix/lib/libmupen64plus.so.2.0.0"
        ;;
esac

save_dir="$root/save"
mkdir -p "$config" "$hotkey_config" "$screenshots" "$save_dir"
"$project_root/build/integral_n64_runtime_frontend" \
    --rom "$project_root/roms/m64p_test_rom.v64" \
    --core "$core" \
    --config-dir "$config" \
    --data-dir "$prefix/share/mupen64plus" \
    --screenshot-dir "$screenshots" \
    --video "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio dummy \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --save-dir "$save_dir" --save-name input-session \
    --controller1 keyboard --controller-map1 "$map" \
    --frame 2 >"$log" 2>&1

grep -Fq 'N64 Runtime: controller 1 profile CUSTOM' "$log"
grep -Fq 'N64 Runtime: direct keyboard input active before Core execution' "$log"
grep -Fq 'Input INFO: Direct keyboard input active; IME disabled' "$log"
grep -Fq 'DPad R = "scancode(135)"' "$config/mupen64plus.cfg"
grep -Fq 'X Axis = "scancode(135,135)"' "$config/mupen64plus.cfg"
grep -Fq 'Y Axis = "scancode(135,135)"' "$config/mupen64plus.cfg"

while IFS= read -r name; do
    grep -Fq "Kbd Mapping $name = 0" "$config/mupen64plus.cfg"
    grep -Fq "Kbd Scancode $name = 0" "$config/mupen64plus.cfg"
    grep -Fq "Joy Mapping $name = \"\"" "$config/mupen64plus.cfg"
done <<'EOF'
Stop
Fullscreen
Save State
Load State
Increment Slot
Reset
Speed Down
Speed Up
Screenshot
Pause
Mute
Increase Volume
Decrease Volume
Fast Forward
Speed Limiter Toggle
Frame Advance
Gameshark
Slot 0
Slot 1
Slot 2
Slot 3
Slot 4
Slot 5
Slot 6
Slot 7
Slot 8
Slot 9
EOF

hotmap=""
i=0
while [ "$i" -lt 27 ]; do
    entry='0:0:0:-1'
    if [ "$i" -eq 8 ]; then entry='1:135:0:-1'; fi
    if [ "$i" -eq 9 ]; then entry='2:4:0:1'; fi
    if [ -n "$hotmap" ]; then hotmap="$hotmap,$entry"; else hotmap="$entry"; fi
    i=$((i + 1))
done

"$project_root/build/integral_n64_runtime_frontend" \
    --rom "$project_root/roms/m64p_test_rom.v64" \
    --core "$core" \
    --config-dir "$hotkey_config" \
    --data-dir "$prefix/share/mupen64plus" \
    --screenshot-dir "$screenshots" \
    --video "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio dummy \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --save-dir "$save_dir" --save-name hotkey-session \
    --hotkeys "$hotmap" --frame 2 >"$hotkey_log" 2>&1

grep -Fq 'Kbd Scancode Screenshot = 135' \
    "$hotkey_config/mupen64plus.cfg"
grep -Fq 'Joy Mapping Pause = "J1B4"' \
    "$hotkey_config/mupen64plus.cfg"
grep -Fq 'Kbd Scancode Stop = 0' "$hotkey_config/mupen64plus.cfg"
grep -Fq 'Joy Mapping Stop = ""' "$hotkey_config/mupen64plus.cfg"

core_events="$project_root/build/work/mupen64plus-core/src/main/eventloop.c"
grep -Fq 'SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |' "$core_events"
grep -Fq 'SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "NO"' "$core_events"
grep -Fq 'SDL_AtomicCAS(&l_stop_confirmation_state, 1, 2)' "$core_events"
grep -Fq 'SDL_ShowMessageBox(&message, &button)' "$core_events"
grep -Fq 'case SDL_WINDOWEVENT_CLOSE:' "$core_events"
grep -Fq 'keysym == SDL_SCANCODE_ESCAPE' "$core_events"
grep -Fq 'event->key.keysym.scancode == SDL_SCANCODE_ESCAPE' "$core_events"
grep -Fq 'event->key.keysym.sym == SDLK_ESCAPE' "$core_events"
grep -Fq 'N64 Runtime: STOP confirmation requested' "$core_events"
grep -Fq 'N64 Runtime: showing STOP confirmation dialog' "$core_events"
grep -Fq 'event_process_stop_confirmation();' \
    "$project_root/build/work/mupen64plus-core/src/main/main.c"
if grep -Fq 'STOP EMULATION?' "$core_events"; then
    echo "Core STOP confirmation unexpectedly depends on invisible OSD text" >&2
    exit 1
fi
if [ "$(grep -Fc 'request_stop_confirmation();' "$core_events")" -ne 7 ]; then
    echo "Core STOP confirmation is not wired to Esc, mapped STOP, controller STOP, SDL_QUIT, and window close" >&2
    exit 1
fi

echo "N64 Runtime custom physical-input test passed"
echo "Log: $log"
echo "Config: $config/mupen64plus.cfg"
echo "Custom hotkeys: $hotkey_config/mupen64plus.cfg"
