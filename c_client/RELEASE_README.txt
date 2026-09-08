INTEGRAL EMULATOR C CLIENT
Linux／Windows／macOS 配布版

このREADMEは、Linux版、Windows版、macOS版で共通です。

収録内容

この配布物には、INTEGRAL EMULATOR C Client、GB Runtime、N64 Runtimeが
含まれています。

ROMとSAVファイルは同梱していません。利用する権利を持っているROMだけを
使用してください。

Windows 11

1. ZIPファイルを完全に展開します。
2. 展開先のINTEGRAL_EMULATOR.exeをダブルクリックします。

必要なDLLとTLS証明書ファイルは配布物に同梱されています。

macOS（Apple Silicon）

macOS版は開発・検証用です。初回の正式サポート対象には含みません。
同梱ライブラリの要件により、macOS 26.0以降が必要です。

1. ZIPファイルを完全に展開します。
2. 展開先のINTEGRAL EMULATOR.appをダブルクリックします。

このアプリはad-hoc署名で、AppleのNotarizationは行っていません。
Gatekeeperによって起動が止められた場合は、配布物の内容を確認し、信頼できる
場合に限って、展開先フォルダで次のコマンドを実行してください。

  sh unlock_macos.sh

このスクリプトが解除するのは、展開した配布フォルダ内の
com.apple.quarantine属性だけです。Gatekeeper全体を無効化することはなく、
sudoも必要ありません。

Linux x86-64

正式サポート対象はUbuntu 24.04 LTSです。Ubuntuでは、起動前に
次のランタイムライブラリをインストールしてください。

  sudo apt update
  sudo apt install -y libsdl2-2.0-0 libsdl2-ttf-2.0-0 libssl3 ca-certificates \
    libfreetype6 libgl1 libglu1-mesa libpng16-16 zlib1g libsamplerate0 \
    libspeexdsp1 libvulkan1

tar.gzファイルを展開し、展開先フォルダをターミナルで開いて、次のコマンドを
実行します。

  ./INTEGRAL_EMULATOR.sh

接続設定

設定ファイルのひな形は次の場所にあります。

  config/integral_client.conf.example

編集用の設定ファイルは、次のコマンドで作成できます。

  Linux:
    cp config/integral_client.conf.example config/integral_client.conf

  Windows PowerShell:
    Copy-Item config\integral_client.conf.example config\integral_client.conf

  macOS:
    cp config/integral_client.conf.example config/integral_client.conf

login.serverにはIntegral Server APIのベースURLを指定します。URLにはパス接頭辞も
指定できます。login.server_idにはprimaryまたはsecondaryを指定します。
ログイン情報を記憶する場合だけ、login.rememberを1にしてください。

パスワードとBearerトークンは、この設定ファイルには保存されません。

フォルダ構成

  config/       Clientの設定ファイル
  roms/         利用者が権利を持つROMの配置先
  export/       エクスポートされたSAVファイル
  runtimes/gb/  GB Runtimeの実行ファイルと起動用リソース
  runtimes/n64/ N64 Runtimeのフロントエンド、コア、プラグイン、データ
  LICENSE       第一者コードのライセンス
  LICENSE_SCOPE.md、THIRD_PARTY_NOTICES.md  ライセンスの適用範囲と第三者告知
  LICENSES/     GNUライセンス原文と同梱コンポーネントのライセンス原文
  RUNTIME_DEPENDENCIES.md  同梱共有ライブラリとライセンスの対応表
  BUILD_PROVENANCE.json  ビルド元の出自、dirty状態、成果物hash

展開後のSHA256SUMSには、配布物内ファイルのハッシュが記録されています。
アーカイブと同じ場所にある.sha256ファイルには、tar.gzまたはZIP自体のハッシュが
記録されています。
