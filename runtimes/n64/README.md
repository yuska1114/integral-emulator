# N64 Runtime

N64 Runtimeは、Mupen64Plusを基盤としてNINTENDO 64のエミュレーションを担当する、INTEGRAL EMULATORのRuntimeです。通常はINTEGRAL EMULATOR Clientから、LOCALまたはN64 ROOMセッションとして起動します。

## ディレクトリ構成

- `src/`: front-end、GUI、platform層、通信処理
- `third_party/`: vendored形式で収録した第三者ソースとupstream baseline
- `patches/`: upstreamソースへ適用するプロジェクト差分
- `tools/`: build・検証スクリプト
- `doc/`: protocol・設計文書
- `roms/`: standalone検証用の非追跡ROMディレクトリ
- `build/`: 生成物。公開ソースには含めない

## ビルド

```sh
cd runtimes/n64
make frontend
```

`make frontend`は、Mupen64Plus core、各plugin、N64 Runtime front-endをソースからビルドし、`build/integral_n64_runtime_frontend`を生成します。Mupen64Plus一式を別途ビルドする必要はありません。

通常のゲームプレイでは、INTEGRAL EMULATOR Clientが登録済みROMとセッション専用の作業パスをRuntimeへ渡します。Runtime自身はIntegral Serverへの認証や、サーバー上のSAVを確定する権限を持ちません。

キーボード操作にはSDLの物理キー位置を使用し、OSで選択中の入力方式や入力ソースは変更しません。

## macOS

Apple Silicon Macでは、Xcode Command Line ToolsとHomebrewの依存パッケージが必要です。

```sh
xcode-select --install
brew install cmake sdl2-compat sdl3 libpng freetype pkg-config
```

Command Line Toolsがすでに導入されている場合、`xcode-select --install`は不要です。その後は通常の`make frontend`でビルドできます。

macOS向けのINTEGRAL EMULATOR製品版に関する未署名・未公証の注意事項は、プロジェクトルートの`README.md`を参照してください。

## 検証

ロムを使用しない基本テストは次のコマンドで実行します。このコマンドにはN64 Runtimeのビルドも含まれるため、事前に`make frontend`を実行する必要はありません。

```sh
make test
```

remote media IPCを個別に検証する場合は、次を実行します。

```sh
make remote-media-ipc-test
```

実ROMを用いるstandalone検証は、通常のINTEGRAL EMULATOR Client経由の製品利用とは別の経路です。必要に応じて`runtimes/n64/roms/`を作成し、利用者自身が所有するROMだけを配置してください。ROM、SAV、スクリーンショット、ログはversion管理および公開ソースへ含めません。

第三者ソースに付属するREADME、LICENSE、著作権表示は、原文のまま`third_party/`以下に保持します。
