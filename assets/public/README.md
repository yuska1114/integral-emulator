# 公開版の共通アイコン

`assets/public/`以下のSVG、PNG、ICO、ICNS、BMPは、公開ビルド用の全面黒の
プレースホルダーです。

PNG、ICO、ICNS、BMPはSVGと同じ図形定義から生成した配布用ファイルです。再生成には
Python 3とPillowを使用します。

```bash
python3 -m pip install Pillow==10.4.0
python3 scripts/build_public_dummy_icons.py
python3 scripts/verify_app_icon_assets.py assets/public
```

生成後は、上記の検証に加えてGit差分を確認してください。
