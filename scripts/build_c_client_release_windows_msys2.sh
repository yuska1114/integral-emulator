#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

export PATH=/ucrt64/bin:/usr/bin:/bin:$PATH

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *)
    echo "This release script is for Windows/MSYS2 only." >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
PREFIX_MAP_FLAGS="-ffile-prefix-map=${PROJECT_ROOT}=. -fdebug-prefix-map=${PROJECT_ROOT}=. -fmacro-prefix-map=${PROJECT_ROOT}=."
export INTEGRAL_RELEASE_PREFIX_MAP_FLAGS="${PREFIX_MAP_FLAGS}"
export MUPEN_OPTFLAGS="${MUPEN_OPTFLAGS:--O3} ${PREFIX_MAP_FLAGS}"
export CMAKE_C_FLAGS="${CMAKE_C_FLAGS:-} ${PREFIX_MAP_FLAGS}"
export CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS:-} ${PREFIX_MAP_FLAGS}"
INTEGRAL_EMULATOR_RELEASE_ROOT="${INTEGRAL_EMULATOR_RELEASE_ROOT:-${PROJECT_ROOT}/dist/windows}"
INTEGRAL_EMULATOR_RELEASE_APP_NAME="${INTEGRAL_EMULATOR_RELEASE_APP_NAME:-INTEGRAL EMULATOR}"
INTEGRAL_EMULATOR_RELEASE_PACKAGE_NAME="${INTEGRAL_EMULATOR_RELEASE_PACKAGE_NAME:-}"
INTEGRAL_EMULATOR_RELEASE_SKIP_BUILD="${INTEGRAL_EMULATOR_RELEASE_SKIP_BUILD:-0}"
RELEASE_ROOT="${INTEGRAL_EMULATOR_RELEASE_ROOT}"
APP_NAME="${INTEGRAL_EMULATOR_RELEASE_APP_NAME}"
VERSION="$(
  sed -n 's/^#define INTEGRAL_CLIENT_VERSION "\([^"]*\)"/\1/p' "${PROJECT_ROOT}/c_client/login_client.c" | head -n 1
)"
if [ -z "${VERSION}" ]; then
  VERSION="0.0"
fi
VERSION_TAG="$(printf '%s' "${VERSION}" | tr '[:upper:]' '[:lower:]')"
PACKAGE_NAME="${INTEGRAL_EMULATOR_RELEASE_PACKAGE_NAME:-INTEGRAL_EMULATOR_CLIENT_VER${VERSION_TAG}}"
PACKAGE_DIR="${RELEASE_ROOT}/${PACKAGE_NAME}"
ZIP_PATH="${RELEASE_ROOT}/${PACKAGE_NAME}.zip"
DLL_DIR="${PACKAGE_DIR}/dll"
CLIENT_DIR="${PACKAGE_DIR}/client"
GB_RUNTIME_DIR="${PACKAGE_DIR}/runtimes/gb"
N64_RUNTIME_DIR="${PACKAGE_DIR}/runtimes/n64"

need_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing command: $1" >&2
    exit 1
  fi
}

need_command cc
need_command make
need_command ldd
need_command windres
need_command zip
need_command python3

if [ "${INTEGRAL_EMULATOR_RELEASE_SKIP_BUILD}" != "1" ]; then
  echo "Building GB Runtime runtime binaries..."
  "${PROJECT_ROOT}/scripts/build_gb_runtime_windows_msys2.sh"

  echo "Building c_client..."
  make -C "${PROJECT_ROOT}/c_client"

  echo "Building N64 Runtime runtime binaries..."
  OS=Windows_NT make -C "${PROJECT_ROOT}/runtimes/n64"
else
  echo "Skipping builds cannot produce trustworthy build provenance." >&2
  exit 1
fi

echo "Creating release package..."
rm -rf "${PACKAGE_DIR}" "${ZIP_PATH}"
mkdir -p \
  "${CLIENT_DIR}" \
  "${GB_RUNTIME_DIR}" \
  "${GB_RUNTIME_DIR}/bootroms" \
  "${N64_RUNTIME_DIR}/build" \
  "${PACKAGE_DIR}/ssl" \
  "${PACKAGE_DIR}/LICENSES/third-party" \
  "${PACKAGE_DIR}/LICENSES/runtime-dependencies" \
  "${DLL_DIR}" \
  "${PACKAGE_DIR}/roms" \
  "${PACKAGE_DIR}/config" \
  "${PACKAGE_DIR}/export"

