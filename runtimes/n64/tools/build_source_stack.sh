#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix="$project_root/build/prefix"
api_dir="$project_root/third_party/mupen64plus-core/src/api"
work_dir="$project_root/build/work"
gliden64_build="$work_dir/GLideN64"
input_build="$work_dir/mupen64plus-input-sdl"
core_build="$work_dir/mupen64plus-core"
ui_console_build="$work_dir/mupen64plus-ui-console"
input_sdl_cflags=$(sdl2-config --cflags)
input_sdl_ldlibs=$(sdl2-config --libs)
jobs=${JOBS:-}
make_uname=
mupen_cc=${MUPEN_CC:-${CC:-cc}}

if [ -z "$jobs" ]; then
    jobs=$(sysctl -n hw.ncpu 2>/dev/null || true)
fi
if [ -z "$jobs" ]; then
    jobs=2
fi

case $(uname -s) in
    MINGW*|MSYS*|CYGWIN*)
        make_uname=UNAME=MINGW64
        mupen_cc="${mupen_cc:-cc} -std=gnu17"
        ;;
esac

mkdir -p "$prefix" "$work_dir"

export_source_tree()
{
    component=$1
    destination=$2
    source="$project_root/third_party/$component"
    rm -rf "$destination"
    mkdir -p "$destination"
    if [ "$component" = "mupen64plus-ui-console" ]; then
        if [ -e "$source/.git" ] &&
            git -C "$source" rev-parse --verify HEAD >/dev/null 2>&1; then
            git -C "$source" archive HEAD -- \
                LICENSES projects/unix/Makefile src | tar -x -C "$destination"
        else
            mkdir -p "$destination/projects/unix"
            cp "$source/LICENSES" "$destination/LICENSES"
            cp "$source/projects/unix/Makefile" "$destination/projects/unix/Makefile"
            cp -R "$source/src" "$destination/src"
        fi
    elif [ -e "$source/.git" ] &&
        git -C "$source" rev-parse --verify HEAD >/dev/null 2>&1; then
        git -C "$source" archive HEAD | tar -x -C "$destination"
    else
        # Source-distribution ZIPs intentionally contain no nested .git data.
        # Their vendored trees are already clean exports of the pinned commits.
        cp -R "$source"/. "$destination"/
    fi
    find "$destination" \( \
        -name _obj -o \
        -name '*.o' -o \
        -name '*.d' -o \
        -name '*.a' -o \
        -name '*.so' -o \
        -name '*.dylib' -o \
        -name '*.dll' -o \
        -name '*.exe' \
    \) -prune -exec rm -rf {} +
}

build_make_component()
{
    component=$1
    shift
    component_build="$work_dir/$component"
    export_source_tree "$component" "$component_build"
    if [ "$component" = "mupen64plus-audio-sdl" ]; then
        apply_patch_once "$component_build" \
            "$project_root/patches/mupen64plus-audio-sdl-remote-media.patch"
    fi
    make -C "$component_build/projects/unix" -j"$jobs" \
        $make_uname \
        CC="$mupen_cc" \
        OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
        APIDIR="$api_dir" \
        PREFIX="$prefix" \
        INSTALL_STRIP_FLAG= \
        "$@" all
    make -C "$component_build/projects/unix" \
        $make_uname \
        CC="$mupen_cc" \
        OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
        APIDIR="$api_dir" \
        PREFIX="$prefix" \
        INSTALL_STRIP_FLAG= \
        "$@" install
}

build_ui_console()
{
    export_source_tree mupen64plus-ui-console "$ui_console_build"
    make -C "$ui_console_build/projects/unix" -j"$jobs" \
        $make_uname \
        CC="$mupen_cc" \
        OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
        APIDIR="$api_dir" \
        PREFIX="$prefix" \
        INSTALL_STRIP_FLAG= \
        "$@" all
    case $(uname -s) in
        MINGW*|MSYS*|CYGWIN*) ui_console_name=mupen64plus.exe ;;
        *) ui_console_name=mupen64plus ;;
    esac
    install -d "$prefix/bin"
    install -m 0755 \
        "$ui_console_build/projects/unix/$ui_console_name" \
        "$prefix/bin/$ui_console_name"

    # Older builds used upstream `make install`, which also copied desktop
    # integration and documentation that Integral never loads. Remove only
    # those exact stale outputs so incremental release builds stay clean.
    rm -f \
        "$prefix/share/applications/mupen64plus.desktop" \
        "$prefix/share/icons/hicolor/48x48/apps/mupen64plus.png" \
        "$prefix/share/icons/hicolor/scalable/apps/mupen64plus.svg" \
        "$prefix/share/man/man6/mupen64plus.6"
}

apply_patch_once()
{
    destination=$1
    patch_file=$2

    if patch -d "$destination" -p1 --dry-run < "$patch_file" >/dev/null 2>&1; then
        patch -d "$destination" -p1 < "$patch_file"
    elif patch -d "$destination" -p1 -R --dry-run < "$patch_file" >/dev/null 2>&1; then
        echo "Already applied: $(basename "$patch_file")"
    else
        patch -d "$destination" -p1 < "$patch_file"
    fi
}

