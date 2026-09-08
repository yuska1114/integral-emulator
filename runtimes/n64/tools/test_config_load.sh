#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=${TMPDIR:-/tmp}/integral-n64-runtime-config-test-$$
cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT INT TERM
mkdir -p "$test_root"
cat > "$test_root/n64_runtime.conf" <<'EOF'
controller1=1
controller2=2
controller3=2
controller4=2
EOF

(cd "$test_root" && "$project_root/build/integral_n64_runtime_frontend" --config-load-smoke)
test -f "$test_root/n64_runtime.conf"
grep -q '^controller1=1$' "$test_root/n64_runtime.conf"
test "$(grep -c '^mode=' "$test_root/n64_runtime.conf" || true)" -eq 0
test ! -e "$test_root/n64_runtime.conf.part"
echo "N64 Runtime config load test passed"
