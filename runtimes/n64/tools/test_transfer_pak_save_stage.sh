#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

root=${TMPDIR:-/tmp}/n64_runtime-transfer-pak-save-test-$$
cleanup() {
    rm -rf "$root"
}
trap cleanup EXIT INT TERM

mkdir -p "$root"
build/test-transfer-pak-save-stage "$root"
test ! -e "$root/slot1.sav.mupen"
test ! -e "$root/slot1.sav.mupen.rtc"
test ! -e "$root/slot1.sav.merge.part"

echo "N64 Runtime MBC3 save round-trip test passed"

