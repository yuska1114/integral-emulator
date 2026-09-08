# SameBoot SGB2 Runtime リソース

`sgb2_boot.bin`は、SameBoy 1.0.3の`runtimes/gb/third_party/SameBoy/BootROMs/sgb2_boot.asm`と、その参照ソースから生成したSGB2用SameBootです。

これらのソースと生成されたバイナリには、`runtimes/gb/third_party/SameBoy/LICENSE`に記載されたExpat Licenseが適用されます。

preferred sourceを構成するファイル一式は、`sgb2_boot.asm`、`sgb_boot.asm`、`sameboot.inc`、`hardware.inc`、およびSameBoyルートの`LICENSE`と`version.mk`です。

RGBDS 1.0.3を用意し、プロジェクトルートで次を実行すると、一時ディレクトリに再生成して収録済みバイナリと照合できます。

```sh
python3 scripts/verify_sameboot_resource.py
```

- ソース一式のSHA-256: `742fef97deb37853124055a488981d96dd7a43e35eb1d8caf09afd0264a5b264`
- 期待されるサイズ: 256 bytes
- SHA-256: `8a65465a9da7ec657726a671da9a85963cf19d2c975e499aea6b97eb37e0b6ea`

GB Runtimeは、SGB2インスタンスの起動前にサイズとSHA-256を検証します。

リソースが存在しない、または検証に失敗した場合は、SGB2インスタンスを起動しません。
