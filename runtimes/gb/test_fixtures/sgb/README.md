# SGB色表示テストROM

このCC0 fixtureは、`sgb_color_test.asm`からRGBDS 1.0.3を使用して生成します。

SGB対応header、パレット設定、画面左右への属性設定、一定間隔のパレット切替を確認するためのROMです。

プロジェクトルートで次を実行すると、`runtimes/gb/build_exp/test-fixtures/sgb_color_test.gb`を生成します。

```sh
make -C runtimes/gb/src sgb-fixture
```

生成したROMは公開ソースに含めません。ライセンスは同ディレクトリの`LICENSE`を参照してください。
