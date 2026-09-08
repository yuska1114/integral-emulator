#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"

case $(uname -s) in
    Darwin)
        core="$prefix/lib/libmupen64plus.dylib"
        extension=dylib
        ;;
    MINGW*|MSYS*|CYGWIN*)
        core="$prefix/lib/mupen64plus.dll"
        extension=dll
        ;;
    *)
        core="$prefix/lib/libmupen64plus.so.2.0.0"
        extension=so
        ;;
esac

missing=0
for path in \
    "$prefix/bin/mupen64plus" \
    "$core" \
    "$prefix/lib/mupen64plus/mupen64plus-input-sdl.$extension" \
    "$prefix/lib/mupen64plus/mupen64plus-audio-sdl.$extension" \
    "$prefix/lib/mupen64plus/mupen64plus-rsp-hle.$extension" \
    "$prefix/lib/mupen64plus/mupen64plus-video-GLideN64.$extension" \
    "$prefix/share/mupen64plus/GLideN64.custom.ini" \
    "$prefix/share/mupen64plus/mupen64plus.ini" \
    "$prefix/share/mupen64plus/InputAutoCfg.ini"
do
    if [ ! -f "$path" ]; then
        echo "Missing source-built artifact: $path" >&2
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    exit 1
fi

echo "Verified source-built runner, core, plugins, and shared data."
