#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
frontend="$project_root/build/integral_n64_runtime_frontend"
root="$project_root/runtime/interactive-stop-test/$(date +%Y%m%d-%H%M%S)"
log="$root/n64_runtime.log"
child_pid=

cleanup() {
    if [ -n "$child_pid" ]; then
        kill "$child_pid" 2>/dev/null || true
        wait "$child_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

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

mkdir -p "$root/config" "$root/screenshots"
"$frontend" \
    --rom "$project_root/roms/m64p_test_rom.v64" \
    --core "$core" \
    --config-dir "$root/config" \
    --data-dir "$prefix/share/mupen64plus" \
    --screenshot-dir "$root/screenshots" \
    --video "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio dummy \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --save-dir "$root/save" --save-name session \
    --interactive --controller1 keyboard --controller2 auto \
    --controller3 auto --controller4 auto \
    --width 640 --height 480 >"$log" 2>&1 &
child_pid=$!

attempt=0
while ! grep -Fq "Core INFO: Starting R4300 emulator" "$log" 2>/dev/null; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 400 ]; then
        echo "Interactive Core did not start. See $log" >&2
        exit 1
    fi
    sleep 0.05
done

kill -TERM "$child_pid"
wait "$child_pid"
child_pid=

grep -Fq "N64 Runtime: execution complete at frame" "$log"
grep -Fq "N64 Runtime: controller 1 profile KEYBOARD" "$log"
grep -Fq "Input INFO: N64 Controller #1: Using auto-config for keyboard" "$log"
grep -Fq 'mode = 1' "$root/config/mupen64plus.cfg"
grep -Fq 'name = "Keyboard"' "$root/config/mupen64plus.cfg"
if grep -Fq "frame-controlled smoke run did not complete" "$log"; then
    echo "Interactive run incorrectly required the smoke frame contract. See $log" >&2
    exit 1
fi

echo "N64 Runtime interactive start/stop test passed"
echo "Log: $log"
