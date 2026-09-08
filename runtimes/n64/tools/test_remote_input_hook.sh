#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
timestamp=$(date +%Y%m%d-%H%M%S)
run_dir="$project_root/runtime/remote-input-hook/$timestamp"
state_file="$run_dir/controller2.bin"
mkdir -p "$run_dir"

(
    while :; do
        "$project_root/build/test-remote-input" --write "$state_file" 0x11
        sleep 0.05
    done
) &
writer_pid=$!
cleanup()
{
    kill "$writer_pid" 2>/dev/null || true
    wait "$writer_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

N64_RUNTIME_SMOKE_REMOTE_INPUT_FILE="$state_file" \
    "$project_root/tools/direct_smoke_test.sh"

echo "N64 Runtime remote Controller 2 hook smoke passed."
