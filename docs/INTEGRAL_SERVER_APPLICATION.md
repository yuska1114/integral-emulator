# INTEGRAL EMULATOR サーバー運用手順書

この文書は、独立アプリケーション版INTEGRAL EMULATORサーバーの導入と日常運用を説明します。

サーバーはPython HTTP APIと、GB／N64 ROOM用のMedia Relayで構成されます。
インターネットへの外部公開、リバースプロキシ、ファイアウォール、ドメイン等の
構築・運用は公式サポート対象外です。

## クライアント版の照合

`integral-server.env`で設定します。既定では無効で、バージョン未送信のClientも接続できます。

```dotenv
INTEGRAL_EMULATOR_CLIENT_VERSION_CHECK_ENABLED=0
INTEGRAL_EMULATOR_ALLOWED_CLIENT_VERSIONS=0.3.0-beta
```

照合する場合は前者を`1`にし、後者へ許可する機械判定値をカンマ区切りで記載して
`sudo integral-server restart`を実行します。例：`0.2.0-beta,0.3.0-beta`。
画面表記`0.3BETA`ではなく、機械判定値`0.3.0-beta`を使用します。
大文字・小文字や接頭辞を含め完全一致で比較し、範囲指定やワイルドカードは使えません。
有効時にリストが空なら全版を拒否します。照合無効時にはリストを適用しません。

未送信・不一致はHTTP 426と`client_version_not_allowed`を返し、Clientには
`ASK SERVER ADMIN FOR SUPPORTED VERSION`を表示します。利用者には対応版を案内してください。
認証失敗・レート制限は従来どおり優先されます。既存ログインのトークンは失効させません。
自己申告値による互換性確認・更新案内であり、不正クライアントを排除する認証機構ではありません。

## 配布内容

サーバー配布物には、次のものが含まれます。

- Pythonサーバー
- 公開設定
- APIとMedia Relayのsystemdユニット
- インストーラーと運用CLI
- 本手順書

ROM、SAV、GB／N64 Runtime、TLS秘密鍵、ゲーム別GB Mobileパッケージは含まれません。

## 動作要件

公式サポート環境はUbuntu 24.04 LTSです。

Windowsでサーバーを動かす場合は、WSL2または仮想マシン上のUbuntu 24.04 LTSを
使用してください。Windowsネイティブのサーバーインストールには対応していません。

必要な環境は次のとおりです。

- Ubuntu 24.04 LTS
- Python 3.11以上
- systemd
- `sudo`権限

サーバーは専用のPython仮想環境を使用し、アプリケーション本体は
`/opt/integral-server/app`へ配置されます。

## インストール

配布アーカイブを展開し、そのディレクトリで実行します。

```bash
sudo ./install.sh
```

インストール後、診断してからサービスを起動します。

```bash
integral-server edit-config
sudo integral-server doctor
sudo integral-server start
integral-server status
curl http://127.0.0.1:8080/health
```

正常時は次の応答が返ります。

```json
{"ok": true}
```

インストーラーはサービスの自動起動を有効にしますが、初回起動は行いません。

`install.sh`を再実行しても、既存の環境設定、管理者パスワード、SQLiteデータベース、
SAVは維持されます。

本手順書は展開した配布ディレクトリ内の`docs/INTEGRAL_SERVER_APPLICATION.md`に
あります。インストール先へはコピーされません。

## 更新

全利用者のゲームと保存処理の終了を確認し、新版を別のディレクトリへ展開します。
サービスを停止し、両方が`inactive`であることを確認してください。

```bash
sudo integral-server stop
systemctl is-active integral-server.service integral-server-media-relay.service
```

停止後に設定と永続データをバックアップします。次の`YYYYMMDD-HHMM`は未使用の日時名へ
置き換えてください。独自の保存先を設定している場合は、その保存先も保全してください。

```bash
sudo install -d -m 0700 /var/backups/integral-server-YYYYMMDD-HHMM
sudo cp -a /etc/integral-server /var/backups/integral-server-YYYYMMDD-HHMM/config
sudo cp -a /var/lib/integral-server /var/backups/integral-server-YYYYMMDD-HHMM/storage
```

バックアップ成功後、新版の展開先で実行します。

```bash
sudo ./install.sh
sudo integral-server doctor
sudo integral-server start
integral-server status
systemctl is-active integral-server.service integral-server-media-relay.service
curl http://127.0.0.1:8080/health
```

両サービスの`active`とhealthの`{"ok": true}`を確認します。待受設定を変更している場合は
確認先も合わせてください。稼働中のサービスに`start`を実行するだけでは、新しいコードは読み込まれません。

