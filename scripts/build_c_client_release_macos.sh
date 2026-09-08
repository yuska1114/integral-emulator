#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if ! command -v python3 >/dev/null 2>&1; then
  echo "Missing command: python3" >&2
  exit 1
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This release script is for macOS only." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PREFIX_MAP_FLAGS="-ffile-prefix-map=${PROJECT_ROOT}=. -fdebug-prefix-map=${PROJECT_ROOT}=."
export INTEGRAL_RELEASE_PREFIX_MAP_FLAGS="${PREFIX_MAP_FLAGS}"
export MUPEN_OPTFLAGS="${MUPEN_OPTFLAGS:--O3} ${PREFIX_MAP_FLAGS}"
export CMAKE_C_FLAGS="${CMAKE_C_FLAGS:-} ${PREFIX_MAP_FLAGS}"
export CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS:-} ${PREFIX_MAP_FLAGS}"
INTEGRAL_EMULATOR_MACOS_DEPLOYMENT_TARGET="${INTEGRAL_EMULATOR_MACOS_DEPLOYMENT_TARGET:-26.0}"
INTEGRAL_EMULATOR_RELEASE_ROOT="${INTEGRAL_EMULATOR_RELEASE_ROOT:-${PROJECT_ROOT}/dist/releases}"
INTEGRAL_EMULATOR_RELEASE_APP_NAME="${INTEGRAL_EMULATOR_RELEASE_APP_NAME:-INTEGRAL EMULATOR}"
INTEGRAL_EMULATOR_RELEASE_PACKAGE_NAME="${INTEGRAL_EMULATOR_RELEASE_PACKAGE_NAME:-}"
INTEGRAL_EMULATOR_RELEASE_SKIP_CODESIGN="${INTEGRAL_EMULATOR_RELEASE_SKIP_CODESIGN:-0}"
INTEGRAL_EMULATOR_RELEASE_SKIP_BUILD="${INTEGRAL_EMULATOR_RELEASE_SKIP_BUILD:-0}"
MACOS_DEPLOYMENT_TARGET="${INTEGRAL_EMULATOR_MACOS_DEPLOYMENT_TARGET}"
export MACOSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET}"
RELEASE_ROOT="${INTEGRAL_EMULATOR_RELEASE_ROOT}"
mkdir -p "${RELEASE_ROOT}"
APP_NAME="${INTEGRAL_EMULATOR_RELEASE_APP_NAME}"
VERSION="$(
  sed -n 's/^#define INTEGRAL_CLIENT_VERSION "\([^"]*\)"/\1/p' "${PROJECT_ROOT}/c_client/login_client.c" | head -n 1
)"
if [[ -z "${VERSION}" ]]; then
  VERSION="0.0"
fi
VERSION_TAG="$(printf '%s' "${VERSION}" | tr -cd 'A-Za-z0-9._-')"
RELEASE_DATE="${INTEGRAL_CLIENT_RELEASE_DATE:-$(date +%Y%m%d)}"
[[ "${RELEASE_DATE}" =~ ^[0-9]{8}$ ]] || { echo "Release date must be YYYYMMDD." >&2; exit 1; }
BUNDLE_VERSION="$(printf '%s' "${VERSION}" | sed -E 's/[^0-9.].*$//; s/^\.*//; s/\.*$//')"
if [[ ! "${BUNDLE_VERSION}" =~ ^[0-9]+([.][0-9]+)*$ ]]; then
  echo "Cannot derive a numeric macOS bundle version from: ${VERSION}" >&2
  exit 1
