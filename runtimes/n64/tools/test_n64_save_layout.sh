#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
frontend="$project_root/build/integral_n64_runtime_frontend"
source_rom=${N64_RUNTIME_TRANSFER_N64_ROM:?set N64_RUNTIME_TRANSFER_N64_ROM}
timestamp=$(date +%Y%m%d-%H%M%S)
run_dir="$project_root/runtime/n64-save-layout-test/$timestamp"
rom="$run_dir/sample_game.z64"
save_dir="$run_dir/session-save"
save_file="$save_dir/working.sav"
config_dir="$run_dir/config"
screenshot_dir="$run_dir/screenshots"
log="$run_dir/n64_runtime.log"

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

if [ ! -x "$frontend" ]; then
    echo "N64 Runtime front-end not found: $frontend" >&2
    exit 1
fi
if [ ! -f "$source_rom" ]; then
    echo "Save-layout test ROM not found: $source_rom" >&2
    exit 1
fi

mkdir -p "$run_dir" "$config_dir" "$screenshot_dir" "$save_dir"
cp "$source_rom" "$rom"

"$frontend" \
    --rom "$rom" \
    --core "$core" \
    --config-dir "$config_dir" \
    --data-dir "$prefix/share/mupen64plus" \
    --screenshot-dir "$screenshot_dir" \
    --video "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio dummy \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --save-dir "$save_dir" \
    --save-name working \
    --frame 120 \
    --width 640 \
    --height 480 >"$log" 2>&1

if [ ! -f "$save_file" ]; then
    echo "Expected explicit session save was not created: $save_file" >&2
    echo "See $log" >&2
    exit 1
fi
original_hash=$(shasum -a 256 "$save_file" | awk '{print $1}')
if find "$save_dir" -type f \( -name '*.eep' -o -name '*.sra' -o -name '*.fla' \) | grep -q .; then
    echo "A legacy battery-save extension was created under $save_dir" >&2
    exit 1
fi

config="$config_dir/mupen64plus.cfg"
for expected in \
    "SaveStatePath = \"$save_dir\"" \
    "SaveSRAMPath = \"$save_dir\"" \
    'SaveFilenameOverride = "working"'
do
    if ! grep -Fq "$expected" "$config"; then
        echo "Core config is missing expected save setting: $expected" >&2
        echo "See $config" >&2
        exit 1
    fi
done

if find "$(dirname "$rom")" -maxdepth 2 -type f -name 'sample_game.sav' | grep -q .; then
    echo "Runtime created a ROM-derived save path" >&2
    exit 1
fi

echo "N64 explicit session save layout test passed."
echo "Save: $save_file"
echo "Hash: $original_hash"
echo "Log: $log"