## 主な配置先

- `/opt/integral-server/app`: サーバーアプリケーションとデータ定義
- `/opt/integral-server/venv`: 専用Python仮想環境
- `/etc/integral-server/integral-server.env`: サーバー環境設定
- `/var/lib/integral-server`: SQLiteデータベース、SAV等の永続データ
- `/var/lib/integral-server/mobile-packages`: GB Mobile追加パッケージ
- `/etc/systemd/system/integral-server.service`: systemdユニット
- `/etc/systemd/system/integral-server-media-relay.service`: Media Relayのsystemdユニット
- `/usr/local/bin/integral-server`: 運用CLI

## 運用コマンド

```text
sudo integral-server doctor
sudo integral-server start
sudo integral-server stop
sudo integral-server restart
integral-server status
sudo integral-server logs
sudo integral-server logs --follow
integral-server edit-config
sudo integral-server user-issue testuser001
sudo integral-server user-issue testuser002 --email user@example.com
sudo integral-server user-password-reset testuser001
sudo integral-server open-admin
sudo integral-server mobile-package list
sudo integral-server mobile-package install <パッケージ.tar.gz>
sudo integral-server uninstall
```

### サービス操作

- `doctor`: Python、設定ファイル、保存領域、管理者パスワード、ROM登録ポリシー、
  GB Mobileパッケージ、起動中のHTTPヘルスを確認します。
- `start`、`stop`、`restart`: APIとMedia Relayのsystemdサービスを操作します。
- `status`: 両サービスの状態を表示します。
- `logs`: 両サービスのログを表示します。`-n 200`で行数を指定し、`--follow`または
  `-f`で追跡できます。

`doctor`は主要な運用条件を診断しますが、すべての設定をサーバー起動と同じ経路で
検証するものではありません。設定変更後は、再起動結果、`status`、ログ、
`/health`まで確認してください。

### 設定編集

```bash
integral-server edit-config
sudo integral-server doctor
sudo integral-server restart
```

`edit-config`自体には`sudo`を付けないでください。必要な権限で設定ファイルを開き、
終了後に基本構文を確認します。サービスは自動再起動されません。

別の設定ファイルを指定する場合は、`--config`をコマンドより前に置きます。

```bash
integral-server --config /absolute/path/server.env edit-config
```

### ユーザー管理

```bash
sudo integral-server user-issue testuser001
sudo integral-server user-password-reset testuser001
```

`user-issue`はログインIDと初期パスワードを発行します。`--email`は任意です。
初回ログイン時にはパスワード変更が必要です。

ログインIDでは大文字・小文字を区別しません。

`user-password-reset`は既存ユーザーへ仮パスワードを発行し、既存の認証セッションを
無効化します。ROOM参加中またはゲーム実行中のユーザーは変更できません。

初期パスワードと仮パスワードは対話端末へ一度だけ表示されます。pipeやファイルへの
リダイレクトは使用できません。

### 管理画面

```bash
sudo integral-server open-admin
```

管理画面のURLと管理者パスワードを表示します。デスクトップ環境では既定のブラウザも
開きます。

SSH接続やGUIのない環境ではブラウザを開かず、URLとパスワードだけを表示します。

## サーバー設定

設定ファイルは次の場所にあります。

```text
/etc/integral-server/integral-server.env
```

初期設定の主な項目は次のとおりです。

```text
INTEGRAL_EMULATOR_API_HOST=127.0.0.1
INTEGRAL_EMULATOR_API_PORT=8080
INTEGRAL_EMULATOR_STORAGE_ROOT=/var/lib/integral-server
INTEGRAL_EMULATOR_PUBLIC_BASE_PATH=
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_RELOAD_INTERVAL_SECONDS=60
INTEGRAL_EMULATOR_ADMIN_PASSWORD=<自動生成された値>
INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS=1
INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION=0
INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT=0
INTEGRAL_EMULATOR_GB_LOCAL_GAME_SECONDS=172800
INTEGRAL_EMULATOR_GB_MOBILE_GAME_SECONDS=172800
INTEGRAL_EMULATOR_N64_LOCAL_GAME_SECONDS=172800
INTEGRAL_EMULATOR_LINK_CABLE_ROOM_GAME_SECONDS=3600
INTEGRAL_EMULATOR_N64_ROOM_GAME_SECONDS=7200
INTEGRAL_EMULATOR_LINK_CABLE_ROOMS=1-16
INTEGRAL_EMULATOR_N64_ROOMS=65-80
```