fi
PACKAGE_NAME="${INTEGRAL_EMULATOR_RELEASE_PACKAGE_NAME:-INTEGRAL_EMULATOR_C_CLIENT_${VERSION_TAG}_MACOS_ARM64_${RELEASE_DATE}}"
PACKAGE_DIR="${RELEASE_ROOT}/${PACKAGE_NAME}"
APP_DIR="${PACKAGE_DIR}/${APP_NAME}.app"
CONTENTS_DIR="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
FRAMEWORKS_DIR="${CONTENTS_DIR}/Frameworks"
CLIENT_DIR="${RESOURCES_DIR}/client"
GB_RUNTIME_DIR="${RESOURCES_DIR}/runtimes/gb"
N64_RUNTIME_DIR="${RESOURCES_DIR}/runtimes/n64"
ZIP_PATH="${RELEASE_ROOT}/${PACKAGE_NAME}.zip"
FORMAL_ARGS=()
if [[ "$(cd "${RELEASE_ROOT}" && pwd)" == "${PROJECT_ROOT}/dist/releases" ]]; then
  python3 "${PROJECT_ROOT}/scripts/write_release_manifest.py" --preflight-formal
  FORMAL_ARGS=(--formal)
fi

if [[ "${INTEGRAL_EMULATOR_RELEASE_SKIP_BUILD}" != "1" ]]; then
  echo "Building c_client..."
  make -C "${PROJECT_ROOT}/c_client"

  echo "Building GB Runtime runtime binaries..."
  make -C "${PROJECT_ROOT}/runtimes/gb/src" server mobile-runtime

  echo "Building N64 Runtime runtime release..."
  "${PROJECT_ROOT}/scripts/build_n64_runtime_macos.sh" --release
else
  echo "Skipping builds cannot produce trustworthy build provenance." >&2
  exit 1
fi
N64_RUNTIME_VERSION="$(tr -d '\r\n' < "${PROJECT_ROOT}/runtimes/n64/VERSION")"
N64_RUNTIME_RELEASE_DIR="${PROJECT_ROOT}/runtimes/n64/release/N64 Runtime-${N64_RUNTIME_VERSION}-macos-$(uname -m)"
if [[ ! -x "${N64_RUNTIME_RELEASE_DIR}/build/integral_n64_runtime_frontend" ]]; then
  echo "N64 Runtime release is missing: ${N64_RUNTIME_RELEASE_DIR}" >&2
  exit 1
fi

echo "Creating app bundle..."
rm -rf "${PACKAGE_DIR}" "${ZIP_PATH}"
mkdir -p \
  "${MACOS_DIR}" \
  "${CLIENT_DIR}" \
  "${GB_RUNTIME_DIR}" \
  "${GB_RUNTIME_DIR}/bootroms" \
  "${N64_RUNTIME_DIR}" \
  "${FRAMEWORKS_DIR}" \
  "${PACKAGE_DIR}/LICENSES/third-party" \
  "${PACKAGE_DIR}/LICENSES/runtime-dependencies" \
  "${PACKAGE_DIR}/roms" \
  "${PACKAGE_DIR}/config" \
  "${PACKAGE_DIR}/export"

