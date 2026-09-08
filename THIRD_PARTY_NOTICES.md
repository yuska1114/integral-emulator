# 第三者コードについて

Integral Emulatorの公開ソースには、第三者由来のコードが含まれています。

## 収録コンポーネント

| コンポーネント | 収録場所 | revision・整合性記録 | ライセンス原文 |
| --- | --- | --- | --- |
| SameBoy | `runtimes/gb/third_party/SameBoy/` | `THIRD_PARTY_LOCK.json` | `runtimes/gb/third_party/SameBoy/LICENSE` |
| libmobile | `runtimes/gb/third_party/libmobile/` | `runtimes/gb/third_party/libmobile/INTEGRAL_REVISION.txt`、`scripts/verify_libmobile_dependency.py` | `runtimes/gb/third_party/libmobile/COPYING`、`runtimes/gb/third_party/libmobile/COPYING.LESSER` |
| mupen64plus-core | `runtimes/n64/third_party/mupen64plus-core/` | `THIRD_PARTY_LOCK.json` | `LICENSES` |
| mupen64plus-ui-console | `runtimes/n64/third_party/mupen64plus-ui-console/` | `THIRD_PARTY_LOCK.json` | `LICENSES` |
| mupen64plus-input-sdl | `runtimes/n64/third_party/mupen64plus-input-sdl/` | `THIRD_PARTY_LOCK.json` | `COPYING`、`LICENSES` |
| mupen64plus-audio-sdl | `runtimes/n64/third_party/mupen64plus-audio-sdl/` | `THIRD_PARTY_LOCK.json` | `LICENSES` |
| mupen64plus-rsp-hle | `runtimes/n64/third_party/mupen64plus-rsp-hle/` | `THIRD_PARTY_LOCK.json` | `LICENSES` |
| GLideN64 | `runtimes/n64/third_party/GLideN64/` | `THIRD_PARTY_LOCK.json` | `LICENSE` |

`THIRD_PARTY_LOCK.json`は、対象コンポーネントのupstream baseline、収録範囲、hash、patchを記録します。libmobileは同ディレクトリの`INTEGRAL_REVISION.txt`でrevisionを記録し、専用スクリプトでsource treeを検証します。

第三者コードには、それぞれの上流プロジェクトのライセンスと著作権表示が適用されます。各third-partyディレクトリに収録されたライセンス、README、ファイルヘッダー等の原文を参照してください。本書の説明と原文が異なる場合は、原文が優先します。

## C Clientバイナリ配布物

C Client配布物では、第一者コードの条件をトップレベルの`LICENSE`と
`LICENSE_SCOPE.md`に、GNUライセンス原文を`LICENSES/`直下に収録します。
SameBoy、libmobile、N64 core・4 pluginの上流ライセンス原文は
`LICENSES/third-party/`、同梱DLL・dylib・共有ライブラリの原文は
`LICENSES/runtime-dependencies/`に収録します。個々の実ファイル名、
コンポーネント名、ライセンス、原文の対応は`RUNTIME_DEPENDENCIES.md`に記載します。