echo "[1/6] Building Mupen64Plus core"
export_source_tree mupen64plus-core "$core_build"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-physical-hotkeys.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-deferred-stop-confirmation.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-transferpak-mbc3-rtc-sidecar.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-homebrew-transferpak.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-current-rdram.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-startup-focus.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-util-keys.patch"
apply_patch_once "$core_build" "$project_root/patches/mupen64plus-core-transferpak-memory.patch"
cp "$project_root/../common/n64_transfer_memory.h" "$core_build/src/backends/integral_transfer_memory.h"
cp "$project_root/../common/screenshot_path.h" "$core_build/src/main/integral_screenshot_path.h"
cp "$project_root/../common/window_focus.h" "$core_build/src/api/integral_window_focus.h"
make -C "$core_build/projects/unix" -j"$jobs" \
    $make_uname \
    CC="$mupen_cc" \
    OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
    PREFIX="$prefix" \
    INSTALL_STRIP_FLAG= \
    all
make -C "$core_build/projects/unix" \
    $make_uname \
    CC="$mupen_cc" \
    OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
    PREFIX="$prefix" \
    INSTALL_STRIP_FLAG= \
    install
# Check the installed public ABI, not just compilation of the patched source.
case $(uname -s) in
    Darwin) nm -gU "$prefix/lib/libmupen64plus.dylib" | grep -q ' _IntegralSetTransferPakMemory$' ;;
    Linux) nm -D --defined-only "$prefix/lib/libmupen64plus.so.2.0.0" | grep -q ' IntegralSetTransferPakMemory$' ;;
    MINGW*|MSYS*|CYGWIN*) objdump -p "$prefix/lib/mupen64plus.dll" | grep -q 'IntegralSetTransferPakMemory' ;;
esac
if [ "$(uname -s)" = "Darwin" ]; then
    # UI Console checks this app-bundle-style location before honoring the
    # explicit core path. Point it at the same project-local core to avoid a
    # misleading loader error in otherwise successful smoke logs.
    install -d "$prefix/bin/Frameworks"
    ln -sf ../../lib/libmupen64plus.dylib \
        "$prefix/bin/Frameworks/libmupen64plus.dylib"
fi

echo "[2/6] Building SDL input plugin"
export_source_tree mupen64plus-input-sdl "$input_build"
apply_patch_once "$input_build" "$project_root/patches/mupen64plus-input-sdl-physical-scancode.patch"
apply_patch_once "$input_build" "$project_root/patches/mupen64plus-input-sdl-remote-controller2.patch"
make -C "$input_build/projects/unix" -j"$jobs" \
    $make_uname \
    CC="$mupen_cc" \
    OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
    APIDIR="$api_dir" PREFIX="$prefix" INSTALL_STRIP_FLAG= \
    SDL_CFLAGS="$input_sdl_cflags" SDL_LDLIBS="$input_sdl_ldlibs" all
make -C "$input_build/projects/unix" \
    $make_uname \
    CC="$mupen_cc" \
    OPTFLAGS="${MUPEN_OPTFLAGS:--O3}" \
    APIDIR="$api_dir" PREFIX="$prefix" INSTALL_STRIP_FLAG= \
    SDL_CFLAGS="$input_sdl_cflags" SDL_LDLIBS="$input_sdl_ldlibs" install

echo "[3/6] Building SDL audio plugin"
build_make_component mupen64plus-audio-sdl

echo "[4/6] Building HLE RSP plugin"
build_make_component mupen64plus-rsp-hle

echo "[5/6] Building GLideN64 video plugin"
cmake -S "$project_root/third_party/GLideN64/src" \
    -B "$gliden64_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${INTEGRAL_RELEASE_PREFIX_MAP_FLAGS:-}" \
    -DCMAKE_CXX_FLAGS="${INTEGRAL_RELEASE_PREFIX_MAP_FLAGS:-}" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DMUPENPLUSAPI=ON \
    -DMUPENPLUSAPI_GLIDENUI=OFF \
    -DNOHQ=ON \
    -DUSE_SYSTEM_LIBS=ON
cmake --build "$gliden64_build" --parallel "$jobs"
cmake --install "$gliden64_build"

# GLideN64's current macOS CMake configuration builds the Mupen64Plus bundle
# but does not install it. Keep the project-local layout identical on every OS.
case $(uname -s) in
    Darwin)
        gliden64_plugin="$gliden64_build/plugin/Release/mupen64plus-video-GLideN64.dylib"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        gliden64_plugin="$gliden64_build/plugin/Release/mupen64plus-video-GLideN64.dll"
        ;;
    *)
        gliden64_plugin="$gliden64_build/plugin/Release/mupen64plus-video-GLideN64.so"
        ;;
esac
if [ ! -f "$gliden64_plugin" ]; then
    echo "GLideN64 build product not found: $gliden64_plugin" >&2
    exit 1
fi
install -d "$prefix/lib/mupen64plus"
install -m 0644 "$gliden64_plugin" "$prefix/lib/mupen64plus/"

# GLideN64's CMake install rules omit the per-game compatibility database on
# macOS together with the plugin bundle. Install it explicitly so ROM-specific
# framebuffer workarounds from the upstream per-game database are available.
install -d "$prefix/share/mupen64plus"
install -m 0644 \
    "$project_root/third_party/GLideN64/ini/GLideN64.custom.ini" \
    "$prefix/share/mupen64plus/GLideN64.custom.ini"

echo "[6/6] Building temporary console reference runner"
build_ui_console \
    COREDIR="$prefix/lib/" \
    PLUGINDIR="$prefix/lib/mupen64plus" \
    SHAREDIR="$prefix/share/mupen64plus"

echo "Source stack installed under $prefix"
"$project_root/tools/verify_sources.sh"
