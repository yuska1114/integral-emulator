#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
frontend="$project_root/build/integral_n64_runtime_frontend"
n64_rom=${N64_RUNTIME_TRANSFER_N64_ROM:?set N64_RUNTIME_TRANSFER_N64_ROM}
gb_rom=${N64_RUNTIME_TRANSFER_GB_ROM:?set N64_RUNTIME_TRANSFER_GB_ROM}
gb_save=${N64_RUNTIME_TRANSFER_GB_SAVE:?set N64_RUNTIME_TRANSFER_GB_SAVE}
timestamp=$(date +%Y%m%d-%H%M%S)
run_dir="$project_root/runtime/transfer-pak-smoke/$timestamp"
storage="$run_dir/storage"
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

for file in "$frontend" "$n64_rom" "$gb_rom" "$gb_save"; do
    if [ ! -e "$file" ]; then
        echo "Transfer Pak smoke input not found: $file" >&2
        exit 1
    fi
done

mkdir -p "$storage" "$config_dir" "$screenshot_dir"
cp "$gb_rom" "$storage/slot1.gbc"
cp "$gb_save" "$storage/slot1.sav"

"$frontend" \
    --rom "$n64_rom" \
    --core "$core" \
    --config-dir "$config_dir" \
    --data-dir "$prefix/share/mupen64plus" \
    --screenshot-dir "$screenshot_dir" \
    --video "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    --audio dummy \
    --input "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    --rsp "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    --save-dir "$run_dir/save" --save-name session \
    --transfer-storage "$storage" \
    --frame 120 \
    --width 640 \
    --height 480 >"$log" 2>&1

for expected in \
    "Game controller 0 (Standard controller) has a Transfer pak plugged in" \
    "GB Loader ROM: $storage/slot1.gbc" \
    "GB Loader RAM: $storage/slot1.sav.mupen - 65536" \
    "GB Loader RTC: $storage/slot1.sav.mupen.rtc - 48" \
    "N64 Runtime: execution complete at frame 121"
do
    if ! grep -Fq "$expected" "$log"; then
        echo "Transfer Pak smoke log is missing: $expected" >&2
        echo "See $log" >&2
        exit 1
    fi
done

test "$(wc -c < "$storage/slot1.sav")" -eq 65584
test ! -e "$storage/slot1.sav.mupen"
test ! -e "$storage/slot1.sav.mupen.rtc"
test ! -e "$storage/slot1.sav.merge.part"

screenshot=$(find "$screenshot_dir" -type f -name '*.png' -print -quit)
if [ -z "$screenshot" ]; then
    echo "Transfer Pak smoke did not produce a screenshot. See $log" >&2
    exit 1
fi

echo "N64 Runtime real Transfer Pak smoke run passed."
echo "Log: $log"
echo "Screenshot: $screenshot"
