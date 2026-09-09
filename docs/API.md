# INTEGRAL EMULATOR API概要

この文書は、Integral ServerとC Client間で使用する主要エンドポイント、認証方式、
基本フローの概要です。完全なリクエスト／レスポンススキーマではありません。
詳細な契約は、現行の`src/integral_emulator/api.py`と関連テストを正本とします。

## 起動と基本経路

ソースツリーから開発用サーバーを起動する例です。

```bash
PYTHONPATH=src python3 -m integral_emulator run \
  --host 127.0.0.1 \
  --port 8080 \
  --storage-root ./storage

curl http://127.0.0.1:8080/health
```

`INTEGRAL_EMULATOR_PUBLIC_BASE_PATH`は、リバースプロキシ経由で公開する際の外部向け
パス接頭辞です。リバースプロキシは接頭辞を取り除いてIntegral Serverへ転送します。
`127.0.0.1`へ直接接続する場合は、通常この接頭辞を付けません。

サーバーのインストールと運用については
[INTEGRAL_SERVER_APPLICATION.md](INTEGRAL_SERVER_APPLICATION.md)を参照してください。

## 認証

認証関係の主要エンドポイントは次のとおりです。

- `GET /health`
- `GET /time`
- `POST /auth/register`
- `POST /auth/login`
- `POST /auth/change-password`
- `POST /auth/logout`
- `GET /me`

`POST /auth/login`は、C Clientが以後の認証に使用するBearer tokenを返します。
認証が必要なClient APIでは、`Authorization: Bearer <token>`を送信します。

`POST /auth/register`による自己登録は既定で無効です。無効時は、管理者が管理画面
または`integral-server user-issue`でユーザーを発行します。

ユーザー名の大文字・小文字は区別しません。登録時の表記は表示用として保持します。

管理画面で使用する主要な操作は次のとおりです。

- `POST /admin/users/issue`
- `POST /admin/users/reset-password`
- `GET /admin/operations`
- `POST /admin/saves/{save_id}/replace`

管理画面は`GET /admin`からログインし、Client APIのBearer tokenとは別の管理者用
Cookieで認証します。

管理者が発行した初期パスワードと仮パスワードは、発行時のレスポンス以外には
平文で保存しません。パスワード再設定時は既存の認証セッションを失効させ、
次回ログイン後のパスワード変更を要求します。

## ROMとSAV

- `GET /roms`
- `POST /roms`
- `GET /roms/{rom_id}`
- `GET /rom-slots`
- `POST /rom-slots/apply`
- `GET /saves`
- `GET /saves/{save_id}`
- `PUT /saves/{save_id}`

ROM登録時にサーバーへ送信するのは、ファイル名、hash、platform、region、
ROMヘッダータイトルなどのメタデータです。ROM本体はサーバーへ保存しません。

`game_type`はクライアントから指定できません。サーバーが次のように決定します。

- 許可カタログと照合する場合は、カタログの`game_type`
- カタログ外ROMを許可する場合は、platformと正規化したROMヘッダータイトルから生成した値

既定値の`INTEGRAL_EMULATOR_ALLOW_UNLISTED_ROMS=1`では、構造的に有効なROMを
登録できます。`0`の場合は、運用者が用意した許可カタログとの一致が必要です。

新しいROMスロットには、既定ではサーバーが初期化SAVを作成します。
ユーザーによる初期SAVの取込みは、既定では無効です。

SAVはサーバーを正本とします。更新時は所有権、サイズ、SHA-256、期待revisionを
検証します。SAVが実行中のGame Sessionに結び付いている場合は、さらに
Game Session IDとfencing tokenを検証します。

C ClientはSAV更新にrequest IDを付け、サーバーは同じ更新要求を冪等に処理します。
管理者が既存SAVを置き換える場合は、管理画面のSAV Replaceを使用します。

## ROOMとGame Session

