#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

release_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER="$release_root/runtimes/gb/integral_gb_runtime_dual_server"
export INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME="$release_root/runtimes/gb/integral_gb_runtime_fixed_host"
export INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME="$release_root/runtimes/gb/integral_gb_runtime_mobile_runtime"
export INTEGRAL_EMULATOR_N64_RUNTIME_HOME="$release_root/runtimes/n64"
cd "$release_root"
exec "$release_root/client/integral_client" "$@"
