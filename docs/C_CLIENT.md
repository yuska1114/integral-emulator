# INTEGRAL EMULATOR C Client

C Clientは、ログイン、ROM登録、LOCALプレイ、ROOM参加、GB Mobile Mode、
GB／N64 Runtimeの起動を行うデスクトップクライアントです。

ROM本体は各利用者のPCに置き、SAVはIntegral Serverを正本として管理します。

## 対応環境

初回公開でサポートするクライアント環境は次のとおりです。

- Ubuntu 24.04 LTS
- Windows 11

Windows 11ではネイティブ版C Clientを使用できます。Integral Serverを同じWindows
PCで動かす場合は、WSL2上のUbuntu 24.04 LTSを使用します。

macOS版は開発・検証用にビルドできますが、初回公開の公式サポート対象ではありません。

Linux x86-64配布バイナリのビルド環境にはUbuntu 22.04 LTSを使用して構いません。
これは古いglibcを基準にして互換性を広く保つためのビルド方針です。公開時の
サポート対象OSは上記のUbuntu 24.04 LTSのままとし、ビルド環境と実行サポート環境を
区別します。

Ubuntu 22.04 LTS標準のCMake 3.22ではlibmobileをビルドできないため、release
builderではCMake 3.25以降を別途用意します。OS baselineを24.04へ上げる理由には
せず、生成されたLinuxバイナリのglibc依存を22.04上で確認します。

## Ubuntu 24.04 LTSでのビルド

必要なパッケージを導入します。

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config nasm \
  libsdl2-dev libsdl2-ttf-dev libssl-dev \
  libfreetype6-dev libgl1-mesa-dev libglu1-mesa-dev libpng-dev zlib1g-dev \
  libsamplerate0-dev libspeexdsp-dev libvulkan-dev
```

ソースツリーのルートで、GB RuntimeとC Clientをビルドします。
SameBoyは依存関係として自動的にビルドされます。

```bash
make -C runtimes/gb/src -j"$(nproc)" server mobile-runtime
make -C c_client -j"$(nproc)"
```

N64 Modeを利用する場合は、N64 Runtimeもビルドします。
`frontend`は必要なsource stackのビルドを含みます。

```bash
make -C runtimes/n64 frontend
```

## ソース版の設定と起動

ソース版C Clientの既定Runtimeパスは、`c_client/`をカレントディレクトリとして
解決されます。次のように設定して起動します。

```bash
cd c_client
mkdir -p config
cp integral_client.conf.example config/integral_client.conf
```

`config/integral_client.conf`の`login.server`を接続先に変更してから起動します。

```bash
./build/integral_client
```

外部サーバーへ接続する場合は、パス接頭辞を含むHTTPS URLを指定してください。
同じPC上のIntegral Serverへ接続する場合は、例えば
`http://127.0.0.1:8080`を使用できます。

## Windows 11ネイティブ版の作成

MSYS2のUCRT64シェルを開き、必要なパッケージを導入します。

```bash
pacman -S --needed \
  base-devel git patch zip python \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-SDL2_ttf \
  mingw-w64-ucrt-x86_64-openssl \
  mingw-w64-ucrt-x86_64-libpng \
  mingw-w64-ucrt-x86_64-freetype \
  mingw-w64-ucrt-x86_64-glew
```

ソースツリーのルートで次のスクリプトを実行します。

```bash
bash scripts/build_c_client_release_windows_msys2.sh
```

C Client、GB Runtime、N64 Runtime、必要なDLLを含む配布用ZIPが
`dist/windows/`に作成されます。

## macOSでの開発用ビルド

SDL2、SDL2_ttf、OpenSSLなどの依存関係を用意したうえで、Ubuntuと同じMakefileを
使用できます。

開発用macOSパッケージは次のスクリプトで作成します。

```bash
bash scripts/build_c_client_release_macos.sh
```

C Client、GB Runtime、N64 Runtime、依存ライブラリ、共通README、
Gatekeeperのquarantine属性だけを解除する`unlock_macos.sh`を含む配布用ZIPが
`dist/releases/`に作成されます。現在のHomebrew依存ライブラリから作成する成果物は
macOS 26.0以降／Apple Silicon向けです。

