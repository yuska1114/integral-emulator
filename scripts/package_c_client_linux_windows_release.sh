#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
[ "$#" -ge 2 ] && [ "$#" -le 3 ] || { echo "Usage: $0 LINUX_BUILD_DIR WINDOWS_BUILD_DIR [OUTPUT_DIR]" >&2; exit 2; }
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
abs_dir() { [ -d "$1" ] || { echo "Directory not found: $1" >&2; exit 1; }; (CDPATH= cd -- "$1" && pwd); }
copy_file() { [ -f "$1" ] || { echo "Required release file not found: $1" >&2; exit 1; }; mkdir -p "$(dirname "$2")"; cp -f "$1" "$2"; }
linux_input=$(abs_dir "$1"); windows_input=$(abs_dir "$2")
output_arg=${3:-"$project_root/dist/releases"}; mkdir -p "$output_arg"; output_root=$(abs_dir "$output_arg")
current_commit=$(git -C "$project_root" rev-parse HEAD)
[ -z "$(git -C "$project_root" status --porcelain=v1 --untracked-files=all)" ] || { echo "Release integration requires a clean checkout." >&2; exit 1; }
python3 "$project_root/scripts/build_artifact_provenance.py" "$linux_input" --platform linux --verify --expected-commit "$current_commit" --require-clean
python3 "$project_root/scripts/build_artifact_provenance.py" "$windows_input" --platform windows --verify --expected-commit "$current_commit" --require-clean
formal_args=()
if [ "$output_root" = "$project_root/dist/releases" ]; then python3 "$project_root/scripts/write_release_manifest.py" --preflight-formal; formal_args=(--formal); fi
release_date=${INTEGRAL_CLIENT_RELEASE_DATE:-$(date +%Y%m%d)}
case "$release_date" in [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]) ;; *) echo "Release date must be YYYYMMDD." >&2; exit 1;; esac
version=$(sed -n 's/^#define INTEGRAL_CLIENT_VERSION "\([^"]*\)"/\1/p' "$project_root/c_client/login_client.c" | head -n 1)
[ -n "$version" ] || { echo "Could not read client version." >&2; exit 1; }; version_tag=$(printf '%s' "$version" | tr -cd 'A-Za-z0-9._-')
linux_name="INTEGRAL_EMULATOR_C_CLIENT_${version_tag}_LINUX_X86_64_${release_date}"; windows_name="INTEGRAL_EMULATOR_C_CLIENT_${version_tag}_WINDOWS_X86_64_${release_date}"
linux_package="$output_root/$linux_name"; windows_package="$output_root/$windows_name"; linux_archive="$output_root/$linux_name.tar.gz"; windows_archive="$output_root/$windows_name.zip"
rm -rf -- "$linux_package" "$windows_package"; rm -f -- "$linux_archive" "$linux_archive.sha256" "$windows_archive" "$windows_archive.sha256"