cp -f "${PROJECT_ROOT}/c_client/build/integral_client" "${CLIENT_DIR}/integral_client"
cp -f "${PROJECT_ROOT}/c_client/integral_client.conf.example" "${PACKAGE_DIR}/config/integral_client.conf.example"
cp -f "${PROJECT_ROOT}/c_client/RELEASE_README.txt" "${PACKAGE_DIR}/README.txt"
cp -f "${PROJECT_ROOT}/scripts/unlock_macos.sh" "${PACKAGE_DIR}/unlock_macos.sh"
chmod +x "${PACKAGE_DIR}/unlock_macos.sh"
cp -f "${PROJECT_ROOT}/runtimes/gb/build_exp/integral_gb_runtime_dual_server" "${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server"
cp -f "${PROJECT_ROOT}/c_client/build/integral_gb_runtime_fixed_host" "${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host"
cp -f "${PROJECT_ROOT}/runtimes/gb/build_exp/integral_gb_runtime_mobile_runtime" "${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime"
mkdir -p "${GB_RUNTIME_DIR}/libmobile"
cp -f "${PROJECT_ROOT}/runtimes/gb/build_exp/libmobile/libmobile.0.dylib" "${GB_RUNTIME_DIR}/libmobile/libmobile.0.dylib"
cp -f "${PROJECT_ROOT}/runtimes/gb/assets/bootroms/sgb2_boot.bin" "${GB_RUNTIME_DIR}/bootroms/sgb2_boot.bin"
mkdir -p "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus" "${N64_RUNTIME_DIR}/build/prefix/share/mupen64plus"
mkdir -p "${N64_RUNTIME_DIR}/build/deps"
cp -f "${N64_RUNTIME_RELEASE_DIR}/build/integral_n64_runtime_frontend" "${N64_RUNTIME_DIR}/build/integral_n64_runtime_frontend"
cp -f "${N64_RUNTIME_RELEASE_DIR}/build/prefix/lib/libmupen64plus.dylib" "${N64_RUNTIME_DIR}/build/prefix/lib/libmupen64plus.dylib"
for plugin in mupen64plus-audio-sdl.dylib mupen64plus-input-sdl.dylib mupen64plus-rsp-hle.dylib mupen64plus-video-GLideN64.dylib; do
  cp -f "${N64_RUNTIME_RELEASE_DIR}/build/prefix/lib/mupen64plus/${plugin}" "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus/${plugin}"
done
for data in font.ttf GLideN64.custom.ini InputAutoCfg.ini mupen64plus.ini mupencheat.txt; do
  cp -f "${N64_RUNTIME_RELEASE_DIR}/build/prefix/share/mupen64plus/${data}" "${N64_RUNTIME_DIR}/build/prefix/share/mupen64plus/${data}"
done
for dep in libSDL2-2.0.0.dylib libSDL3.dylib libfreetype.6.dylib libpng16.16.dylib; do
  cp -f "${N64_RUNTIME_RELEASE_DIR}/build/deps/${dep}" "${N64_RUNTIME_DIR}/build/deps/${dep}"
done
cp -f "${PROJECT_ROOT}/LICENSE" "${PACKAGE_DIR}/LICENSE"
cp -f "${PROJECT_ROOT}/c_client/BINARY_LICENSE_SCOPE.md" "${PACKAGE_DIR}/LICENSE_SCOPE.md"
cp -f "${PROJECT_ROOT}/c_client/BINARY_THIRD_PARTY_NOTICES.md" "${PACKAGE_DIR}/THIRD_PARTY_NOTICES.md"
for license in AGPL-3.0-or-later GPL-2.0-or-later GPL-3.0-or-later LGPL-3.0-or-later; do cp -f "${PROJECT_ROOT}/LICENSES/${license}.txt" "${PACKAGE_DIR}/LICENSES/${license}.txt"; done
cp -f "${PROJECT_ROOT}/runtimes/gb/third_party/SameBoy/LICENSE" "${PACKAGE_DIR}/LICENSES/third-party/SameBoy-LICENSE"
cp -f "${PROJECT_ROOT}/runtimes/gb/third_party/libmobile/COPYING" "${PACKAGE_DIR}/LICENSES/third-party/libmobile-COPYING"
cp -f "${PROJECT_ROOT}/runtimes/gb/third_party/libmobile/COPYING.LESSER" "${PACKAGE_DIR}/LICENSES/third-party/libmobile-COPYING.LESSER"
for component in mupen64plus-core mupen64plus-audio-sdl mupen64plus-rsp-hle; do
  mkdir -p "${PACKAGE_DIR}/LICENSES/third-party/${component}"
  cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/${component}/LICENSES" "${PACKAGE_DIR}/LICENSES/third-party/${component}/LICENSES"
