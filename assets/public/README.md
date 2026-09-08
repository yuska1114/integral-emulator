# 公開版の共通アイコン

`integral_emulator_icon.svg`は、公開ソースと公開ビルドで使用するブランド非依存の
共通プレースホルダーです。文字、ロゴ、図柄を一切含まない全面黒の画像です。

PNG、ICO、ICNS、BMPはSVGと同じ図形定義から生成した配布用ファイルです。再生成には
Python 3とPillowを使用します。

```bash
python3 -m pip install Pillow==10.4.0
python3 scripts/build_public_dummy_icons.py
python3 scripts/verify_app_icon_assets.py assets/public
```

生成後は、上記の検証に加えてGit差分を確認してください。これらの素材は
プロジェクト作者が2026年に作成したもので、ルートの`LICENSE`
（GPL-3.0-or-later）に従って公開します。

公開ビルドではこのアイコンが既定です。非公開の公式ブランド画像を使用する場合だけ、
配布ビルド時に次の環境変数へ対応形式の絶対パスを明示します。

- `INTEGRAL_EMULATOR_ICON_PNG`
- `INTEGRAL_EMULATOR_ICON_ICO`
- `INTEGRAL_EMULATOR_ICON_ICNS`
- `INTEGRAL_EMULATOR_ICON_BMP`

指定した画像が存在しない場合や形式が不正な場合、配布ビルドは失敗します。
