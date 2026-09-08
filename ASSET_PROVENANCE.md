# 素材の由来

公開ソースには、Integral Emulatorの公式ブランド用ロゴ・アイコン、スクリーンショット、ROM、SAVを含めません。

公開ソースZIP内の機械的な収録一覧は`PUBLIC_SOURCE_MANIFEST.json`に記録されます。

## 公開版の共通アイコン

`assets/public/`以下のSVG、PNG、ICO、ICNS、BMPは、公開ビルド用の全面黒のプレースホルダーです。

## Runtime用素材

第三者由来のRuntime用バイナリ素材として個別に収録するものは、次の1件です。

| 素材 | 状態 | 生成元・確認方法 |
| --- | --- | --- |
| `runtimes/gb/assets/bootroms/sgb2_boot.bin` | SameBootのsourceから再生成可能 | `runtimes/gb/third_party/SameBoy/BootROMs/`、SameBoy `LICENSE`、`scripts/verify_sameboot_resource.py` |

SameBootのサイズは256 byte、SHA-256は`8a65465a9da7ec657726a671da9a85963cf19d2c975e499aea6b97eb37e0b6ea`です。再生成確認にはRGBDS 1.0.3が必要です。

## 合成テストデータ

`test_fixtures/gb_mobile/packages/synthetic_numbers_a/`と`synthetic_numbers_b/`は、公開テスト用に作成した合成データです。市販ゲーム由来のデータを含みません。由来とライセンスは、各ディレクトリの`PROVENANCE.md`と`LICENSES/`を参照してください。

third-partyソースに含まれる素材の扱いは、各コンポーネントの原文ライセンスと`THIRD_PARTY_NOTICES.md`に従います。
