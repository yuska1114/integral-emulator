#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

RELEASE_ROOT=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
if [ -z "$RELEASE_ROOT" ] || [ ! -d "$RELEASE_ROOT" ]; then
  echo "Could not determine the macOS release folder." >&2
  exit 1
fi
if ! command -v xattr >/dev/null 2>&1; then
  echo "The xattr command is not available." >&2
  exit 1
fi

xattr -dr com.apple.quarantine "$RELEASE_ROOT"
printf 'Removed com.apple.quarantine from the macOS release folder:\n%s\n' "$RELEASE_ROOT"
