#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root="$project_root/runtime/gui-smoke/$(date +%Y%m%d-%H%M%S)"
screenshot="$root/n64_runtime-menu.bmp"
controllers="$root/n64_runtime-controllers.bmp"
transfer="$root/n64_runtime-transfer.bmp"
key_config="$root/n64_runtime-key-config.bmp"
hotkeys="$root/n64_runtime-hotkeys.bmp"
log="$root/gui.log"
frontend=build/integral_n64_runtime_frontend
if [ -x "${frontend}.exe" ]; then
    frontend="${frontend}.exe"
fi

logged_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

mkdir -p "$root"
cd "$project_root"
SDL_VIDEODRIVER=dummy "$frontend" --menu-smoke "$screenshot" >"$log" 2>&1
SDL_VIDEODRIVER=dummy "$frontend" --menu-smoke-controllers "$controllers" >>"$log" 2>&1
SDL_VIDEODRIVER=dummy "$frontend" --menu-smoke-transfer "$transfer" >>"$log" 2>&1
SDL_VIDEODRIVER=dummy "$frontend" --menu-smoke-key-config "$key_config" >>"$log" 2>&1
SDL_VIDEODRIVER=dummy "$frontend" --menu-smoke-hotkeys "$hotkeys" >>"$log" 2>&1

test -s "$screenshot"
test -s "$controllers"
test -s "$transfer"
test -s "$key_config"
test -s "$hotkeys"
grep -Fq "N64 Runtime GUI: screenshot saved to $(logged_path "$screenshot")" "$log"
grep -Fq "N64 Runtime GUI: screenshot saved to $(logged_path "$controllers")" "$log"
grep -Fq "N64 Runtime GUI: screenshot saved to $(logged_path "$transfer")" "$log"
grep -Fq "N64 Runtime GUI: screenshot saved to $(logged_path "$key_config")" "$log"
grep -Fq "N64 Runtime GUI: screenshot saved to $(logged_path "$hotkeys")" "$log"

echo "N64 Runtime GUI smoke test passed"
echo "Screenshot: $screenshot"
echo "Controllers: $controllers"
echo "Transfer Pak: $transfer"
echo "Key config: $key_config"
echo "Emulator keys: $hotkeys"
echo "Log: $log"
