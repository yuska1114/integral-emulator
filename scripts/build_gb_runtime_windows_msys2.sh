#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

export PATH=/ucrt64/bin:/usr/bin:/bin:$PATH

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GB_RUNTIME_DIR="$ROOT_DIR/runtimes/gb"
SAMEBOY_DIR="$GB_RUNTIME_DIR/third_party/SameBoy"
SAMEBOY_BUILD_DIR="$SAMEBOY_DIR/build"
SAMEBOY_OBJ_DIR="$SAMEBOY_BUILD_DIR/gb_runtime-core-obj"
SAMEBOY_LIB_DIR="$SAMEBOY_BUILD_DIR/lib"
SAMEBOY_LIB="$SAMEBOY_LIB_DIR/libsameboy.a"
GETLINE_HEADER="$SAMEBOY_OBJ_DIR/gb_runtime_getline_compat.h"
GETLINE_SOURCE="$SAMEBOY_OBJ_DIR/gb_runtime_getline_compat.c"
GETLINE_OBJECT="$SAMEBOY_OBJ_DIR/gb_runtime_getline_compat.o"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *)
        echo "Windows/MSYS2 build must be run from an MSYS2-style shell." >&2
        exit 1
        ;;
esac

for tool in cc ar windres make pkg-config sdl2-config cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing command: $tool" >&2
        exit 1
    fi
done

if ! pkg-config --exists sdl2; then
    echo "pkg-config cannot find SDL2." >&2
    exit 1
fi

if [ ! -d "$SAMEBOY_DIR/Core" ]; then
    echo "Missing vendored SameBoy source: $SAMEBOY_DIR" >&2
    exit 1
fi

mkdir -p "$SAMEBOY_OBJ_DIR" "$SAMEBOY_LIB_DIR"

cat > "$GETLINE_HEADER" <<'EOF'
#ifndef GB_RUNTIME_GETLINE_COMPAT_H
#define GB_RUNTIME_GETLINE_COMPAT_H

#ifdef _WIN32
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif

ssize_t gb_runtime_getline(char **lineptr, size_t *n, FILE *stream);
#define getline gb_runtime_getline
#endif

#endif
EOF

cat > "$GETLINE_SOURCE" <<'EOF'
#include "gb_runtime_getline_compat.h"

#ifdef _WIN32
ssize_t gb_runtime_getline(char **lineptr, size_t *n, FILE *stream)
{
    if (lineptr == NULL || n == NULL || stream == NULL) {
        return -1;
    }
    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (*lineptr == NULL) {
            return -1;
        }
    }
    size_t used = 0;
    int ch;
    while ((ch = fgetc(stream)) != EOF) {
        if (used + 1 >= *n) {
            if (*n > ((size_t)-1) / 2) {
                return -1;
            }
            size_t next_size = *n * 2;
            char *next = realloc(*lineptr, next_size);
            if (next == NULL) {
                return -1;
            }
            *lineptr = next;
            *n = next_size;
        }
        (*lineptr)[used++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (used == 0 && ch == EOF) {
        return -1;
    }
    (*lineptr)[used] = '\0';
    return (ssize_t)used;
}
#endif
EOF

sameboy_version=$(sed -n 's/^VERSION := //p' "$SAMEBOY_DIR/version.mk")
if [ -z "$sameboy_version" ]; then
    sameboy_version=unknown
fi
sameboy_copyright_year=$(grep -Eo '20[2-9][0-9]' "$SAMEBOY_DIR/LICENSE" | tail -n 1 || true)
if [ -z "$sameboy_copyright_year" ]; then
    sameboy_copyright_year=2026
fi

warning_flags="-Wall -Wextra -Wno-attributes -Wno-error=attributes -Wno-missing-braces -Wno-nonnull -Wno-unused-result -Wno-multichar -Wno-int-in-bool-context -Wno-format-truncation ${INTEGRAL_RELEASE_PREFIX_MAP_FLAGS:-}"
if cc --version 2>/dev/null | grep -qi gcc; then
    warning_flags="$warning_flags -Wno-maybe-uninitialized"
fi

objects=
echo "Building SameBoy Core static library for GB Runtime..."
cc -std=gnu11 $warning_flags -c "$GETLINE_SOURCE" -o "$GETLINE_OBJECT"
objects="$objects $GETLINE_OBJECT"

for source in "$SAMEBOY_DIR"/Core/*.c; do
    object="$SAMEBOY_OBJ_DIR/$(basename "$source" .c).o"
    echo "  CC $source"
    cc -std=gnu11 $warning_flags \
        -D_GNU_SOURCE \
        -DGB_VERSION="\"$sameboy_version\"" \
        -DGB_COPYRIGHT_YEAR="\"$sameboy_copyright_year\"" \
        -D_USE_MATH_DEFINES \
        -DGB_INTERNAL \
        -I"$SAMEBOY_DIR" \
        -include "$GETLINE_HEADER" \
        -c "$source" \
        -o "$object"
    objects="$objects $object"
done

rm -f "$SAMEBOY_LIB"
ar -crs "$SAMEBOY_LIB" $objects

echo "Building GB Runtime..."
make -C "$GB_RUNTIME_DIR/src" clean
make -C "$GB_RUNTIME_DIR/src"
make -C "$GB_RUNTIME_DIR/src" mobile-runtime

for binary in \
    integral_gb_runtime_frontend.exe \
    integral_gb_runtime_dual_server.exe \
    integral_gb_runtime_mobile_runtime.exe; do
    if [ ! -f "$GB_RUNTIME_DIR/build_exp/$binary" ]; then
        echo "Missing expected binary: $GB_RUNTIME_DIR/build_exp/$binary" >&2
        exit 1
    fi
done

if [ ! -f "$GB_RUNTIME_DIR/build_exp/bootroms/sgb2_boot.bin" ]; then
    echo "Missing SGB2 SameBoot runtime resource" >&2
    exit 1
fi

echo "GB Runtime Windows/MSYS2 build succeeded."
find "$GB_RUNTIME_DIR/build_exp" -maxdepth 1 -type f -print | sort