cp -f "${PROJECT_ROOT}/c_client/build/integral_client.exe" "${CLIENT_DIR}/integral_client.exe"
cp -f "${PROJECT_ROOT}/c_client/integral_client.conf.example" "${PACKAGE_DIR}/config/integral_client.conf.example"
cp -f "${PROJECT_ROOT}/runtimes/gb/build_exp/integral_gb_runtime_frontend.exe" "${GB_RUNTIME_DIR}/integral_gb_runtime_frontend.exe"
cp -f "${PROJECT_ROOT}/runtimes/gb/build_exp/integral_gb_runtime_dual_server.exe" "${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server.exe"
cp -f "${PROJECT_ROOT}/c_client/build/integral_gb_runtime_fixed_host.exe" "${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host.exe"
cp -f "${PROJECT_ROOT}/runtimes/gb/build_exp/integral_gb_runtime_mobile_runtime.exe" "${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime.exe"
mobile_dll="$(find "${PROJECT_ROOT}/runtimes/gb/build_exp/libmobile" -maxdepth 2 -type f -iname '*mobile*.dll' | head -n 1)"
if [ -z "${mobile_dll}" ]; then
  echo "Missing libmobile DLL" >&2
  exit 1
fi
cp -f "${mobile_dll}" "${DLL_DIR}/"
cp -f "${PROJECT_ROOT}/runtimes/gb/assets/bootroms/sgb2_boot.bin" "${GB_RUNTIME_DIR}/bootroms/sgb2_boot.bin"
cp -f "${PROJECT_ROOT}/runtimes/n64/build/integral_n64_runtime_frontend.exe" "${N64_RUNTIME_DIR}/build/integral_n64_runtime_frontend.exe"
mkdir -p "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus" "${N64_RUNTIME_DIR}/build/prefix/share/mupen64plus"
cp -f "${PROJECT_ROOT}/runtimes/n64/build/prefix/lib/mupen64plus.dll" "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus.dll"
for plugin in mupen64plus-audio-sdl.dll mupen64plus-input-sdl.dll mupen64plus-rsp-hle.dll mupen64plus-video-GLideN64.dll; do
  cp -f "${PROJECT_ROOT}/runtimes/n64/build/prefix/lib/mupen64plus/${plugin}" "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus/${plugin}"
done
for data in font.ttf GLideN64.custom.ini InputAutoCfg.ini mupen64plus.ini mupencheat.txt; do
  cp -f "${PROJECT_ROOT}/runtimes/n64/build/prefix/share/mupen64plus/${data}" "${N64_RUNTIME_DIR}/build/prefix/share/mupen64plus/${data}"
done
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
if [ -f /ucrt64/etc/ssl/cert.pem ]; then
  cp -f /ucrt64/etc/ssl/cert.pem "${PACKAGE_DIR}/ssl/cert.pem"
fi

echo "Building Windows launcher..."
windres "${PROJECT_ROOT}/c_client/windows_launcher.rc" -O coff \
  -o "${PROJECT_ROOT}/c_client/build/integral_launcher.res.o"
cc -O2 -Wall -Wextra -Werror -mwindows \
  ${PREFIX_MAP_FLAGS} \
  "${PROJECT_ROOT}/c_client/windows_launcher.c" \
  "${PROJECT_ROOT}/c_client/build/integral_launcher.res.o" \
  -o "${PACKAGE_DIR}/${APP_NAME}.exe"

cp -f "${PROJECT_ROOT}/c_client/RELEASE_README.txt" "${PACKAGE_DIR}/README.txt"
cp -f "${PROJECT_ROOT}/LICENSE" "${PACKAGE_DIR}/LICENSE"
cp -f "${PROJECT_ROOT}/c_client/BINARY_LICENSE_SCOPE.md" "${PACKAGE_DIR}/LICENSE_SCOPE.md"
cp -f "${PROJECT_ROOT}/c_client/BINARY_THIRD_PARTY_NOTICES.md" "${PACKAGE_DIR}/THIRD_PARTY_NOTICES.md"
mkdir -p "${PACKAGE_DIR}/LICENSES"
for license in AGPL-3.0-or-later GPL-2.0-or-later GPL-3.0-or-later LGPL-3.0-or-later; do
  cp -f "${PROJECT_ROOT}/LICENSES/${license}.txt" "${PACKAGE_DIR}/LICENSES/${license}.txt"
