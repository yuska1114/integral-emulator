# GB Runtime

GB Runtimeは、SameBoyを基盤とするINTEGRAL EMULATORのGame Boy／Game Boy Color向けRuntimeです。通常のゲーム実行、Link Cable通信、Mobile Modeを担当し、製品利用ではINTEGRAL EMULATOR Clientから起動されます。

## ディレクトリ構成

- `src/`: Runtime本体
- `assets/`: Runtimeで使用するリソース
- `patches/`: upstream sourceへのプロジェクト差分
- `test_fixtures/`: Runtime検証用fixture
- `third_party/`: upstream source

## ビルド

依存パッケージは[C Clientの文書](../../docs/C_CLIENT.md)を参照してください。プロジェクトルートで次を実行すると、Clientが使用するGB Runtimeをビルドできます。SameBoyとlibmobileは依存関係として自動的にビルドされます。

```bash
make -C runtimes/gb/src server mobile-runtime
make -C c_client
```

`make -C c_client`には、ROOMで使用するfixed-Host Runtimeのビルドも含まれます。

## 第三者ソース

GB RuntimeはSameBoyおよびlibmobileを利用します。

第三者ソースのライセンス、著作権表示、upstream READMEは`third_party/`に保持しています。収録revisionと検証方法は[第三者コードについて](../../THIRD_PARTY_NOTICES.md)を参照してください。
