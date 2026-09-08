#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

root=${TMPDIR:-/tmp}/n64_runtime-gui-cart-test-$$
trap 'rm -rf "$root"' EXIT INT TERM
mkdir -p "$root"
build/test-gui-cart "$root"
