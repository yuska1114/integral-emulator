# INTEGRAL EMULATOR

INTEGRAL EMULATORは、デスクトップクライアント、Integral Server、GB Runtime、N64 Runtimeを組み合わせたエミュレーター環境です。

GB RuntimeはGame Boy／Game Boy Colorの実行、Link Cable通信、Mobile Modeを担当します。N64 RuntimeはNINTENDO 64の実行とROOMセッションを担当します。ROMは利用者のPCに置き、SAVはIntegral Serverを正本として管理します。

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
sudo integral-server doctor
sudo integral-server start
curl http://127.0.0.1:8080/health
```

正常に起動していれば、`/health`から`{"ok": true}`が返ります。

設定、ユーザー発行、バックアップ等は[サーバー運用](docs/INTEGRAL_SERVER_APPLICATION.md)を参照してください。C ClientとRuntimeのビルドは[C Client](docs/C_CLIENT.md)、[GB Runtime](runtimes/gb/README.md)、[N64 Runtime](runtimes/n64/README.md)を参照してください。

## 開発用macOS配布の注意

開発・検証用のmacOS版は、Developer ID署名およびApple Notarizationを行っていません。ダウンロードしたreleaseには、macOSのquarantine属性が付与される場合があります。

Client、GB Runtime、N64 Runtime、helper binaryを個別にGatekeeperで許可する代わりに、releaseフォルダ直下の`unlock_macos.sh`を使って、そのフォルダ配下の`com.apple.quarantine`属性だけを一括解除できます。Terminalでreleaseフォルダを開き、内容を確認して信頼できる場合に限り、ユーザー自身で次を実行してください。管理者権限は不要です。

```sh
sh unlock_macos.sh
```

この操作の対象は、`unlock_macos.sh`が置かれているreleaseフォルダだけです。Gatekeeper全体を無効化するものではなく、quarantine以外の拡張属性も削除しません。

## テスト

Pythonテストと、固定した第三者ソースの整合性確認は次のコマンドで実行できます。

```bash
PYTHONPATH=src python3 -m unittest discover -s tests
python3 scripts/verify_third_party_lock.py
python3 scripts/verify_libmobile_dependency.py runtimes/gb/third_party/libmobile
```

個別コンポーネントのビルド・検証方法は各文書を参照してください。

## ドキュメント

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

このプロジェクトは独立した互換プロジェクトであり、利用対象となるゲーム、ハードウェア、サービスまたは商標の権利者による公式製品・公式サービスではありません。
