#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
frontend="$project_root/build/integral_n64_runtime_frontend"
rom=${1:-"$project_root/roms/m64p_test_rom.v64"}
timestamp=$(date +%Y%m%d-%H%M%S)
run_dir="$project_root/runtime/n64_runtime-smoke/$timestamp"
config_dir="$run_dir/config"
screenshot_dir="$run_dir/screenshots"
log="$run_dir/n64_runtime.log"
audio_mode=${N64_RUNTIME_SMOKE_AUDIO:-dummy}
capture_frame=${N64_RUNTIME_SMOKE_FRAME:-120}
remote_input_file=${N64_RUNTIME_SMOKE_REMOTE_INPUT_FILE:-}
remote_media_file=${N64_RUNTIME_SMOKE_REMOTE_MEDIA_FILE:-}

case "$capture_frame" in
    ''|*[!0-9]*|0)
        echo "N64_RUNTIME_SMOKE_FRAME must be a positive integer" >&2
        exit 1
        ;;
esac

case $(uname -s) in
    Darwin)
        extension=dylib
        core="$prefix/lib/libmupen64plus.dylib"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        extension=dll
        frontend="$frontend.exe"
        core="$prefix/lib/mupen64plus.dll"
        core_log=$(cygpath -m "$core")
        ;;
    *)
        extension=so
        core="$prefix/lib/libmupen64plus.so.2.0.0"
        ;;
esac

core_log=${core_log:-$core}

case "$audio_mode" in
    dummy)
        audio_argument=dummy
        ;;
    plugin)
        audio_argument="$prefix/lib/mupen64plus/mupen64plus-audio-sdl.$extension"
        ;;
    *)
        echo "N64_RUNTIME_SMOKE_AUDIO must be 'dummy' or 'plugin'" >&2
        exit 1
        ;;
esac

if [ ! -x "$frontend" ]; then
    echo "N64 Runtime front-end not found: $frontend" >&2
    exit 1
fi
if [ ! -f "$rom" ]; then
    echo "Smoke ROM not found: $rom" >&2
    exit 1
fi

"$project_root/tools/verify_sources.sh"
mkdir -p "$config_dir" "$screenshot_dir"
save_dir="$run_dir/save"
mkdir -p "$save_dir"

if [ -n "$remote_input_file" ]; then
    set -- --remote-input-file "$remote_input_file"
else
    set --
fi
if [ -n "$remote_media_file" ]; then
    set -- "$@" --remote-media-file "$remote_media_file"
fi

"$frontend" \
    --rom "$rom" \
    --core "$core" \
    --config-dir "$config_dir" \
    --data-dir "$prefix/share/mupen64plus" \
    --screenshot-dir "$screenshot_dir" \
    --video "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio "$audio_argument" \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --save-dir "$save_dir" \
    --save-name session \
    --frame "$capture_frame" \
    --width 640 \
    --height 480 \
    "$@" >"$log" 2>&1

screenshot=$(find "$screenshot_dir" -type f -name '*.png' -print -quit)
if [ -z "$screenshot" ]; then
    echo "Direct smoke run completed without producing a screenshot. See $log" >&2
    exit 1
fi

for expected in \
    "N64 Runtime: loaded core 'Mupen64Plus Core' version 2.6.0 from $core_log" \
    "N64 Runtime: loaded Video plugin 'GLideN64' version 2.0.0" \
    "N64 Runtime: loaded Input plugin 'Mupen64Plus SDL Input Plugin' version 2.6.0" \
    "N64 Runtime: loaded RSP plugin 'Hacktarux/Azimer High-Level Emulation RSP Plugin' version 2.6.0" \
    "N64 Runtime STATUS: requested screenshot at frame $capture_frame" \
    "Core INFO: Captured screenshot for frame $capture_frame" \
    "N64 Runtime: execution complete at frame $((capture_frame + 1))"
do
    if ! grep -Fq "$expected" "$log"; then
        echo "Direct smoke log is missing expected evidence: $expected" >&2
        echo "See $log" >&2
        exit 1
    fi
done

if [ -n "$remote_input_file" ] &&
   ! grep -Fq "N64 Runtime remote Controller 2 input active: 0x11" "$log"; then
    echo "Direct smoke log does not show remote Controller 2 injection. See $log" >&2
    exit 1
fi

if [ "$audio_mode" = "plugin" ] &&
   ! grep -Fq "N64 Runtime: loaded Audio plugin 'Mupen64Plus SDL Audio Plugin' version 2.6.0" "$log"; then
    echo "Direct smoke log does not show the source-built audio plugin. See $log" >&2
    exit 1
fi

if [ -n "$remote_media_file" ]; then
    inspector="$project_root/build/test-remote-media-ipc"
    if [ ! -x "$inspector" ] || ! "$inspector" --inspect "$remote_media_file"; then
        echo "Direct smoke did not capture valid remote video/audio IPC. See $log" >&2
        exit 1
    fi
fi

if grep -Fq "UI-Console" "$log"; then
    echo "Direct smoke log unexpectedly contains UI-Console output. See $log" >&2
    exit 1
fi

echo "Direct N64 Runtime smoke run passed."
echo "Log: $log"
echo "Screenshot: $screenshot"
