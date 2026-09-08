#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
rom=${1:-"$project_root/roms/m64p_test_rom.v64"}
timestamp=$(date +%Y%m%d-%H%M%S)
run_dir="$project_root/runtime/smoke/$timestamp"
config_dir="$run_dir/config"
screenshot_dir="$run_dir/screenshots"
log="$run_dir/mupen64plus.log"

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

if [ ! -f "$rom" ]; then
    echo "Smoke ROM not found: $rom" >&2
    exit 1
fi

"$project_root/tools/verify_sources.sh"
mkdir -p "$config_dir" "$screenshot_dir"

"$prefix/bin/mupen64plus" \
    --corelib "$core" \
    --configdir "$config_dir" \
    --datadir "$prefix/share/mupen64plus" \
    --plugindir "$prefix/lib/mupen64plus" \
    --gfx "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio dummy \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --windowed \
    --resolution 640x480 \
    --testshots 120 \
    --sshotdir "$screenshot_dir" \
    --nosaveoptions \
    "$rom" >"$log" 2>&1

screenshot=$(find "$screenshot_dir" -type f -name '*.png' -print -quit)
if [ -z "$screenshot" ]; then
    echo "Smoke run completed without producing a screenshot. See $log" >&2
    exit 1
fi

for expected in \
    "attached to core library 'Mupen64Plus Core' version 2.6.0" \
    "using Video plugin: 'GLideN64 rev.7c8cc44f'" \
    "using Input plugin: 'Mupen64Plus SDL Input Plugin' v2.6.0" \
    "using RSP plugin: 'Hacktarux/Azimer High-Level Emulation RSP Plugin' v2.6.0" \
    "Captured screenshot for frame 120" \
    "R4300 emulator finished"
do
    if ! grep -Fq "$expected" "$log"; then
        echo "Smoke log is missing expected evidence: $expected" >&2
        echo "See $log" >&2
        exit 1
    fi
done

echo "Smoke run passed."
echo "Log: $log"
echo "Screenshot: $screenshot"
