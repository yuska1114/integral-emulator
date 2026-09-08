# N64 Runtimeのupstream patch

このディレクトリには、N64 Runtimeのビルド時にvendored upstreamソースへ適用するプロジェクト差分を収録しています。`third_party/`以下のupstreamソースは直接変更せず、`tools/build_source_stack.sh`が作業用コピーへpatchを適用してからビルドします。

各patchは対応するバイナリのソースの一部であるため、公開ソースへ含めます。対象baseline、適用順、SHA-256、licenseの正式な記録は、プロジェクトルートの`THIRD_PARTY_LOCK.json`を参照してください。

## mupen64plus-core

- `mupen64plus-core-physical-hotkeys.patch`: 物理scancode、操作hotkey、保存名、終了確認
- `mupen64plus-core-deferred-stop-confirmation.patch`: SDL event処理外での終了確認
- `mupen64plus-core-transferpak-mbc3-rtc-sidecar.patch`: Transfer Pak用MBC3 RTC sidecar
- `mupen64plus-core-homebrew-transferpak.patch`: media loader使用時のTransfer Pak有効化
- `mupen64plus-core-current-rdram.patch`: 現行RDRAM処理のbackport

## mupen64plus-input-sdl

- `mupen64plus-input-sdl-physical-scancode.patch`: 物理scancodeと入力のないTransfer Pak slot
- `mupen64plus-input-sdl-remote-controller2.patch`: N64 ROOMのController 2入力

## mupen64plus-audio-sdl

- `mupen64plus-audio-sdl-remote-media.patch`: N64 ROOM用PCM出力

## 検証

プロジェクトルートで次を実行すると、vendoredソースとpatchの整合性を確認できます。

```sh
python3 scripts/verify_third_party_lock.py
```

通常のN64 Runtimeビルドでは、`make frontend`が必要なpatchを適用します。upstream付属のREADME、LICENSE、著作権表示は変更しません。
