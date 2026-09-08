#!/bin/sh
# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repository_root=$(CDPATH= cd -- "$project_root/../.." && pwd)
version=$(sed -n '1p' "$project_root/VERSION")
architecture=$(uname -m)
release_name="N64 Runtime-$version-macos-$architecture"
release_dir="$project_root/release/$release_name"
release_zip="$project_root/release/$release_name.zip"
app="$release_dir/N64 Runtime.app"
dependency_dir="$release_dir/build/deps"
icon_source=${INTEGRAL_EMULATOR_ICON_PNG:-"$repository_root/assets/public/integral_emulator_icon.png"}
skip_build=false
no_zip=false

usage()
{
    printf 'Usage: %s [--skip-build] [--no-zip]\n' "$0"
}

need_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing command: %s\n' "$1" >&2
        exit 1
    fi
}

is_system_dependency()
{
    case "$1" in
        /System/Library/*|/usr/lib/*) return 0 ;;
        *) return 1 ;;
    esac
}

dependency_list()
{
    otool -L "$1" | awk 'NR > 1 { print $1 }' > "$release_dir/.dependencies"
}

collect_dependencies()
{
    target=$1
    identity=$(otool -D "$target" 2>/dev/null | sed -n '2p' || true)
    dependency_list "$target"
    while IFS= read -r dependency; do
        [ -n "$dependency" ] || continue
        [ "$dependency" = "$identity" ] && continue
        is_system_dependency "$dependency" && continue
        case "$dependency" in
            @*) continue ;;
        esac
        if [ ! -f "$dependency" ]; then
            printf 'Unresolved release dependency for %s: %s\n' \
                "$target" "$dependency" >&2
            exit 1
        fi
        destination="$dependency_dir/$(basename "$dependency")"
        if [ -e "$destination" ]; then
            if ! cmp -s "$dependency" "$destination"; then
                printf 'Dependency filename collision: %s\n' "$destination" >&2
                exit 1
            fi
        else
            cp "$dependency" "$destination"
            dependency_added=true
        fi
    done < "$release_dir/.dependencies"
}

rewrite_dependencies()
{
    target=$1
    loader_prefix=$2
    set_identity=$3
    identity=$(otool -D "$target" 2>/dev/null | sed -n '2p' || true)
    dependency_list "$target"
    while IFS= read -r dependency; do
        [ -n "$dependency" ] || continue
        [ "$dependency" = "$identity" ] && continue
        is_system_dependency "$dependency" && continue
        case "$dependency" in
            @*) continue ;;
        esac
        install_name_tool -change "$dependency" \
            "$loader_prefix/$(basename "$dependency")" "$target"
    done < "$release_dir/.dependencies"
    if [ "$set_identity" = true ]; then
        install_name_tool -id "@loader_path/$(basename "$target")" "$target"
    fi
    codesign --force --sign - "$target" >/dev/null
}

verify_dependencies()
{
    target=$1
    identity=$(otool -D "$target" 2>/dev/null | sed -n '2p' || true)
    dependency_list "$target"
    while IFS= read -r dependency; do
        [ -n "$dependency" ] || continue
        [ "$dependency" = "$identity" ] && continue
        case "$dependency" in
            /System/Library/*|/usr/lib/*|@loader_path/*) ;;
            *)
                printf 'Release still contains an external dependency in %s: %s\n' \
                    "$target" "$dependency" >&2
                exit 1
                ;;
        esac
    done < "$release_dir/.dependencies"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-build) skip_build=true ;;
        --no-zip) no_zip=true ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$(uname -s)" != Darwin ]; then
    printf 'This release builder currently supports macOS only.\n' >&2
    exit 1
fi
if [ -z "$version" ]; then
    printf 'VERSION is empty.\n' >&2
    exit 1
fi

for command in make cc otool install_name_tool codesign ditto awk cmp \
               python3 sdl2-config; do
    need_command "$command"
done
if [ ! -f "$icon_source" ]; then
    printf 'Application icon is missing: %s\n' "$icon_source" >&2
    exit 1
fi
python3 "$repository_root/scripts/verify_app_icon_assets.py" "$icon_source" --format png

cd "$project_root"
if [ "$skip_build" = false ]; then
    make frontend
fi

for required in \
    build/integral_n64_runtime_frontend \
    build/prefix/lib/libmupen64plus.dylib \
    build/prefix/lib/mupen64plus/mupen64plus-video-GLideN64.dylib \
    build/prefix/lib/mupen64plus/mupen64plus-audio-sdl.dylib \
    build/prefix/lib/mupen64plus/mupen64plus-input-sdl.dylib \
    build/prefix/lib/mupen64plus/mupen64plus-rsp-hle.dylib \
    build/prefix/share/mupen64plus \
    build/prefix/share/mupen64plus/GLideN64.custom.ini
do
    if [ ! -e "$required" ]; then
        printf 'Required release input is missing: %s\n' "$required" >&2
        exit 1
    fi
done

rm -rf "$release_dir" "$release_zip"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" \
    "$release_dir/build/prefix/lib/mupen64plus" \
    "$release_dir/build/prefix/share" "$dependency_dir" \
    "$release_dir/roms" "$release_dir/runtime" "$release_dir/licenses"

# Homebrew's current SDL2 package is sdl2-compat. It loads SDL3 at runtime
# rather than declaring it as a Mach-O dependency, and specifically searches
# for libSDL3.dylib beside itself. Bundle that hidden dependency explicitly.
sdl_prefix=$(sdl2-config --prefix)
sdl3_source="$sdl_prefix/opt/sdl3/lib/libSDL3.0.dylib"
if [ ! -f "$sdl3_source" ]; then
    printf 'SDL3 runtime required by sdl2-compat was not found: %s\n' \
        "$sdl3_source" >&2
    exit 1
fi
cp "$sdl3_source" "$dependency_dir/libSDL3.dylib"

cp build/integral_n64_runtime_frontend "$release_dir/build/integral_n64_runtime_frontend"
cp build/prefix/lib/libmupen64plus.dylib "$release_dir/build/prefix/lib/"
cp build/prefix/lib/mupen64plus/*.dylib \
    "$release_dir/build/prefix/lib/mupen64plus/"
cp -R build/prefix/share/mupen64plus \
    "$release_dir/build/prefix/share/mupen64plus"

for component in mupen64plus-core mupen64plus-input-sdl \
                 mupen64plus-audio-sdl mupen64plus-rsp-hle; do
    if [ -e "third_party/$component/LICENSES" ]; then
        cp -R "third_party/$component/LICENSES" \
            "$release_dir/licenses/$component-LICENSES"
    fi
done
if [ -f third_party/mupen64plus-input-sdl/COPYING ]; then
    cp third_party/mupen64plus-input-sdl/COPYING \
        "$release_dir/licenses/mupen64plus-input-sdl-COPYING.txt"
fi
if [ -f third_party/GLideN64/LICENSE ]; then
    cp third_party/GLideN64/LICENSE \
        "$release_dir/licenses/GLideN64-LICENSE.txt"
fi
cat > "$release_dir/licenses/RUNTIME_DEPENDENCIES.txt" <<'EOF'
Bundled macOS runtime libraries

sdl2-compat - Zlib License - https://github.com/libsdl-org/sdl2-compat
SDL3        - Zlib License - https://libsdl.org/
libpng      - libpng-2.0   - https://www.libpng.org/pub/png/libpng.html
FreeType    - FTL          - https://www.freetype.org/

These libraries are copied from the build machine solely to make this package
self-contained. See each upstream project for its complete license text and
corresponding source code.
EOF

cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>N64 Runtime</string>
    <key>CFBundleIdentifier</key>
    <string>local.n64_runtime.app</string>
    <key>CFBundleIconFile</key>
    <string>N64 Runtime</string>
    <key>CFBundleName</key>
    <string>N64 Runtime</string>
    <key>CFBundleDisplayName</key>
    <string>N64 Runtime</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

icon_builder="$project_root/build/tools/build_macos_icns"
mkdir -p "$(dirname "$icon_builder")"
cc -fobjc-arc -framework AppKit \
    "$project_root/tools/build_macos_icns.m" -o "$icon_builder"
"$icon_builder" "$icon_source" \
    "$app/Contents/Resources/N64 Runtime.icns"

cat > "$app/Contents/MacOS/N64 Runtime" <<'EOF'
#!/bin/sh
set -eu

app_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
package_root=$(CDPATH= cd -- "$app_root/.." && pwd)
case ${1-} in
    -psn_*) shift ;;
esac
cd "$package_root"
exec "$package_root/build/integral_n64_runtime_frontend" "$@"
EOF
chmod +x "$app/Contents/MacOS/N64 Runtime"

cat > "$release_dir/README.txt" <<EOF
N64 Runtime $version ($architecture / macOS)

N64 Runtimeは、NINTENDO 64とGB Runtime Transfer Pak連携に対応したエミュレータです。
この配布パッケージにROMやセーブデータは含まれていません。

はじめかた:
1. 正規に入手した.z64、.n64、または.v64ファイルをromsフォルダへ入れます。
2. N64 Runtime.appを開きます。
3. 通常のゲームプレイはINTEGRAL EMULATOR Clientから開始します。

セーブデータ:
- xxxxx.z64のゲーム内セーブは、roms/xxxxx/xxxxx.savへ保存されます。
- ステートセーブやメモリーパックもroms/xxxxxフォルダにまとめられます。
- romsフォルダは、この配布フォルダ内の場所から移動しないでください。

Transfer Pak:
- standalone画面では、ローカルGB/GBCカートリッジをSLOT1～4へ設定できます。
- 製品プレイではINTEGRAL EMULATOR Clientがsession専用の作業領域を渡します。

操作:
- 矢印キー: 項目移動・値変更
- Enter: 決定
- Esc: 戻る
- CONTROLLERS: N64コントローラー1～4のキー設定
- TRANSFER PAK: ローカルGB/GBCカートリッジ設定
- EMULATOR KEYS: 停止、スクリーンショット、ステート保存などの設定

エミュレータ停止キーやゲーム画面の閉じるボタンを押した場合は確認画面が出ます。
既定値はNOです。

初回起動時にmacOSが確認を表示した場合は、N64 Runtime.appをControlキーを押しながら
クリックし、「開く」を選択してください。このパッケージはDeveloper ID署名や
Appleの公証を行っていないローカル配布版です。

ライセンス情報はlicensesフォルダに収録されています。
EOF

cat > "$release_dir/roms/README.txt" <<'EOF'
正規に入手した.z64、.n64、.v64ファイルを、このフォルダへ入れてください。
ROMとセーブデータは配布パッケージに含まれていません。
EOF

{
    printf 'N64 Runtime %s source components\n\n' "$version"
    printf 'This binary was built from the N64 Runtime repository and these pinned revisions:\n'
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git submodule status
    else
        printf '\nGit metadata is intentionally absent from this source distribution.\n'
        printf 'The bundled trees are clean exports of the revisions recorded below.\n\n'
        sed -n '1,999p' third_party/README.md
    fi
    printf '\nLocal compatibility patches are listed under patches/ in the source repository.\n'
} > "$release_dir/SOURCE_COMPONENTS.txt"

dependency_added=true
while [ "$dependency_added" = true ]; do
    dependency_added=false
    collect_dependencies "$release_dir/build/integral_n64_runtime_frontend"
    collect_dependencies "$release_dir/build/prefix/lib/libmupen64plus.dylib"
    for target in "$release_dir"/build/prefix/lib/mupen64plus/*.dylib; do
        collect_dependencies "$target"
    done
    for target in "$dependency_dir"/*.dylib; do
        [ -e "$target" ] || continue
        collect_dependencies "$target"
    done
done

rewrite_dependencies "$release_dir/build/integral_n64_runtime_frontend" '@loader_path/deps' false
rewrite_dependencies "$release_dir/build/prefix/lib/libmupen64plus.dylib" \
    '@loader_path/../../deps' true
for target in "$release_dir"/build/prefix/lib/mupen64plus/*.dylib; do
    rewrite_dependencies "$target" '@loader_path/../../../deps' true
done
for target in "$dependency_dir"/*.dylib; do
    [ -e "$target" ] || continue
    rewrite_dependencies "$target" '@loader_path' true
done

verify_dependencies "$release_dir/build/integral_n64_runtime_frontend"
verify_dependencies "$release_dir/build/prefix/lib/libmupen64plus.dylib"
for target in "$release_dir"/build/prefix/lib/mupen64plus/*.dylib \
              "$dependency_dir"/*.dylib; do
    [ -e "$target" ] || continue
    verify_dependencies "$target"
done
rm -f "$release_dir/.dependencies"

codesign --force --deep --sign - "$app" >/dev/null
SDL_VIDEODRIVER=dummy "$app/Contents/MacOS/N64 Runtime" \
    --menu-smoke "$release_dir/runtime/release-smoke.bmp" \
    > "$release_dir/runtime/release-smoke.log" 2>&1
test -s "$release_dir/runtime/release-smoke.bmp"
rm -f "$release_dir/runtime/release-smoke.bmp" \
      "$release_dir/runtime/release-smoke.log"

if [ "$no_zip" = false ]; then
    ditto -c -k --sequesterRsrc --keepParent "$release_dir" "$release_zip"
fi

printf 'N64 Runtime macOS release passed dependency and launch verification.\n'
printf 'Release directory: %s\n' "$release_dir"
if [ "$no_zip" = false ]; then
    printf 'Release archive: %s\n' "$release_zip"
fi