## 接続設定

手動設定のひな形は`c_client/integral_client.conf.example`です。
利用者が事前に設定する接続項目は次の3つです。

- `login.server`: パス接頭辞を含むAPIベースURL
- `login.server_id`: `primary`または`secondary`
- `login.remember`: ログイン情報を記憶する場合は`1`

`login.remember`によるパスワード保存には、Windows Credential Managerまたは
macOS Keychainを使用します。Ubuntuでは現在利用できないため、自動的に無効になります。

パスワードとBearer tokenは設定ファイルへ保存しません。

## Runtime・フォント・TLSの設定

配布パッケージではRuntimeの場所が自動設定されるため、通常は環境変数を指定する
必要はありません。

開発時などに既定値を変更する場合は、次の環境変数を使用できます。

- `INTEGRAL_EMULATOR_CHAT_FONT`
- `INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER`
- `INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME`
- `INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA_FILE`
- `INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME`
- `INTEGRAL_EMULATOR_N64_RUNTIME_HOME`
- `INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CA_FILE`

## 操作とコントローラー

通常のClient画面はキーボードで操作します。

ゲーム実行中と`KEY CONFIG`画面では、SDLが認識するコントローラーを使用できます。
GBとN64の割当は`KEY CONFIG`画面で設定し、Client設定ファイルへ保存します。

ROOMはサーバーが発行するルームコードで作成・参加します。番号一覧から直接参加する
旧方式はありません。

## ROMとSAV

ROM登録では、ROM本体をサーバーへ送信せず、hash、ROMヘッダータイトルなどの
メタデータだけを送信します。ROMファイルは利用者のPCに残ります。

新しいROMを登録すると、通常はサーバーが初期化SAVを作成します。
サーバー管理者が初期SAV取込みを許可している場合に限り、利用者はROM登録画面で
SAVを選択し、確認後に送信できます。

サーバー上のSAVをローカルへ保存する場合は、ROM登録画面の`EXPORT`を選択します。
出力先はClientの作業ディレクトリにある`export/<日時>/`です。

## Game Sessionと一時SAV

Game Session Lockを取得したClientだけがRuntimeを起動できます。

LOCALプレイとMobile Modeでは、サーバーから取得したSAVをセッション用ディレクトリへ
一時的に書き出します。Runtime終了後、変更されたSAVをサーバーへ反映してから
一時ファイルを削除します。

通信障害などでSAVを反映できなかった場合は、利用者自身の候補SAVを
`runtime/save-outbox/`へ保護して、次回の再送に使用します。

## GB ROOM

User1のPCが固定Hostとなり、2つのGB Runtime slotとローカルLink Cableを実行します。
相手のSAVスナップショットは匿名パイプを通してRuntimeへ渡し、通常のSAVファイル、
EXPORT、回復用outboxには保存しません。

BattleではSAVを更新しません。Tradeでは、両者の終了結果が一致した場合だけ、
両者のSAVを一組としてサーバーへ反映します。

## Mobile Mode

Mobile Modeでは、サーバーが配信したパッケージをGB Runtimeへ渡します。
SAVの取得、更新、回復方法はLOCALプレイと同じで、サーバー上のSAVが正本です。

ゲーム固有の配信内容はサーバー側の追加パッケージで管理し、C Clientには組み込みません。

## N64 ROOM

User1のPCがN64 Runtimeを実行します。User2のClientは映像と音声を受信し、
コントローラー入力をUser1へ送信します。

N64 ROMとTransfer Pak用SAVは、User1のセッション用ディレクトリへ一時的に配置されます。
N64 ROOMの実行結果は、どちらの利用者のサーバーSAVにも反映しません。

## ログ

POSIX環境では、通常ログとRuntimeの子プロセス出力を所有者だけが読み書きできる
`0600`で作成します。既存ログの権限が広い場合も起動時に補正します。

WindowsではOSのACL保護を使用します。パスワード、Bearer token、ROM本体、
SAV本体はログへ記録しません。