標準構成では`127.0.0.1:8080`だけを待ち受けます。

`INTEGRAL_EMULATOR_PUBLIC_BASE_PATH`の初期値は空です。これはリバースプロキシ等で
外部パス接頭辞を使用する場合の設定ですが、その具体的な構築は公式サポート対象外です。

管理者パスワードをログ、課題管理システム、共有資料へ貼り付けないでください。

### ユーザー自己登録

既定値は次のとおりです。

```text
INTEGRAL_EMULATOR_ALLOW_SELF_REGISTRATION=0
```

`0`では公開APIからの自己登録を拒否し、管理者が`user-issue`で発行したユーザーだけが
利用できます。

自己登録を許可する場合は`1`へ変更して、サーバーを再起動します。

### ゲーム期限

LOCALとMobileの既定期限は48時間、Link Cable ROOMは1時間、N64 ROOMは2時間です。
上記の`*_GAME_SECONDS`を正の秒数へ変更してサーバーを再起動すると、新しく開始する
ゲームへ反映されます。生存確認は短い利用権だけを更新し、ゲーム期限自体は延長しません。

### ROOM数

Link Cable ROOMには1～64、N64 ROOMには65～128を使用できます。

```text
INTEGRAL_EMULATOR_LINK_CABLE_ROOMS=1-16
INTEGRAL_EMULATOR_N64_ROOMS=65-80
```

設定値には、カンマ区切りの番号と包含範囲を指定できます。

```text
1-8,10,12-16
```

空値にすると、そのモードのROOMをすべて無効化します。範囲外、降順、重複、
書式不正はサーバー起動時に拒否されます。

稼働中のROOMを設定から外した場合も起動が拒否されます。ROOMを終了するか、
設定を戻してください。

## 永続データとバックアップ

SQLite権威データは次の場所にあります。

```text
/var/lib/integral-server/data/integral_emulator.sqlite3
```

SAV本体と回復用ファイルも`/var/lib/integral-server`配下に保存されます。

バックアップ時はサービスを停止し、設定と保存ルートを同じ時点で保存してください。
次の保存先は毎回新しい名前を使用します。

```bash
sudo integral-server stop
sudo install -d -m 0700 /var/backups/integral-server-YYYYMMDD
sudo cp -a /etc/integral-server \
  /var/backups/integral-server-YYYYMMDD/config
sudo cp -a /var/lib/integral-server \
  /var/backups/integral-server-YYYYMMDD/storage
```

コピーがエラーなく完了したことを確認し、サービスを再開します。

```bash
sudo integral-server start
integral-server status
curl --fail http://127.0.0.1:8080/health
```

`status`でAPIとMedia Relayの両サービスが稼働していることと、`/health`から
`{"ok": true}`が返ることを確認してください。

復旧時はSQLiteファイルだけでなく、対応するSAVツリーも同じバックアップ時点へ
戻してください。稼働中のSQLiteファイルだけを単独コピーしないでください。

## アンインストール

```bash
sudo integral-server uninstall
```

次のものを削除します。

- `/opt/integral-server`
- systemdユニット
- 運用CLI

次の永続データは削除しません。

- `/etc/integral-server`
- `/var/lib/integral-server`

データを削除する`purge`機能はありません。

`/opt/integral-server`内に直接追加したROMカタログはアンインストール時に削除されます。
後述のとおり、カタログ原本は`/etc/integral-server/rom-catalogs`へ保存してください。

## ネットワークモードとMedia Relay

`integral-server start`はHTTP APIとMedia Relayを起動します。ネットワークモードは
次の設定で選び、APIとMedia Relayで共通に使用します。

```text
INTEGRAL_EMULATOR_NETWORK_MODE=tls
```

指定できる値は`tls`と`plain`です。既定値は`tls`で、接続失敗時に別のモードへ
自動的に切り替わることはありません。

### 公開サーバーで使用する場合

`tls`を使用します。HTTP APIはloopbackで待ち受け、外部のHTTPSリバースプロキシから
転送してください。Media Relayは設定した証明書と秘密鍵を使って直接TLS通信します。

```text
INTEGRAL_EMULATOR_NETWORK_MODE=tls
INTEGRAL_EMULATOR_API_HOST=127.0.0.1
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_HOST=0.0.0.0
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PUBLIC_HOST=relay.example.com
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PORT=25164
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_FILE=/etc/integral-server/media-relay.crt
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_KEY_FILE=/etc/integral-server/media-relay.key
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_RELOAD_INTERVAL_SECONDS=60
```

