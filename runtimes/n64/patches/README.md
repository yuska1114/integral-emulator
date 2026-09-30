# N64 Runtimeのupstream patch

このディレクトリには、N64 Runtimeのビルド時にvendored upstreamソースへ適用するプロジェクト差分と、vendored GLideN64ソースに反映済みの差分を収録しています。ビルド時の差分は`tools/build_source_stack.sh`が作業用コピーへ適用します。

各patchは対応するバイナリのソースの一部であるため、公開ソースへ含めます。対象baseline、適用順、SHA-256、licenseの正式な記録は、プロジェクトルートの`THIRD_PARTY_LOCK.json`を参照してください。

## mupen64plus-core

- `mupen64plus-core-physical-hotkeys.patch`: 物理scancode、操作hotkey、保存名、終了確認
- `mupen64plus-core-deferred-stop-confirmation.patch`: SDL event処理外での終了確認
- `mupen64plus-core-transferpak-mbc3-rtc-sidecar.patch`: Transfer Pak用MBC3 RTC sidecar
- `mupen64plus-core-homebrew-transferpak.patch`: media loader使用時のTransfer Pak有効化
- `mupen64plus-core-current-rdram.patch`: Mupen64Plus上流RDRAM修正のpartial backport。著作者・元commitはルートの`THIRD_PARTY_NOTICES.md`を参照
- `mupen64plus-core-startup-focus.patch`: 共通SDL処理によるゲーム開始時1回だけの前面化

- `mupen64plus-core-util-keys.patch`: 設定STOPの確認、共通の撮影名、撮影結果通知
- `mupen64plus-core-transferpak-memory.patch`: ROOM専用のGB RAM・RTCメモリ保存層（LOCALのファイル保存は維持）
- `mupen64plus-core-integral-change-notices.patch`: 上記core patchの適用後、変更対象15ファイル内へ日付・内容・patch名を表示

## mupen64plus-input-sdl

- `mupen64plus-input-sdl-physical-scancode.patch`: 物理scancodeと入力のないTransfer Pak slot
- `mupen64plus-input-sdl-remote-controller2.patch`: N64 ROOMのController 2入力
- `mupen64plus-input-sdl-integral-change-notices.patch`: 変更対象2ファイル内への変更表示

## mupen64plus-audio-sdl

- `mupen64plus-audio-sdl-remote-media.patch`: N64 ROOM用PCM出力
- `mupen64plus-audio-sdl-integral-change-notices.patch`: 変更対象1ファイル内への変更表示

## GLideN64

- `gliden64-integral-game-viewport.patch`: ゲーム表示領域の取得に必要な反映済み差分
- `gliden64-integral-change-notices.patch`: 反映済み差分の変更対象8ファイル内への変更表示

## 検証

プロジェクトルートで次を実行すると、vendoredソースとpatchの整合性を確認できます。

```sh
python3 scripts/verify_third_party_lock.py
```

通常のN64 Runtimeビルドでは、`make frontend`が必要なpatchを適用します。upstream付属のREADME、LICENSE、著作権表示は変更しません。
変更表示patchは各コンポーネントの最後に適用されます。各変更対象ファイルの冒頭に`Integral Emulator`、表示追加日、該当patchの元の変更日・名前・概要を残します。新しいpatchまたは変更対象ファイルを追加した際の表示漏れは`tests/test_third_party_lock.py`で検出します。