done
mkdir -p "${PACKAGE_DIR}/LICENSES/third-party/mupen64plus-input-sdl" "${PACKAGE_DIR}/LICENSES/third-party/GLideN64"
cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/mupen64plus-input-sdl/COPYING" "${PACKAGE_DIR}/LICENSES/third-party/mupen64plus-input-sdl/COPYING"
cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/mupen64plus-input-sdl/LICENSES" "${PACKAGE_DIR}/LICENSES/third-party/mupen64plus-input-sdl/LICENSES"
cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/GLideN64/LICENSE" "${PACKAGE_DIR}/LICENSES/third-party/GLideN64/LICENSE"
for license in gpl-license lgpl-license font-license; do
  cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/mupen64plus-core/doc/${license}" "${PACKAGE_DIR}/LICENSES/third-party/mupen64plus-core/${license}"
done
mkdir -p "${PACKAGE_DIR}/LICENSES/third-party/GLideN64/Glow" "${PACKAGE_DIR}/LICENSES/third-party/GLideN64/gles2n64"
cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/GLideN64/licenses/Glow/LICENSE" "${PACKAGE_DIR}/LICENSES/third-party/GLideN64/Glow/LICENSE"
cp -f "${PROJECT_ROOT}/runtimes/n64/third_party/GLideN64/licenses/gles2n64/LICENSE" "${PACKAGE_DIR}/LICENSES/third-party/GLideN64/gles2n64/LICENSE"

cat > "${MACOS_DIR}/${APP_NAME}" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
APP_BUNDLE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RELEASE_DIR="$(cd "${APP_BUNDLE}/.." && pwd)"
RESOURCES_DIR="${APP_BUNDLE}/Contents/Resources"
mkdir -p "${RELEASE_DIR}/roms" "${RELEASE_DIR}/config" "${RELEASE_DIR}/export"
cd "${RELEASE_DIR}"
export INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER="${RESOURCES_DIR}/runtimes/gb/integral_gb_runtime_dual_server"
export INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME="${RESOURCES_DIR}/runtimes/gb/integral_gb_runtime_fixed_host"
export INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME="${RESOURCES_DIR}/runtimes/gb/integral_gb_runtime_mobile_runtime"
export INTEGRAL_EMULATOR_N64_RUNTIME_HOME="${RESOURCES_DIR}/runtimes/n64"
exec "${RESOURCES_DIR}/client/integral_client" "$@"
LAUNCHER
chmod +x "${MACOS_DIR}/${APP_NAME}"

cat > "${CONTENTS_DIR}/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleDisplayName</key>
  <string>${APP_NAME}</string>
  <key>CFBundleExecutable</key>
  <string>${APP_NAME}</string>
  <key>CFBundleIdentifier</key>
  <string>org.integral-emulator.client</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>${APP_NAME}</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>${BUNDLE_VERSION}</string>
  <key>CFBundleVersion</key>
  <string>${BUNDLE_VERSION}</string>
  <key>LSMinimumSystemVersion</key>
  <string>${MACOS_DEPLOYMENT_TARGET}</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
PLIST

make_icon() {
  local source_png="${PROJECT_ROOT}/assets/product/integral_emulator_icon.png"
  local iconset="${RESOURCES_DIR}/AppIcon.iconset"
  local icns="${RESOURCES_DIR}/AppIcon.icns"
  if [[ ! -f "${source_png}" ]] || ! command -v sips >/dev/null || ! command -v iconutil >/dev/null; then
    return 0
  fi

  mkdir -p "${iconset}"
  for size in 16 32 128 256 512; do
    sips -z "${size}" "${size}" "${source_png}" --out "${iconset}/icon_${size}x${size}.png" >/dev/null
    local retina=$((size * 2))
    sips -z "${retina}" "${retina}" "${source_png}" --out "${iconset}/icon_${size}x${size}@2x.png" >/dev/null
  done
  if ! iconutil -c icns "${iconset}" -o "${icns}" >/dev/null 2>&1; then
    if ! python3 - "${source_png}" "${icns}" <<'PY'
from pathlib import Path
import sys

from PIL import Image

source = Image.open(sys.argv[1]).convert("RGBA")
source.resize((1024, 1024), Image.Resampling.LANCZOS).save(
    Path(sys.argv[2]), format="ICNS"
)
PY
    then
      echo "Warning: icon generation failed; continuing without a custom app icon." >&2
      rm -rf "${iconset}"
      return 0
    fi
  fi
  rm -rf "${iconset}"
  /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "${CONTENTS_DIR}/Info.plist" >/dev/null
}