copy_common_legal() {
  target=$1
  copy_file "$project_root/LICENSE" "$target/LICENSE"
  copy_file "$project_root/c_client/BINARY_LICENSE_SCOPE.md" "$target/LICENSE_SCOPE.md"
  copy_file "$project_root/c_client/BINARY_THIRD_PARTY_NOTICES.md" "$target/THIRD_PARTY_NOTICES.md"
  for license in AGPL-3.0-or-later GPL-2.0-or-later GPL-3.0-or-later LGPL-3.0-or-later; do copy_file "$project_root/LICENSES/$license.txt" "$target/LICENSES/$license.txt"; done
  copy_file "$project_root/runtimes/gb/third_party/SameBoy/LICENSE" "$target/LICENSES/third-party/SameBoy/LICENSE"
  copy_file "$project_root/runtimes/gb/third_party/libmobile/COPYING" "$target/LICENSES/third-party/libmobile/COPYING"
  copy_file "$project_root/runtimes/gb/third_party/libmobile/COPYING.LESSER" "$target/LICENSES/third-party/libmobile/COPYING.LESSER"
  copy_file "$project_root/runtimes/n64/third_party/mupen64plus-core/LICENSES" "$target/LICENSES/third-party/mupen64plus-core/LICENSES"
  for license in gpl-license lgpl-license font-license; do copy_file "$project_root/runtimes/n64/third_party/mupen64plus-core/doc/$license" "$target/LICENSES/third-party/mupen64plus-core/$license"; done
  copy_file "$project_root/runtimes/n64/third_party/mupen64plus-input-sdl/COPYING" "$target/LICENSES/third-party/mupen64plus-input-sdl/COPYING"
  copy_file "$project_root/runtimes/n64/third_party/mupen64plus-input-sdl/LICENSES" "$target/LICENSES/third-party/mupen64plus-input-sdl/LICENSES"
  copy_file "$project_root/runtimes/n64/third_party/mupen64plus-audio-sdl/LICENSES" "$target/LICENSES/third-party/mupen64plus-audio-sdl/LICENSES"
  copy_file "$project_root/runtimes/n64/third_party/mupen64plus-rsp-hle/LICENSES" "$target/LICENSES/third-party/mupen64plus-rsp-hle/LICENSES"
  copy_file "$project_root/runtimes/n64/third_party/GLideN64/LICENSE" "$target/LICENSES/third-party/GLideN64/LICENSE"
  copy_file "$project_root/runtimes/n64/third_party/GLideN64/licenses/Glow/LICENSE" "$target/LICENSES/third-party/GLideN64/Glow/LICENSE"
  copy_file "$project_root/runtimes/n64/third_party/GLideN64/licenses/gles2n64/LICENSE" "$target/LICENSES/third-party/GLideN64/gles2n64/LICENSE"
}
copy_n64() {
  platform=$1; src=$2; dst=$3
  if [ "$platform" = linux ]; then frontend=integral_n64_runtime_frontend; core=libmupen64plus.so.2.0.0; ext=so; else frontend=integral_n64_runtime_frontend.exe; core=mupen64plus.dll; ext=dll; fi
  copy_file "$src/$frontend" "$dst/$frontend"; copy_file "$src/prefix/lib/$core" "$dst/prefix/lib/$core"
  for plugin in audio-sdl input-sdl rsp-hle video-GLideN64; do copy_file "$src/prefix/lib/mupen64plus/mupen64plus-$plugin.$ext" "$dst/prefix/lib/mupen64plus/mupen64plus-$plugin.$ext"; done
  for data in font.ttf GLideN64.custom.ini InputAutoCfg.ini mupen64plus.ini mupencheat.txt; do copy_file "$src/prefix/share/mupen64plus/$data" "$dst/prefix/share/mupen64plus/$data"; done
}

mkdir -p "$linux_package/roms" "$linux_package/export" "$windows_package/roms" "$windows_package/export"
copy_file "$project_root/c_client/RELEASE_README.txt" "$linux_package/README.txt"; copy_file "$project_root/c_client/linux_release_launcher.sh" "$linux_package/INTEGRAL_EMULATOR.sh"
copy_file "$linux_input/integral_client" "$linux_package/client/integral_client"
copy_file "$linux_input/assets/integral_emulator_icon.bmp" "$linux_package/assets/integral_emulator_icon.bmp"
for runtime in dual_server fixed_host mobile_runtime; do copy_file "$linux_input/integral_gb_runtime_$runtime" "$linux_package/runtimes/gb/integral_gb_runtime_$runtime"; done
copy_file "$linux_input/runtimes/gb/libmobile/libmobile.so.0.0.0" "$linux_package/runtimes/gb/libmobile/libmobile.so.0"
copy_file "$project_root/runtimes/gb/assets/bootroms/sgb2_boot.bin" "$linux_package/runtimes/gb/bootroms/sgb2_boot.bin"
copy_file "$project_root/c_client/integral_client.conf.example" "$linux_package/config/integral_client.conf.example"; copy_n64 linux "$linux_input/runtimes/n64/build" "$linux_package/runtimes/n64/build"; copy_common_legal "$linux_package"
copy_file "$project_root/runtimes/gb/third_party/libmobile/COPYING.LESSER" "$linux_package/LICENSES/runtime-dependencies/libmobile/COPYING.LESSER"
cat > "$linux_package/RUNTIME_DEPENDENCIES.md" <<'EOF'
# 同梱共有ライブラリ
| ファイル | コンポーネント | ライセンス | 原文 |
| --- | --- | --- | --- |
| `runtimes/gb/libmobile/libmobile.so.0` | libmobile | LGPL-3.0-or-later | `LICENSES/runtime-dependencies/libmobile/COPYING.LESSER` |

その他のLinux共有ライブラリはOSから提供され、配布物には同梱しません。
EOF
chmod 0755 "$linux_package/INTEGRAL_EMULATOR.sh" "$linux_package/client/integral_client" "$linux_package/runtimes/gb/integral_gb_runtime_"* "$linux_package/runtimes/n64/build/integral_n64_runtime_frontend"

