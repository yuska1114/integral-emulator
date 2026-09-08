# 素材の由来

初回公開ソースには、Integral Emulatorの公式ブランドロゴ・公式ブランドアイコン、スクリーンショット、ROM、SAV、非公開の検証資料、未審査の画像・音声・フォントを含めません。公開ビルドに必要なブランド非依存の共通ダミーアイコンだけを収録します。

公開ソースZIP内の機械的な収録一覧は`PUBLIC_SOURCE_MANIFEST.json`に記録されます。

## 公開版の共通アイコン

`assets/public/integral_emulator_icon.svg`と、そこから生成したPNG、ICO、ICNS、BMPは、プロジェクト作者が2026年に作成した全面黒の無地画像です。図柄、ゲーム作品、第三者ブランド、公式版のロゴを含みません。ルートの`LICENSE`（GPL-3.0-or-later）に従って公開します。

生成手順は`assets/public/README.md`と`scripts/build_public_dummy_icons.py`、形式検査は`scripts/verify_app_icon_assets.py`に記載しています。公開ビルドではこの素材を既定値とし、非公開の公式ブランド画像は環境変数で明示した場合だけ使用します。

## Runtime用素材

第三者由来のRuntime用バイナリ素材として個別に収録するものは、次の1件です。

| 素材 | 状態 | 生成元・確認方法 |
| --- | --- | --- |
| `runtimes/gb/assets/bootroms/sgb2_boot.bin` | SameBootのsourceから再生成可能 | `runtimes/gb/third_party/SameBoy/BootROMs/`、SameBoy `LICENSE`、`scripts/verify_sameboot_resource.py` |

SameBootのサイズは256 byte、SHA-256は`8a65465a9da7ec657726a671da9a85963cf19d2c975e499aea6b97eb37e0b6ea`です。再生成確認にはRGBDS 1.0.3が必要です。

## 合成テストデータ

`test_fixtures/gb_mobile/packages/synthetic_numbers_a/`と`synthetic_numbers_b/`は、公開テスト用に作成した合成データです。市販ゲーム由来のデータを含みません。由来とライセンスは、各ディレクトリの`PROVENANCE.md`と`LICENSES/`を参照してください。

third-partyソースに含まれる素材の扱いは、各コンポーネントの原文ライセンスと`THIRD_PARTY_NOTICES.md`に従います。