## 検証

C Clientの共通検証は次のコマンドで実行します。

```bash
make -C c_client smoke
make -C c_client room-poll-worker-test
make -C c_client media-codec-test
make -C runtimes/gb/src key-config-controller-test
```

`smoke`には、Client設定、現行API、ROOM matching、Mobile契約、ROM解決、
ログ権限、一時ファイル処理、主要画面の検証が含まれます。

Windowsでは、次のOS固有検証も実行します。

```bash
make -C c_client media-foundation-probe
make -C c_client media-h264-test
make -C c_client n64-runtime-stop-process-test
```

## Linux／Windows配布アーカイブの作成

Linuxの入力成果物はUbuntu 22.04 LTS x86-64上で次のスクリプトにより作成します。
この段階で成果物hash、ビルド元commit、dirty状態を`BUILD_PROVENANCE.json`へ
記録し、公開バイナリからローカルcheckoutの絶対パスを除去してstripします。

```bash
bash scripts/build_c_client_release_linux.sh \
  dist/linux/INTEGRAL_EMULATOR_C_CLIENT_LINUX_BUILD
```

WindowsおよびmacOSのrelease builderも同じ形式のビルド記録を成果物内に
生成します。`SKIP_BUILD`による既存バイナリの再包装は、信頼できるビルド時点を
記録できないためrelease作成では受け付けません。

検証済みのLinuxビルドディレクトリとWindowsビルドディレクトリから、
配布用のtar.gzとZIPを同時に作成できます。

```bash
bash scripts/package_c_client_linux_windows_release.sh \
  dist/linux/INTEGRAL_EMULATOR_C_CLIENT_LINUX_YYYYMMDD \
  dist/windows/INTEGRAL_EMULATOR_C_CLIENT_WINDOWS_YYYYMMDD
```

出力先は既定で`dist/releases/`です。両パッケージの`README.txt`は
`c_client/RELEASE_README.txt`だけを原本とし、作成時に同一性を検証します。
Linuxパッケージは専用ランチャーからGB Dual Server、Fixed Host、Mobile
RuntimeおよびN64 Runtimeの絶対パスを設定し、Mobile Runtime用libmobileも
同梱します。
Windowsパッケージはランチャー、GB／N64 Runtime、DLL、TLS証明書を維持します。
公開物は明示的なallowlistから構成し、N64 reference runnerと開発用headerは
含めません。ROM、SAV、RTC、鍵、実設定、ログ、SQLite、macOSメタデータを
検出した場合は作成を拒否します。各プラットフォームに同形式の
`RELEASE_MANIFEST.json`と`SHA256SUMS`、各アーカイブの隣に`.sha256`を生成します。
正式な`dist/releases/`出力にはclean worktreeと、同一commitの検証済み正式公開
ソースZIPが必要です。条件を満たさない場合、スクリプトは自動的なfallbackや
互換コピーを作らず失敗します。レビュー用成果物が必要な場合は、代替出力先を
`dist/release-candidates/<短縮commit>/`として明示的に指定します。Linux／Windows
パッケージャーでは第3引数、macOSビルダーでは出力先overrideで指定します。
候補版のmanifestにも実際のcommitとdirty状態をそのまま記録し、
`dist/releases/`の正式版とは区別します。
このスクリプトは再ビルドを行わず、両入力の`BUILD_PROVENANCE.json`に記録された
commitが現在のclean checkoutと一致し、dirtyがfalseで、全入力hashが一致する
場合だけ配布整形します。記録欠落、改変、commit不一致、dirty入力は失敗します。

macOS成果物名も
`INTEGRAL_EMULATOR_C_CLIENT_<version>_MACOS_ARM64_<YYYYMMDD>.zip`とし、
Linux／Windowsと同じ命名規則を使用します。旧名称のaliasは作りません。

macOSでは、次のH.264検証を実行できます。

```bash
make -C c_client media-h264-test
```

N64 Runtime自体のビルドと検証については
[../runtimes/n64/README.md](../runtimes/n64/README.md)を参照してください。