done

copy_runtime_dlls_for() {
  local bin="$1"
  ldd "${bin}" 2>/dev/null |
    awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\// && tolower($i) ~ /\.dll$/) print $i }' |
    while IFS= read -r dll; do
      case "${dll}" in
        /c/Windows/*|/C/Windows/*|/c/windows/*|/C/windows/*|/c/WINDOWS/*|/C/WINDOWS/*)
          continue
          ;;
      esac
      if [ -f "${dll}" ]; then
        cp -f "${dll}" "${DLL_DIR}/$(basename "${dll}")"
      fi
    done
}

for bin in \
  "${PACKAGE_DIR}/${APP_NAME}.exe" \
  "${CLIENT_DIR}/integral_client.exe" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_frontend.exe" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_dual_server.exe" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_fixed_host.exe" \
  "${GB_RUNTIME_DIR}/integral_gb_runtime_mobile_runtime.exe" \
  "${N64_RUNTIME_DIR}/build/integral_n64_runtime_frontend.exe" \
  "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus.dll" \
  "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus/"*.dll; do
  copy_runtime_dlls_for "${bin}"
done

allowed_dlls=(SDL2.dll SDL2_ttf.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll libcrypto-3-x64.dll libfreetype-6.dll libgcc_s_seh-1.dll libglib-2.0-0.dll libgraphite2.dll libharfbuzz-0.dll libiconv-2.dll libintl-8.dll libmobile.dll libpcre2-8-0.dll libpng16-16.dll libssl-3-x64.dll libstdc++-6.dll libwinpthread-1.dll zlib1.dll)
for present in "${DLL_DIR}"/*.dll; do
  name=$(basename "${present}"); permitted=0
  for allowed in "${allowed_dlls[@]}"; do [ "${name}" = "${allowed}" ] && permitted=1; done
  [ "${permitted}" = 1 ] || { echo "Unexpected DLL outside release allowlist: ${name}" >&2; exit 1; }
done
for allowed in "${allowed_dlls[@]}"; do [ -f "${DLL_DIR}/${allowed}" ] || { echo "Required DLL missing: ${allowed}" >&2; exit 1; }; done

license_files=(
  brotli/LICENSE bzip2/LICENSE freetype/FTL.TXT freetype/GPLv2.TXT
  gcc-libs/COPYING.LIB gcc-libs/COPYING.RUNTIME gcc-libs/COPYING3 gcc-libs/README
  gettext-runtime/COPYING glib2/COPYING graphite2/COPYING graphite2/LICENSE
  harfbuzz/COPYING libiconv/COPYING libiconv/COPYING.LIB libiconv/README
  libpng/LICENSE libwinpthread/COPYING openssl/LICENSE pcre2/COPYING pcre2/LICENCE.md
  SDL2/LICENSE.txt SDL2_ttf/LICENSE.txt zlib/LICENSE
)
: > "${PACKAGE_DIR}/RUNTIME_DEPENDENCY_LICENSE_FILES.txt"
for relative in "${license_files[@]}"; do
  source_path="/ucrt64/share/licenses/${relative}"
  [ -f "${source_path}" ] || { echo "Runtime dependency license missing: ${source_path}" >&2; exit 1; }
  mkdir -p "${PACKAGE_DIR}/LICENSES/runtime-dependencies/$(dirname "${relative}")"
  cp -f "${source_path}" "${PACKAGE_DIR}/LICENSES/runtime-dependencies/${relative}"
  printf '%s\n' "${relative}" >> "${PACKAGE_DIR}/RUNTIME_DEPENDENCY_LICENSE_FILES.txt"
done
cp -f "${PROJECT_ROOT}/runtimes/gb/third_party/libmobile/COPYING.LESSER" "${PACKAGE_DIR}/LICENSES/runtime-dependencies/libmobile-COPYING.LESSER"
printf '%s\n' 'libmobile-COPYING.LESSER' >> "${PACKAGE_DIR}/RUNTIME_DEPENDENCY_LICENSE_FILES.txt"
cat > "${PACKAGE_DIR}/RUNTIME_DEPENDENCIES.md" <<'EOF'
# Windows同梱DLL

`dll/`のDLLは次のコンポーネントに対応します。ライセンス原文は
`LICENSES/runtime-dependencies/`以下の同名ディレクトリに収録しています。

| DLL | コンポーネント | ライセンス原文 |
| --- | --- | --- |
| SDL2.dll | SDL2 | SDL2/LICENSE.txt |
| SDL2_ttf.dll | SDL2_ttf | SDL2_ttf/LICENSE.txt |
| libbrotlicommon.dll, libbrotlidec.dll | Brotli | brotli/LICENSE |
| libbz2-1.dll | bzip2 | bzip2/LICENSE |
| libcrypto-3-x64.dll, libssl-3-x64.dll | OpenSSL | openssl/LICENSE |
| libfreetype-6.dll | FreeType | freetype/FTL.TXT, freetype/GPLv2.TXT |
| libgcc_s_seh-1.dll, libstdc++-6.dll | GCC runtime | gcc-libs/COPYING.LIB, COPYING.RUNTIME, COPYING3, README |
| libglib-2.0-0.dll | GLib | glib2/COPYING |
| libgraphite2.dll | Graphite2 | graphite2/COPYING, graphite2/LICENSE |
| libharfbuzz-0.dll | HarfBuzz | harfbuzz/COPYING |
| libiconv-2.dll | libiconv | libiconv/COPYING, COPYING.LIB, README |
| libintl-8.dll | gettext-runtime | gettext-runtime/COPYING |
| libmobile.dll | libmobile | libmobile-COPYING.LESSER |
| libpcre2-8-0.dll | PCRE2 | pcre2/COPYING, pcre2/LICENCE.md |
| libpng16-16.dll | libpng | libpng/LICENSE |
| libwinpthread-1.dll | winpthreads | libwinpthread/COPYING |
| zlib1.dll | zlib | zlib/LICENSE |
EOF

echo "Verifying runtime dependencies..."
export PATH="${DLL_DIR}:${PATH}"
for bin in \
  "${PACKAGE_DIR}/${APP_NAME}.exe" \
  "${CLIENT_DIR}/"*.exe \
  "${GB_RUNTIME_DIR}/"*.exe \
  "${N64_RUNTIME_DIR}/build/"*.exe \
  "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus.dll" \
  "${N64_RUNTIME_DIR}/build/prefix/lib/mupen64plus/"*.dll; do
  if ldd "${bin}" 2>/dev/null | grep -qi 'not found'; then
    echo "Missing DLL dependency for ${bin}" >&2
    ldd "${bin}" >&2 || true
    exit 1
  fi
done

WINDOWS_PROJECT_ROOT="$(cygpath -w "${PROJECT_ROOT}")"
if grep -aR -l -F "${PROJECT_ROOT}" "${PACKAGE_DIR}" >/dev/null || \
   grep -aR -l -F "${WINDOWS_PROJECT_ROOT}" "${PACKAGE_DIR}" >/dev/null; then
  echo "Local build path remains in Windows artifacts." >&2
  exit 1
fi
python3 "${PROJECT_ROOT}/scripts/verify_c_client_release_licenses.py" "${PACKAGE_DIR}" --platform windows
python3 "${PROJECT_ROOT}/scripts/build_artifact_provenance.py" "${PACKAGE_DIR}" \
  --platform windows --project-root "${PROJECT_ROOT}" --write

python3 "${PROJECT_ROOT}/scripts/write_release_manifest.py" \
  "${PACKAGE_DIR}" \
  --platform windows \
  --version "${VERSION}"

echo "Creating zip..."
(
  cd "${RELEASE_ROOT}"
  zip -qry "${ZIP_PATH}" "${PACKAGE_NAME}"
  sha256sum "$(basename "${ZIP_PATH}")" > "$(basename "${ZIP_PATH}").sha256"
)
python3 "${PROJECT_ROOT}/scripts/write_release_manifest.py" \
  "${PACKAGE_DIR}" --platform windows --version "${VERSION}" --archive "${ZIP_PATH}"

echo "Release folder: ${PACKAGE_DIR}"
echo "Release zip: ${ZIP_PATH}"
echo "Release checksum: ${ZIP_PATH}.sha256"
