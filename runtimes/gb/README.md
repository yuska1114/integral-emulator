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

## ウィンドウ表示

C ClientとGBのゲーム画面は、ウィンドウの端をドラッグして自由に大きさを変更できます。
C Clientから起動する通常のGB、Mobile Mode、Link Cable ROOMのHost／Remote画面は、
起動時のClientと同じ外枠サイズを使用します。起動後はそれぞれ独立して変更できます。
GB映像はアスペクト比と整数倍率を保った最近傍補間で中央表示し、余った領域は黒い余白に
なります。OSで選択中のキーボード入力方式は変更しません。

SGB2用の起動処理は通常どおり実行しますが、その起動中の映像と音声はClientへ出力せず、
ゲーム画面へ移るまで黒画面・無音になります。SGBコマンドやカラーパレットの処理は省略しません。

Mobile ModeはLOCALと同じサーバー時刻補正をRTCへ適用します。終了確認から戻った場合は
バッテリーSAVを書き出し、正常終了結果とSAVのサイズ・hashが一致した場合だけClientが
サーバーへ反映します。入力にはClientのSLOT 1と共通操作の割当を使用しますが、倍速操作は
無効です。SERVER2では同じROMを2つの
登録枠から起動できますが、SLOT1とSLOT2にはそれぞれ異なるSAVが必要です。

## 第三者ソース

GB RuntimeはSameBoyおよびlibmobileを利用します。

第三者ソースのライセンス、著作権表示、upstream READMEは`third_party/`に保持しています。収録revisionと検証方法は[第三者コードについて](../../THIRD_PARTY_NOTICES.md)を参照してください。
