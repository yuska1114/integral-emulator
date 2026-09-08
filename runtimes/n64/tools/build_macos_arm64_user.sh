#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(sed -n '1p' "$ROOT_DIR/VERSION")
RELEASE_NAME="N64 Runtime-$VERSION-macos-arm64"
RELEASE_ROOT="$ROOT_DIR/release/$RELEASE_NAME"
INSTALL_HINT='xcode-select --install
brew install cmake sdl2-compat sdl3 libpng freetype pkg-config'

show_install_hint()
{
    printf '%s\n' "$INSTALL_HINT"
}

usage()
{
    printf 'Usage: %s [--clean] [--release] [--run] [--install-hint] [--help]\n' "$0"
}

need_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing command: %s\n' "$1" >&2
        printf 'Install dependencies with:\n\n' >&2
        show_install_hint >&2
        exit 1
    fi
}

check_platform()
{
    if [ "$(uname -s)" != "Darwin" ]; then
        printf 'This script is for macOS Apple Silicon.\n' >&2
        exit 1
    fi
    if [ "$(uname -m)" != "arm64" ]; then
        printf 'This script requires Apple Silicon arm64; this machine is %s.\n' \
            "$(uname -m)" >&2
        exit 1
    fi
}

check_command_line_tools()
{
    if xcrun --find clang >/dev/null 2>&1; then return; fi
    printf 'Xcode Command Line Tools were not found. Install them with:\n\n' >&2
    printf 'xcode-select --install\n' >&2
    exit 1
}

clean=false
create_release=false
run_after_build=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        --clean) clean=true ;;
        --release) create_release=true ;;
        --run) run_after_build=true ;;
        --install-hint)
            show_install_hint
            exit 0
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

cd "$ROOT_DIR"
check_platform
check_command_line_tools

for command in cc c++ make cmake patch tar sdl2-config pkg-config; do
    need_command "$command"
done
if ! command -v brew >/dev/null 2>&1; then
    printf 'Homebrew was not found. Install it, then run:\n\n' >&2
    printf 'brew install cmake sdl2-compat sdl3 libpng freetype pkg-config\n' >&2
    exit 1
fi
if ! pkg-config --exists freetype2 libpng; then
    printf 'pkg-config cannot find FreeType or libpng.\n' >&2
    printf 'Install dependencies with:\n\n' >&2
    show_install_hint >&2
    exit 1
fi

for component in GLideN64 mupen64plus-audio-sdl mupen64plus-core \
                 mupen64plus-input-sdl mupen64plus-rsp-hle \
                 mupen64plus-ui-console; do
    if [ ! -d "third_party/$component" ]; then
        printf 'Missing vendored source: third_party/%s\n' "$component" >&2
        exit 1
    fi
done

if [ "$clean" = true ]; then
    printf 'Cleaning N64 Runtime build and local release outputs...\n'
    rm -rf build "$RELEASE_ROOT" "$ROOT_DIR/release/$RELEASE_NAME.zip"
fi

printf 'Building the pinned Mupen64Plus source stack and N64 Runtime...\n'
make frontend

if [ ! -x build/integral_n64_runtime_frontend ]; then
    printf 'Expected executable was not produced: build/integral_n64_runtime_frontend\n' >&2
    exit 1
fi

printf 'Running hidden menu smoke tests...\n'
tools/test_gui_smoke.sh

if [ "$create_release" = true ]; then
    printf 'Creating a self-contained local macOS release...\n'
    tools/build_release_macos.sh --skip-build
fi

printf '\nBuild complete.\n'
if [ "$create_release" = true ]; then
    printf 'Launch with:\n  %s/N64 Runtime.app/Contents/MacOS/N64 Runtime\n' \
        "$RELEASE_ROOT"
else
    printf 'Launch with:\n  build/integral_n64_runtime_frontend\n'
fi

if [ "$run_after_build" = true ]; then
    if [ "$create_release" = true ]; then
        exec "$RELEASE_ROOT/N64 Runtime.app/Contents/MacOS/N64 Runtime"
    fi
    exec build/integral_n64_runtime_frontend
fi
