# INTEGRAL EMULATOR

INTEGRAL EMULATORは、オープンソースのGame Boyエミュレータです。通信ケーブルによるGame Boy同士の通信、NINTENDO 64との接続、モバイルアダプタGBとの連携が特徴です。また、セーブデータはサーバーで管理する設計になっています。

本エミュレータは、主に、デスクトップクライアント、Integral Server、GB Runtime、N64 Runtimeから構成されます。

本リポジトリにはゲームROMやSAVデータは含まれていません。

## 主な構成

| ディレクトリ | 内容 |
| --- | --- |
| `src/integral_emulator/` | Integral Serverおよび通信データ配信基盤 |
| `c_client/` | デスクトップクライアント |
| `runtimes/gb/` | GB Runtime |
| `runtimes/n64/` | N64 Runtime |
| `deploy/` | Integral Serverの導入・サービス設定 |
| `docs/` | 仕様・技術文書 |
| `scripts/` | ビルド・検証等の補助ツール |

## 対応環境

- C Client: Ubuntu 24.04 LTS、Windows 11
- Integral Server: Ubuntu 24.04 LTS
- macOS: 開発・検証用ビルドのみ

Windowsでサーバーを動かす場合は、WSL2または仮想マシン上のUbuntu 24.04 LTSを使用します。Windowsネイティブのサーバーインストールはサポート対象外です。

## Integral Serverの導入

Ubuntu 24.04 LTSで、ソースツリーのルートから次を実行します。

```bash
sudo ./install.sh
integral-server edit-config
sudo integral-server doctor
sudo integral-server start
curl http://127.0.0.1:8080/health
```

同じPCまたは信頼できるLANで、TLS証明書を用意せずに利用する場合は、初回起動前にネットワークモードをplainへ変更してください（[サーバー運用](docs/INTEGRAL_SERVER_APPLICATION.md)）。
正常に起動していれば、`/health`から`{"ok": true}`が返ります。

設定、ユーザー発行、バックアップ等は[サーバー運用](docs/INTEGRAL_SERVER_APPLICATION.md)を参照してください。C ClientとRuntimeのビルドは[C Client](docs/C_CLIENT.md)、[GB Runtime](runtimes/gb/README.md)、[N64 Runtime](runtimes/n64/README.md)を参照してください。

## 開発用macOS配布の注意

macOS配布版のRuntimeは、`.app/Contents/Resources/runtimes/`内にあります。

開発・検証用のmacOS版は、Developer ID署名およびApple Notarizationを行っていません。ダウンロードしたreleaseには、macOSのquarantine属性が付与される場合があります。

Client、GB Runtime、N64 Runtime、helper binaryを個別にGatekeeperで許可する代わりに、releaseフォルダ直下の`unlock_macos.sh`を使って、そのフォルダ配下の`com.apple.quarantine`属性だけを一括解除できます。Terminalでreleaseフォルダを開き、内容を確認して信頼できる場合に限り、ユーザー自身で次を実行してください。管理者権限は不要です。

```sh
sh unlock_macos.sh
```

この操作の対象は、`unlock_macos.sh`が置かれているreleaseフォルダだけです。Gatekeeper全体を無効化するものではなく、quarantine以外の拡張属性も削除しません。

## テスト

配布アーカイブとともに提供する外側の`SHA256SUMS`はアーカイブ自体、
展開後の`SHA256SUMS`は配布物内ファイルの確認に使います。

PythonテストにはC Clientとの統合試験が含まれます。[C Clientのビルド依存関係](docs/C_CLIENT.md)を導入し、ROOM試験バイナリを作成してから実行してください。

```bash
make -C c_client room-state-test
PYTHONPATH=src python3 -m unittest discover -s tests
python3 scripts/verify_third_party_lock.py
python3 scripts/verify_libmobile_dependency.py runtimes/gb/third_party/libmobile
```

個別コンポーネントのビルド・検証方法は各文書を参照してください。

公開ソースZIPは、展開前に同梱manifestと内容が一致するかを単独で検証できます。

```bash
python3 scripts/public_source_integrity.py --archive <公開ソースZIP>
```

展開後は、ソースツリーのルートで次を実行します。ビルド生成物は許可されますが、
収録ソースの改変・欠落や想定外のファイルは拒否されます。

```bash
python3 scripts/public_source_integrity.py --directory .
```

この検証はファイル内容の一致を確認するものであり、配布元の真正性を保証するものではありません。

## ドキュメント

サーバー配布物を再作成する場合は、検証済みの公開ソースZIPを展開し、そのルートで実行します。

```bash
python3 scripts/public_source_integrity.py --directory .
python3 scripts/build_integral_server_distribution.py
```

公開manifestのないツリー、許可外ファイルや内容の不一致があるツリーからは生成しません。
配布物のソースは同梱の`PUBLIC_SOURCE_MANIFEST.json`の部分集合です。
生成する`BUILD_PROVENANCE.json`と`SHA256SUMS`に、出自・収録内容のハッシュを記録します。

- [API概要](docs/API.md)
- [C Client](docs/C_CLIENT.md)
- [サーバー運用](docs/INTEGRAL_SERVER_APPLICATION.md)
- [GB Runtime](runtimes/gb/README.md)
- [N64 Runtime](runtimes/n64/README.md)

## ライセンス

INTEGRAL EMULATORは複数のライセンスで構成されます。概要は[LICENSE](LICENSE)、Integral Emulator独自部分の適用範囲は[LICENSE_SCOPE.md](LICENSE_SCOPE.md)を参照してください。

第三者コードについては[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)および各third-partyコンポーネントに付属するライセンス・著作権表示を参照してください。

公開素材の範囲と由来は[ASSET_PROVENANCE.md](ASSET_PROVENANCE.md)、GNUライセンスの原文は[`LICENSES/`](LICENSES/)に収録しています。

## 注意

このプロジェクトは独立したプロジェクトであり、利用対象となるゲーム、ハードウェア、サービスまたは商標の権利者による公式製品・公式サービスではありません。
