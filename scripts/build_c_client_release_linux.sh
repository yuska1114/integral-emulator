#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

[[ "$(uname -s)" == Linux ]] || { echo "This build script requires Linux." >&2; exit 1; }
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
output_dir=${1:-"$project_root/dist/linux/INTEGRAL_EMULATOR_C_CLIENT_LINUX_BUILD"}
for command in cc make cmake pkg-config sdl2-config strip python3; do
  command -v "$command" >/dev/null || { echo "Missing command: $command" >&2; exit 1; }
done
cmake_version=$(cmake --version | awk 'NR == 1 { print $3 }')
python3 - "$cmake_version" <<'PY'
import sys
parts = tuple(int(value) for value in sys.argv[1].split(".")[:2])
if parts < (3, 25):
    raise SystemExit("CMake 3.25 or newer is required to build libmobile")
PY

prefix_flags="-ffile-prefix-map=$project_root=. -fdebug-prefix-map=$project_root=."
export INTEGRAL_RELEASE_PREFIX_MAP_FLAGS="$prefix_flags"
export MUPEN_OPTFLAGS="${MUPEN_OPTFLAGS:--O3} $prefix_flags"
export CMAKE_C_FLAGS="${CMAKE_C_FLAGS:-} $prefix_flags"
export CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS:-} $prefix_flags"

make -C "$project_root/c_client" clean
make -C "$project_root/runtimes/gb/src" clean
rm -rf -- "$project_root/runtimes/n64/build" "$project_root/runtimes/n64/release"
make -C "$project_root/runtimes/gb/src" server mobile-runtime
make -C "$project_root/c_client"
make -C "$project_root/runtimes/n64" frontend

rm -rf -- "$output_dir"
mkdir -p "$output_dir/runtimes/gb/libmobile" \
  "$output_dir/runtimes/n64/build/prefix/lib/mupen64plus" \
  "$output_dir/runtimes/n64/build/prefix/share/mupen64plus"
install -m 0755 "$project_root/c_client/build/integral_client" "$output_dir/integral_client"
install -m 0755 "$project_root/runtimes/gb/build_exp/integral_gb_runtime_dual_server" "$output_dir/integral_gb_runtime_dual_server"
install -m 0755 "$project_root/c_client/build/integral_gb_runtime_fixed_host" "$output_dir/integral_gb_runtime_fixed_host"
install -m 0755 "$project_root/runtimes/gb/build_exp/integral_gb_runtime_mobile_runtime" "$output_dir/integral_gb_runtime_mobile_runtime"
libmobile=$(find "$project_root/runtimes/gb/build_exp/libmobile" -maxdepth 1 -type f -name 'libmobile.so.*' | sort | tail -n 1)
[[ -n "$libmobile" ]] || { echo "Missing libmobile shared library." >&2; exit 1; }
install -m 0755 "$libmobile" "$output_dir/runtimes/gb/libmobile/libmobile.so.0.0.0"
install -m 0755 "$project_root/runtimes/n64/build/integral_n64_runtime_frontend" "$output_dir/runtimes/n64/build/integral_n64_runtime_frontend"
install -m 0755 "$project_root/runtimes/n64/build/prefix/lib/libmupen64plus.so.2.0.0" "$output_dir/runtimes/n64/build/prefix/lib/libmupen64plus.so.2.0.0"
for plugin in audio-sdl input-sdl rsp-hle video-GLideN64; do
  install -m 0755 "$project_root/runtimes/n64/build/prefix/lib/mupen64plus/mupen64plus-$plugin.so" "$output_dir/runtimes/n64/build/prefix/lib/mupen64plus/mupen64plus-$plugin.so"
done
for data in font.ttf GLideN64.custom.ini InputAutoCfg.ini mupen64plus.ini mupencheat.txt; do
  install -m 0644 "$project_root/runtimes/n64/build/prefix/share/mupen64plus/$data" "$output_dir/runtimes/n64/build/prefix/share/mupen64plus/$data"
done

while IFS= read -r -d '' binary; do strip --strip-unneeded "$binary"; done < <(
  find "$output_dir" -type f \( -perm -0100 -o -name '*.so' -o -name '*.so.*' \) -print0
)
if grep -aR -l -F "$project_root" "$output_dir" >/dev/null; then
  echo "Local build path remains in Linux artifacts." >&2
  exit 1
fi
python3 "$project_root/scripts/build_artifact_provenance.py" "$output_dir" \
  --platform linux --project-root "$project_root" --write
echo "Linux build artifacts: $output_dir"