is_system_dependency() {
  local dep="$1"
  [[ "${dep}" == /usr/lib/* || "${dep}" == /System/Library/* ]]
}

copy_and_fix_dylibs() {
  local -a queue=(
    "${CLIENT_DIR}/integral_client"
    "${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server"
    "${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host"
    "${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime"
    "${GB_RUNTIME_DIR}/libmobile/libmobile.0.dylib"
  )
  local -a processed=()

  while ((${#queue[@]} > 0)); do
    local file="${queue[0]}"
    queue=("${queue[@]:1}")
    local already=0
    if ((${#processed[@]} > 0)); then
      for item in "${processed[@]}"; do
        if [[ "${item}" == "${file}" ]]; then
          already=1
          break
        fi
      done
    fi
    if ((already)); then
      continue
    fi
    processed+=("${file}")

    while IFS= read -r dep; do
      [[ -z "${dep}" ]] && continue
      [[ "${dep}" == @* ]] && continue
      is_system_dependency "${dep}" && continue
      if [[ ! -f "${dep}" ]]; then
        echo "Warning: dependency not found: ${dep}" >&2
        continue
      fi
      local name
      name="$(basename "${dep}")"
      local target="${FRAMEWORKS_DIR}/${name}"
      if [[ ! -f "${target}" ]]; then
        cp -f "${dep}" "${target}"
        chmod u+w "${target}"
        queue+=("${target}")
      fi
    done < <(otool -L "${file}" | awk 'NR > 1 {print $1}')
  done

  local -a machos=(
    "${CLIENT_DIR}/integral_client"
    "${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server"
    "${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host"
    "${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime"
    "${GB_RUNTIME_DIR}/libmobile/libmobile.0.dylib"
  )
  while IFS= read -r dylib; do
    machos+=("${dylib}")
  done < <(find "${FRAMEWORKS_DIR}" -type f -name '*.dylib' | sort)

  for file in "${machos[@]}"; do
    [[ -f "${file}" ]] || continue
    local loader_ref="@loader_path"
    if [[ "${file}" == "${CLIENT_DIR}"/* ]]; then
      loader_ref="@loader_path/../../Frameworks"
    elif [[ "${file}" == "${GB_RUNTIME_DIR}/libmobile"/* ]]; then
      loader_ref="@loader_path/../../../../Frameworks"
    elif [[ "${file}" != "${FRAMEWORKS_DIR}"/* ]]; then
      loader_ref="@loader_path/../../../Frameworks"
    else
      install_name_tool -id "@loader_path/$(basename "${file}")" "${file}" || true
    fi
    while IFS= read -r dep; do
      [[ -z "${dep}" ]] && continue
      [[ "${dep}" == @* ]] && continue
      is_system_dependency "${dep}" && continue
      local name
      name="$(basename "${dep}")"
      if [[ -f "${FRAMEWORKS_DIR}/${name}" ]]; then
        install_name_tool -change "${dep}" "${loader_ref}/${name}" "${file}" || true
      fi
    done < <(otool -L "${file}" | awk 'NR > 1 {print $1}')
  done

  if strings "${FRAMEWORKS_DIR}/libSDL2-2.0.0.dylib" 2>/dev/null | grep -q 'libSDL3.dylib'; then
    local sdl3_path=""
    for candidate in \
      /opt/homebrew/lib/libSDL3.dylib \
      /usr/local/lib/libSDL3.dylib \
      /opt/homebrew/opt/sdl3/lib/libSDL3.dylib \
      /usr/local/opt/sdl3/lib/libSDL3.dylib; do
      if [[ -f "${candidate}" ]]; then
        sdl3_path="${candidate}"
        break
      fi
    done
    if [[ -z "${sdl3_path}" ]]; then
      echo "Warning: libSDL2 is sdl2-compat but libSDL3.dylib was not found." >&2
    else
      cp -f "${sdl3_path}" "${FRAMEWORKS_DIR}/libSDL3.dylib"
      chmod u+w "${FRAMEWORKS_DIR}/libSDL3.dylib"
      install_name_tool -id "@loader_path/libSDL3.dylib" "${FRAMEWORKS_DIR}/libSDL3.dylib" || true
    fi
  fi
}

version_is_greater() {
  local actual="$1"
  local declared="$2"
  local actual_major=0 actual_minor=0 actual_patch=0
  local declared_major=0 declared_minor=0 declared_patch=0
  IFS=. read -r actual_major actual_minor actual_patch <<< "${actual}"
  IFS=. read -r declared_major declared_minor declared_patch <<< "${declared}"
  actual_minor="${actual_minor:-0}"
  actual_patch="${actual_patch:-0}"
  declared_minor="${declared_minor:-0}"
  declared_patch="${declared_patch:-0}"
  (( actual_major > declared_major )) ||
    (( actual_major == declared_major && actual_minor > declared_minor )) ||
    (( actual_major == declared_major && actual_minor == declared_minor && actual_patch > declared_patch ))
}

verify_macos_deployment_targets() {
  local failures=0
  local inspected=0
  while IFS= read -r -d '' candidate; do
    if ! file -b "${candidate}" | grep -q 'Mach-O'; then
      continue
    fi
    inspected=$((inspected + 1))
    local found=0
    while IFS= read -r minos; do
      [[ -z "${minos}" ]] && continue
      found=1
      if version_is_greater "${minos}" "${MACOS_DEPLOYMENT_TARGET}"; then
        echo "Deployment target mismatch: ${candidate} requires macOS ${minos}, bundle declares ${MACOS_DEPLOYMENT_TARGET}." >&2
        failures=$((failures + 1))
      fi
    done < <(
      otool -l "${candidate}" | awk '
        $1 == "minos" { print $2 }
        $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { legacy = 1; next }
        legacy && $1 == "version" { print $2; legacy = 0 }
      '
    )
    if ((found == 0)); then
      echo "Deployment target metadata missing: ${candidate}" >&2
      failures=$((failures + 1))
    fi
  done < <(find "${APP_DIR}" -type f -print0)
  if ((inspected == 0)); then
    echo "No Mach-O files found in app bundle." >&2
    return 1
  fi
  if ((failures > 0)); then
    echo "Release stopped: ${failures} Mach-O deployment-target issue(s)." >&2
    return 1
  fi
  echo "Verified ${inspected} Mach-O files for macOS ${MACOS_DEPLOYMENT_TARGET}."
}

make_icon
copy_and_fix_dylibs

remove_unused_sdl2_rpath() {
  local unwanted='@loader_path/../../../../opt/sdl3/lib'
  while IFS= read -r -d '' candidate; do
    file -b "${candidate}" | grep -q 'Mach-O' || continue
    while otool -l "${candidate}" | awk '
      $1 == "cmd" && $2 == "LC_RPATH" { rpath = 1; next }
      rpath && $1 == "path" { print $2; rpath = 0 }
    ' | grep -Fxq "${unwanted}"; do
      install_name_tool -delete_rpath "${unwanted}" "${candidate}"
    done
  done < <(find "${APP_DIR}" -type f -print0)
  if find "${APP_DIR}" -type f -print0 | xargs -0 strings 2>/dev/null | grep -Fq "${unwanted}"; then
    echo "Unused SDL3 RPATH remains in macOS app bundle." >&2
    exit 1
  fi
}

remove_unused_sdl2_rpath
verify_macos_deployment_targets

copy_dependency_license() {
  local source=$1 target=$2
  [[ -f "${source}" ]] || { echo "Dependency license missing: ${source}" >&2; exit 1; }
  mkdir -p "$(dirname "${PACKAGE_DIR}/LICENSES/runtime-dependencies/${target}")"
  cp -f "${source}" "${PACKAGE_DIR}/LICENSES/runtime-dependencies/${target}"
}
copy_dependency_license /opt/homebrew/opt/sdl2-compat/LICENSE.txt SDL2/LICENSE.txt
copy_dependency_license /opt/homebrew/opt/sdl3/LICENSE.txt SDL3/LICENSE.txt
copy_dependency_license /opt/homebrew/opt/sdl2_ttf/LICENSE.txt SDL2_ttf/LICENSE.txt
copy_dependency_license /opt/homebrew/opt/openssl@3/LICENSE.txt OpenSSL/LICENSE.txt
copy_dependency_license /opt/homebrew/opt/freetype/LICENSE.TXT FreeType/LICENSE.TXT
copy_dependency_license "${PROJECT_ROOT}/LICENSES/FreeType/FTL.TXT" FreeType/FTL.TXT
copy_dependency_license "${PROJECT_ROOT}/LICENSES/FreeType/GPLv2.TXT" FreeType/GPLv2.TXT
copy_dependency_license /opt/homebrew/opt/libpng/LICENSE libpng/LICENSE
copy_dependency_license /opt/homebrew/opt/harfbuzz/COPYING HarfBuzz/COPYING
copy_dependency_license /opt/homebrew/opt/pcre2/COPYING PCRE2/COPYING
copy_dependency_license /opt/homebrew/opt/graphite2/COPYING Graphite2/COPYING
copy_dependency_license /opt/homebrew/opt/graphite2/LICENSE Graphite2/LICENSE
copy_dependency_license /opt/homebrew/opt/gettext/COPYING gettext/COPYING
copy_dependency_license "${PROJECT_ROOT}/runtimes/gb/third_party/libmobile/COPYING.LESSER" GLib/COPYING.LESSER
allowed_frameworks=(libSDL2-2.0.0.dylib libSDL2_ttf-2.0.0.dylib libSDL3.dylib libcrypto.3.dylib libfreetype.6.dylib libglib-2.0.0.dylib libgraphite2.3.dylib libharfbuzz.0.dylib libintl.8.dylib libpcre2-8.0.dylib libpng16.16.dylib libssl.3.dylib)
while IFS= read -r framework; do
  name="$(basename "${framework}")"; permitted=0
  for allowed in "${allowed_frameworks[@]}"; do [[ "${name}" == "${allowed}" ]] && permitted=1; done
  [[ "${permitted}" == 1 ]] || { echo "Bundled dylib lacks a reviewed license mapping: ${name}" >&2; exit 1; }
done < <(find "${FRAMEWORKS_DIR}" -maxdepth 1 -type f -name '*.dylib' | sort)
cat > "${PACKAGE_DIR}/RUNTIME_DEPENDENCIES.md" <<'EOF'
# macOS同梱dylib

| dylib | コンポーネント | ライセンス原文 |
| --- | --- | --- |
| libSDL2-2.0.0.dylib | SDL2 / sdl2-compat | LICENSES/runtime-dependencies/SDL2/LICENSE.txt |
| libSDL3.dylib | SDL3 | LICENSES/runtime-dependencies/SDL3/LICENSE.txt |
| libSDL2_ttf-2.0.0.dylib | SDL2_ttf | LICENSES/runtime-dependencies/SDL2_ttf/LICENSE.txt |
| libcrypto.3.dylib, libssl.3.dylib | OpenSSL | LICENSES/runtime-dependencies/OpenSSL/LICENSE.txt |
| libfreetype.6.dylib | FreeType | LICENSES/runtime-dependencies/FreeType/LICENSE.TXT, LICENSES/runtime-dependencies/FreeType/FTL.TXT, LICENSES/runtime-dependencies/FreeType/GPLv2.TXT |
| libpng16.16.dylib | libpng | LICENSES/runtime-dependencies/libpng/LICENSE |
| libharfbuzz.0.dylib | HarfBuzz | LICENSES/runtime-dependencies/HarfBuzz/COPYING |
| libpcre2-8.0.dylib | PCRE2 | LICENSES/runtime-dependencies/PCRE2/COPYING |
| libgraphite2.3.dylib | Graphite2 | LICENSES/runtime-dependencies/Graphite2/COPYING, LICENSE |
| libintl.8.dylib | gettext/libintl | LICENSES/runtime-dependencies/gettext/COPYING |
| libglib-2.0.0.dylib | GLib | LICENSES/runtime-dependencies/GLib/COPYING.LESSER |
| runtimes/gb/libmobile/libmobile.0.dylib | libmobile | LICENSES/third-party/libmobile-COPYING.LESSER |

N64 Runtimeの`build/deps/`にあるSDL2、SDL3、FreeType、libpngも、上記と同じ
ライセンス原文を参照します。
EOF

python3 "${PROJECT_ROOT}/scripts/verify_c_client_release_licenses.py" "${PACKAGE_DIR}" --platform macos

# xattr requires owner-write access when the user runs unlock_macos.sh after
# extracting an unnotarized release. Preserve all existing execute/read bits.
chmod -R u+w "${PACKAGE_DIR}"

echo "Removing local debug paths from product executables..."
for binary in \
  "${CLIENT_DIR}/integral_client" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime"; do
  strip -S "${binary}"
done

if [[ "${INTEGRAL_EMULATOR_RELEASE_SKIP_CODESIGN}" != "1" ]] && command -v codesign >/dev/null; then
  echo "Ad-hoc signing app bundle..."
  codesign --force --deep --sign - "${APP_DIR}" >/dev/null
fi

if grep -aR -l -F "${PROJECT_ROOT}" "${PACKAGE_DIR}" >/dev/null; then
  echo "Local build path remains in macOS artifacts." >&2
  exit 1
fi
python3 "${PROJECT_ROOT}/scripts/build_artifact_provenance.py" "${PACKAGE_DIR}" \
  --platform macos --project-root "${PROJECT_ROOT}" --write

python3 "${PROJECT_ROOT}/scripts/write_release_manifest.py" \
  "${PACKAGE_DIR}" \
  --platform macos \
  --app-name "${APP_NAME}" \
  --version "${VERSION}" \
  --minimum-os "${MACOS_DEPLOYMENT_TARGET}" \
  ${FORMAL_ARGS[@]+"${FORMAL_ARGS[@]}"}

echo "Creating zip..."
(
  cd "${RELEASE_ROOT}"
  zip -qry "${ZIP_PATH}" "${PACKAGE_NAME}"
  shasum -a 256 "$(basename "${ZIP_PATH}")" > "$(basename "${ZIP_PATH}").sha256"
)
python3 "${PROJECT_ROOT}/scripts/write_release_manifest.py" \
  "${PACKAGE_DIR}" --platform macos --app-name "${APP_NAME}" \
  --version "${VERSION}" --minimum-os "${MACOS_DEPLOYMENT_TARGET}" \
  --archive "${ZIP_PATH}" ${FORMAL_ARGS[@]+"${FORMAL_ARGS[@]}"}

echo "Release app: ${APP_DIR}"
echo "Release zip: ${ZIP_PATH}"
echo "Release checksum: ${ZIP_PATH}.sha256"