copy_file "$project_root/c_client/RELEASE_README.txt" "$windows_package/README.txt"; copy_file "$windows_input/INTEGRAL EMULATOR.exe" "$windows_package/INTEGRAL_EMULATOR.exe"
copy_file "$windows_input/client/integral_client.exe" "$windows_package/client/integral_client.exe"
for runtime in frontend dual_server fixed_host mobile_runtime; do copy_file "$windows_input/runtimes/gb/integral_gb_runtime_$runtime.exe" "$windows_package/runtimes/gb/integral_gb_runtime_$runtime.exe"; done
copy_file "$windows_input/runtimes/gb/bootroms/sgb2_boot.bin" "$windows_package/runtimes/gb/bootroms/sgb2_boot.bin"; copy_file "$windows_input/config/integral_client.conf.example" "$windows_package/config/integral_client.conf.example"; copy_file "$windows_input/ssl/cert.pem" "$windows_package/ssl/cert.pem"; copy_n64 windows "$windows_input/runtimes/n64/build" "$windows_package/runtimes/n64/build"
windows_dlls=(SDL2.dll SDL2_ttf.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll libcrypto-3-x64.dll libfreetype-6.dll libgcc_s_seh-1.dll libglib-2.0-0.dll libgraphite2.dll libharfbuzz-0.dll libiconv-2.dll libintl-8.dll libmobile.dll libpcre2-8-0.dll libpng16-16.dll libssl-3-x64.dll libstdc++-6.dll libwinpthread-1.dll zlib1.dll)
for dll in "${windows_dlls[@]}"; do copy_file "$windows_input/dll/$dll" "$windows_package/dll/$dll"; done
copy_common_legal "$windows_package"; copy_file "$windows_input/RUNTIME_DEPENDENCIES.md" "$windows_package/RUNTIME_DEPENDENCIES.md"
while IFS= read -r relative; do [ -z "$relative" ] || copy_file "$windows_input/LICENSES/runtime-dependencies/$relative" "$windows_package/LICENSES/runtime-dependencies/$relative"; done < "$windows_input/RUNTIME_DEPENDENCY_LICENSE_FILES.txt"

python3 "$project_root/scripts/verify_c_client_release_licenses.py" "$linux_package" --platform linux
python3 "$project_root/scripts/verify_c_client_release_licenses.py" "$windows_package" --platform windows

python3 "$project_root/scripts/build_artifact_provenance.py" "$linux_package" --platform linux --write --project-root "$project_root" --source-provenance-root "$linux_input"
python3 "$project_root/scripts/build_artifact_provenance.py" "$windows_package" --platform windows --write --project-root "$project_root" --source-provenance-root "$windows_input"

cmp -s "$linux_package/README.txt" "$windows_package/README.txt" || { echo "Packaged README files differ." >&2; exit 1; }
python3 "$project_root/scripts/write_release_manifest.py" "$linux_package" --platform linux --version "$version" --minimum-os "Ubuntu 24.04 LTS" ${formal_args[@]+"${formal_args[@]}"}
python3 "$project_root/scripts/write_release_manifest.py" "$windows_package" --platform windows --version "$version" --minimum-os "Windows 11 x86-64" ${formal_args[@]+"${formal_args[@]}"}
COPYFILE_DISABLE=1 tar --no-xattrs -czf "$linux_archive" -C "$output_root" "$linux_name"; (cd "$output_root" && zip -qry "$windows_archive" "$windows_name")
python3 "$project_root/scripts/write_release_manifest.py" "$linux_package" --platform linux --version "$version" --minimum-os "Ubuntu 24.04 LTS" --archive "$linux_archive" ${formal_args[@]+"${formal_args[@]}"}
python3 "$project_root/scripts/write_release_manifest.py" "$windows_package" --platform windows --version "$version" --minimum-os "Windows 11 x86-64" --archive "$windows_archive" ${formal_args[@]+"${formal_args[@]}"}
(cd "$output_root" && shasum -a 256 "$(basename "$linux_archive")" > "$(basename "$linux_archive").sha256" && shasum -a 256 "$(basename "$windows_archive")" > "$(basename "$windows_archive").sha256")
echo "Linux release: $linux_archive"; echo "Windows release: $windows_archive"
