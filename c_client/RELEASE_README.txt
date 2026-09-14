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

実行ファイルだけを別の場所へ移動せず、展開したフォルダ構成のまま使用してください。

このWindows版にはコード署名を行っていません。Microsoft Defender SmartScreenなどに
より、発行元を確認できない旨の警告が表示される場合があります。公開元とZIPファイルの
SHA-256を確認し、信頼できる場合にのみ実行してください。

macOS（Apple Silicon）

macOS版は開発・検証用です。正式サポート対象には含みません。
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

公開サーバーにはHTTPSで接続してください。HTTPは、サーバー管理者が平文モードを
選択した同じPCまたは信頼できるLANでのみ使用してください。HTTPではログイン情報、
トークン、ROMメタデータ、SAVが暗号化されません。

パスワードとBearerトークンは、この設定ファイルには保存されません。
Linuxでログイン情報の記憶を有効にすると、パスワードは平文で
~/.config/integral-emulator/credentials/以下へ保存されます
（XDG_CONFIG_HOME設定時はその配下）。保存先ディレクトリは0700、
接続先・ユーザー別の保存ファイルは0600です。記憶を無効にすると削除されます。

初めて遊ぶまで

メニュー画面はキーボードで操作します。基本は上下キーまたはTabで項目を移動し、
Enterで決定、Escで戻ります。画面下にも操作方法が表示されます。

1. 接続先とアカウントを用意します。
   サーバー管理者から接続先URL、ユーザー名、パスワードを受け取り、前述の
   「接続設定」を確認してください。アカウントの自己登録は標準では無効です。
   ENV（primary／secondary）の選択は、管理者の案内に合わせてください。

2. ROMファイルを配置します。
   利用する権利を持つROMファイルを、配布物の展開先にあるroms/へ置いてください。

3. Clientを起動してログインします。
   ACCOUNT LOGIN画面でSERVERとENVを確認し、USERNAMEとPASSWORDを入力します。
   入力欄を選んでF2を押すと編集できます。LOGINを選んでEnterを押してください。
   パスワード変更を求められた場合は、画面の案内に従って変更します。

4. ROMを登録します。
   MAIN MENUのROM REGISTERを開き、空いているROM1～ROM8の枠を選びます。
   F4でroms/内の一覧を開き、ROMを選んでEnterを押します。その後、REGISTERの行へ
   移動してEnterを押し、登録完了を確認してください。複数のROMは1本ずつ登録します。
   ファイルを選んだだけで戻ると、登録は確定しません。

   新規登録では通常、新しいSAVをサーバーが作成します。手持ちのSAVから始めたい
   場合は、登録前に管理者へ取込みが許可されているか確認してください。
   ROM本体はサーバーへ送信されません。登録後もroms/に置いたまま使用します。

5. GBのゲームを開始します。
   必要に応じてMAIN MENUのKEY CONFIGで、キーやコントローラーを設定します。
   LOCAL → GB MODEを開き、SLOT1の行で左右キーを押して登録済みのROMを選びます。
   1画面で遊ぶ場合はSLOT2を空にします。設定済みならSLOT2の行でBackspaceを押すと
   解除できます。STARTを選んでEnterを押すとゲームが起動します。

ROM1～ROM8は登録用の枠、SLOT1／SLOT2は今回遊ぶゲームを選ぶ欄です。
2画面で遊ぶ場合はSLOT2にもROMを選びます。同じROMを使う場合も、別々の登録枠と
SAVが必要です。保存と終了の方法は、後述の「セーブデータの保存とゲームの終了」を
確認してください。

ほかのモードを使う場合

- N64：ROM REGISTERでN64 ROMを登録し、LOCAL → N64 MODEのN64 SLOTで選びます。
  必要に応じてSLOT1～SLOT4にTransfer Pak用のGB ROMを選び、STARTで開始します。
- ROOM：作成側はCREATE ROOMでLINK CABLE ROOMまたはN64 ROOMを選び、表示された
  5桁のコードを相手へ伝えます。参加側はJOIN ROOMでコードを入力します。
  入室後、各自のROMなど必要な項目を設定し、両者がREADYにします。
- Mobile：LOCAL → MOBILE MODEでSLOT1とSCENARIOを選び、STARTで開始します。
  対象ROMに対応した追加パッケージを、サーバー管理者が導入している必要があります。
  Clientの導入だけでは利用できません。SCENARIOがNOT AVAILABLEの場合は、ROMの
  選択とサーバー側の対応状況を管理者へ確認してください。

詳しい説明は、同じバージョンの公開ソースZIPを展開して確認できます。

  docs/C_CLIENT.md                     Clientの設定、操作、保存、各モードの説明
  docs/INTEGRAL_SERVER_APPLICATION.md  サーバーの導入、ユーザー発行、Mobile追加パッケージ

ウィンドウ表示

ClientとGBのゲーム画面は、ウィンドウの端をドラッグして大きさを変更できます。
GBのゲーム画面は起動時のClientと同じウィンドウサイズになり、映像を整数倍率で
中央表示して余白を黒くします。Clientの終了時のサイズは次回起動時に復元されます。

RTC搭載ROMのSAVが通信に必要なRTC情報を含まない場合は、LOCALで一度ゲームを起動し、
正常に終了してから再度通信を開始してください。

セーブデータの保存とゲームの終了

セーブデータ（SAV）はサーバーで管理します。LOCALでもサーバーへの接続が必要です。
終了操作だけでゲーム内のセーブが実行されるわけではありません。ゲームに保存操作が
ある場合は、先にゲーム内で保存してください。

  モード                         | サーバーSAVへの反映
  -------------------------------|--------------------------------------------
  GB LOCAL（1画面／2画面）       | あり。実行中と終了後にSAVの変更を同期します。
  N64 LOCAL                      | あり。N64のSAVと、Transfer Pakに選んだGB SAV。
  Mobile Mode                    | 正常終了し、SAVの検証に成功した場合だけ反映。
  Link Cable ROOM：Battle        | なし。対戦結果によってSAVは更新されません。
  Link Cable ROOM：Trade         | Hostの正常終了後、両者の結果が一致した場合だけ
                                 | 両者のSAVを一組として反映します。
  N64 ROOM                       | なし。両者ともサーバーSAVは更新されません。

N64 LOCALのコントローラーパックのデータは、サーバー同期の対象外です。

終了するときは、ゲーム画面で終了キー（初期設定はEsc）またはウィンドウの閉じる
ボタンを使い、終了確認でYESを選んでください。

Link Cable ROOMのTradeでは、Host（User1）が終了操作を行い、Remote（User2）は
完了するまで接続を保って待ってください。Remoteが自分で終了すると、両者のSAVは
更新されません。ゲーム内で交換が成功していても、この終了手順が必要です。

Client本体は、ゲームと保存処理が終わるまで開いたままにしてください。
通信障害、ゲーム期限切れ、強制終了の場合は、最新の状態がサーバーへ反映されない
ことがあります。

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
  BUILD_PROVENANCE.json  ビルド情報

展開後のSHA256SUMSには、配布物内ファイルのハッシュが記録されています。
アーカイブと同じ場所にある.sha256ファイルには、tar.gzまたはZIP自体のハッシュが
記録されています。