証明書は`PUBLIC_HOST`に指定したホスト名に対して有効である必要があります。

Media Relayは指定された証明書と秘密鍵を定期的に再確認します。正常な組を読み込めた
場合だけ新規接続用のTLS Contextを交換します。接続中の通信は維持されます。証明書と
秘密鍵の不一致、形式不正、読取り失敗時は直前の正常なContextを使い続けます。
監視間隔は秒単位の正数で指定します。

Caddyが管理する証明書を使用する場合も、ファイルのコピーやACME処理はIntegral Server
では行いません。例えばCaddyの実際の保存先をそのまま指定します。

```text
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CERT_FILE=/var/lib/caddy/.local/share/caddy/certificates/acme-v02.api.letsencrypt.org-directory/example.com/example.com.crt
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_KEY_FILE=/var/lib/caddy/.local/share/caddy/certificates/acme-v02.api.letsencrypt.org-directory/example.com/example.com.key
```

実際のパスはCaddyの保存内容を確認して置き換えてください。Caddyの保存領域全体を
公開したり、`integral-server`ユーザーを`caddy`グループへ追加したりせず、親ディレクトリ
には通過権限、証明書と秘密鍵の2ファイルだけに読取り権限を付与します。付属の監視
インストーラーはPOSIX ACLを初回設定し、Caddyが対象ファイルを置換した後にも同じACLを
自動的に再付与します。既存の`/etc/integral-server`の所有者、グループ、権限、ACLは
変更せず、監視専用の`certificate-acl-watch.env`だけをroot所有・mode 0600で保存します。

```bash
sudo apt install acl
sudo ./install-certificate-acl-watch.sh \
  /var/lib/caddy/.local/share/caddy/certificates/acme-v02.api.letsencrypt.org-directory/example.com/example.com.crt \
  /var/lib/caddy/.local/share/caddy/certificates/acme-v02.api.letsencrypt.org-directory/example.com/example.com.key
sudo -u integral-server test -r /var/lib/caddy/.local/share/caddy/certificates/acme-v02.api.letsencrypt.org-directory/example.com/example.com.crt
sudo -u integral-server test -r /var/lib/caddy/.local/share/caddy/certificates/acme-v02.api.letsencrypt.org-directory/example.com/example.com.key
systemctl status integral-server-certificate-acl.path
```

監視サービスは証明書の取得、コピー、Relayの再起動を行いません。対象ファイルが
一時的に読めない場合、Media Relayは旧Contextを維持し、ACL復旧後の次回監視で新しい
証明書を読み込みます。

### 同じPCまたは信頼できるLANで使用する場合

信頼できるLAN内に限り、`plain`を明示的に選択できます。

```text
INTEGRAL_EMULATOR_NETWORK_MODE=plain
INTEGRAL_EMULATOR_API_HOST=0.0.0.0
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_HOST=0.0.0.0
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PUBLIC_HOST=192.168.1.20
INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_PORT=25164
```

同じPCだけから接続する場合は、待受先と公開先に`127.0.0.1`を使用できます。
`plain`では、パスワード、token、ROMメタデータ、SAV、映像、音声、入力が暗号化されません。
公衆Wi-Fi、共有ネットワーク、インターネットへのポート転送では使用しないでください。

設定後は、診断、再起動、状態確認を行います。

```bash
sudo integral-server doctor
sudo integral-server restart
integral-server status
```

## GB Mobile追加パッケージ

インストール直後はGB Mobileパッケージが登録されていません。

```bash
sudo integral-server mobile-package list
```

パッケージを導入する場合は、内容と出所を確認したアーカイブを指定します。

```bash
sudo integral-server mobile-package install ./mobile-package.tar.gz
```

同じパッケージIDを更新する場合だけ`--replace`を使用します。

```bash
sudo integral-server mobile-package install --replace ./mobile-package.tar.gz
```

サービスが起動中の場合、導入コマンド内でサービスが再起動されます。停止中の場合は、
次回起動時から有効になります。

導入後は次のとおり確認します。

```bash
sudo integral-server mobile-package list
sudo integral-server doctor
integral-server status
curl http://127.0.0.1:8080/health
```

ゲーム別パッケージ、その生成器、実ゲームの応答データはサーバー配布物に含まれません。
ROM、SAV、パスワード、秘密鍵を追加パッケージへ含めてはいけません。

`/var/lib/integral-server/mobile-packages`を手作業で変更しないでください。

## ROM登録ポリシー