- `POST /room-matching/create`
- `POST /room-matching/join`
- `GET /room-matching/current`
- `POST /room-matching/leave`
- `POST /rooms/{room_number}/state`
- `POST /rooms/{room_number}/chat`
- `POST /rooms/{room_number}/start`
- `POST /rooms/heartbeat`
- `POST /game/start`
- `GET /game/status`
- `POST /game/heartbeat`
- `POST /game/stop`

ROOMはサーバーが発行するルームコードで作成・参加します。参加者はROMスロット、
通信モード、READY状態を設定します。両者がREADYになると、C Clientが必要に応じて
`POST /rooms/{room_number}/start`を呼び出します。

ROOM番号として利用できる範囲は、Link Cableが`1–64`、N64が`65–128`です。
既定で有効になる範囲は、Link Cableが`1–16`、N64が`65–80`です。
有効にするROOM番号はサーバー設定で変更できます。

`POST /game/start`は、ROOMを使用しないLOCAL実行またはN64 Runtime実行の
Game Session Lock取得に使用します。

## GB Runtime fixed Host通信

ROOM開始時に発行されたfixed HostセッションIDに対して、次のエンドポイントを
使用します。

- `GET /gb-runtime-fixed-host-sessions/{session_id}/manifest`
- `POST /gb-runtime-fixed-host-sessions/{session_id}/preflight`
- `GET /gb-runtime-fixed-host-sessions/{session_id}/runtime-snapshots`
- `POST /gb-runtime-fixed-host-sessions/{session_id}/relay-ticket`
- `POST /gb-runtime-fixed-host-sessions/{session_id}/host-finish`
- `POST /gb-runtime-fixed-host-sessions/{session_id}/terminal-receipt`
- `POST /gb-runtime-fixed-host-sessions/{session_id}/cancel`

基本フローは、ROOM開始、両参加者のpreflight、HostによるSAVスナップショット取得、
両参加者のrelay ticket取得、Runtime実行、両者の終了結果確認、確定処理の順です。

relay ticketの`connection.relay_transport`は必須で、`tls`または`plain`です。
Clientは指定された方式だけを使用し、別の方式へfallbackしません。

User1が固定Hostとなり、2つのGB Runtime slotとローカルLink Cableを実行します。
Hostには、両参加者が選択したROMと同じROMヘッダータイトルのROMが必要です。

BattleではSAVを更新しません。Tradeでは、両者の候補SAV、base revision、hash、
終了結果が整合した場合だけ、両者のSAVを一組として確定します。片側だけの完了、
期限切れ、切断、hash不一致の場合は更新しません。

## Mobileセッション

- `POST /mobile-scenarios`
- `POST /mobile-sessions`
- `GET /mobile-sessions/{session_id}`
- `POST /mobile-sessions/{session_id}/heartbeat`
- `POST /mobile-sessions/{session_id}/complete`
- `POST /mobile-sessions/{session_id}/cancel`

サーバーは、導入済みのMobileパッケージからRuntime契約を作成します。
Mobileセッションは配信設定、ハートビート、Game Session Lockを管理します。

SAVの取得と更新には、LOCAL実行と同じ`GET /saves/{save_id}`と
`PUT /saves/{save_id}`を使用します。

## N64 Runtime mediaセッション

- `GET /n64-runtime-media-sessions/{session_id}`
- `GET /n64-runtime-media-sessions/{session_id}/runtime-saves/{kind}`

N64 mediaセッションは、N64 ROOMで各参加者が
`POST /rooms/{room_number}/start`を呼び出したときに作成または取得されます。
各参加者には、それぞれのGame Session Lockとrelay ticketが発行されます。
ROOM開始応答の`connection.relay_transport`は必須で、`tls`または`plain`です。

`kind`は`n64`、`host-gb`、`remote-gb`のいずれかです。
この経路で取得するSAVはRuntime用の一時データであり、N64 ROOMの実行結果は
サーバー上のSAVへ反映しません。

## データ保存

SQLiteが認証、ROOM、セッション、ROM／SAVメタデータの正本です。
SAV本体は、SQLiteの管理情報と対応するサーバーストレージに保存します。

本書は主要経路の概要に限定し、全リクエスト／レスポンススキーマは重複記載しません。
