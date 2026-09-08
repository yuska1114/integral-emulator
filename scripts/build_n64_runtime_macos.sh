#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
N64_RUNTIME_DIR="$ROOT_DIR/runtimes/n64"

usage()
{
    cat <<'EOF'
Usage: scripts/build_n64_runtime_macos.sh [--clean] [--release] [--run] [--smoke] [--install-hint] [--help]

Builds the project-local N64 Runtime source tree.

Options:
  --clean         Remove N64 Runtime build outputs before building.
  --release       Also create the local macOS release bundle/zip.
  --run           Launch N64 Runtime after a successful build.
  --smoke         Run the deterministic direct smoke test after building.
  --install-hint  Print Homebrew/Xcode dependency commands.
  --help          Show this help.
EOF
}

if [ ! -d "$N64_RUNTIME_DIR" ]; then
    printf 'Missing N64 Runtime source directory: %s\n' "$N64_RUNTIME_DIR" >&2
    exit 1
fi

run_smoke=false
forward_args=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --smoke)
            run_smoke=true
            ;;
        --clean|--release|--run|--install-hint)
            forward_args="$forward_args $1"
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

cd "$N64_RUNTIME_DIR"

# shellcheck disable=SC2086
tools/build_macos_arm64_user.sh $forward_args

if [ "$run_smoke" = true ]; then
    make smoke
fi