### 制限なしモード

既定値は次のとおりです。

```text
INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS=1
```

この状態では、基本的な形式、拡張子、サイズ、hash、ROMヘッダータイトルの検証に
合格すれば、カタログ未登録のROMも登録できます。

ROM本体はサーバーへ送信されません。サーバーへ送信されるのはROMのメタデータだけです。

### 厳格カタログモード

許可カタログに一致するROMだけを登録可能にする場合は、次のように設定します。

```text
INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS=0
```

有効なカタログは次のディレクトリから読み込まれます。

```text
/opt/integral-server/app/config/allowed_roms/
```

公開配布の`default.json`は空カタログです。追加カタログがなければ、厳格モードでは
すべてのROM登録が拒否されます。

### カタログの作成

アンインストールで失わないよう、カタログ原本は`/etc/integral-server`配下へ保存します。

```bash
sudo install -d -m 0750 /etc/integral-server/rom-catalogs
sudoedit /etc/integral-server/rom-catalogs/local_roms.json
```

内容例は次のとおりです。

```json
{
  "schema_version": 1,
  "catalog_id": "local_roms",
  "catalog_role": "primary",
  "roms": [
    {
      "content_id": "my_homebrew_v1",
      "game_type": "homebrew_gb",
      "platform": "gb",
      "display_name": "My Homebrew",
      "canonical_name": "My Homebrew Version 1",
      "region": "JP",
      "size": 32768,
      "rom_header_title": "MY HOMEBREW",
      "hashes": {
        "crc32": "12345678",
        "md5": "0123456789ABCDEF0123456789ABCDEF",
        "sha1": "0123456789ABCDEF0123456789ABCDEF01234567",
        "sha256": "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
      }
    }
  ]
}
```

主な条件は次のとおりです。

- `catalog_id`と`content_id`は他のカタログと重複させない
- `catalog_role`は通常`primary`を使用する
- `platform`は`gb`または`n64`
- `game_type`は英小文字、数字、`_`、`-`を使った1～64文字の識別子
- `size`はバイト単位
- `crc32`と`md5`は必須
- `sha1`または`sha256`の少なくとも一方が必須
- `rom_header_title`は省略可能

作成した原本を、サーバーが読み込むディレクトリへ配置します。

```bash
sudo install -m 0644 \
  /etc/integral-server/rom-catalogs/local_roms.json \
  /opt/integral-server/app/config/allowed_roms/local_roms.json
```

診断後に再起動します。

```bash
sudo integral-server doctor
sudo integral-server restart
integral-server status
```

厳格モードでは、`doctor`に次の項目が表示されることを確認します。

```text
[PASS] ROM allowlist enforcement: enabled
[PASS] ROM catalogs loadable: <登録件数> entries
```

カタログを変更した場合も、原本を編集してから同じ手順でインストールし直してください。

アンインストール後にサーバーを再インストールした場合も、原本からカタログを再配置します。

### カタログの無効化

1件のROMを無効化する場合は、対象エントリへ次の項目を追加します。

```json
"enabled": false
```

カタログ全体を無効化する場合は、インストール済みファイルの拡張子を変更します。

```bash
sudo mv /opt/integral-server/app/config/allowed_roms/local_roms.json \
  /opt/integral-server/app/config/allowed_roms/local_roms.json.disabled
sudo integral-server doctor
sudo integral-server restart
```

## ユーザー初期SAV取込み

既定値は次のとおりです。

```text
INTEGRAL_EMULATOR_ALLOW_USER_INITIAL_SAVE_IMPORT=0
```

`0`では、ROMの初回登録時にユーザーからSAVを受け取らず、サーバーが初期化SAVを
作成します。

ユーザーによる初期SAV取込みを許可する場合だけ`1`へ変更し、サービスを再起動します。
取込みにはC Client上での明示的な選択と確認が必要です。

既存SAVの差し替えには管理画面のSAV Replaceを使用します。

## 障害時の確認

次の順に確認します。

```bash
sudo integral-server doctor
integral-server status
sudo integral-server logs -n 200
curl -v http://127.0.0.1:8080/health
```

主な確認点は次のとおりです。

- `doctor`に`[FAIL]`がないか
- 設定ファイルが読み取れるか
- 保存領域をサービスユーザーが読み書きできるか
- ROMカタログのJSON、識別子、hash、重複に問題がないか
- SQLiteデータベースとSAVが同じバックアップ時点か
- 8080番ポートを別のプロセスが使用していないか
- ROOM設定変更後の起動エラーがログにないか
