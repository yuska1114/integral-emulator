# SameBoy用patch

`sameboy-integral-rtc-and-windows-build.patch`は、SameBoy `v1.0.3`（upstream baseline `208ba4afabffab9edde416f2dbb8ae459e34adb8`）に対するINTEGRAL EMULATOR向けの差分です。

主に、Server時刻との差分をRTCへ反映するための機能と、Windows/MSYS2向けstatic Core build対応を追加します。

patchの適用順、SHA-256、upstream treeとの対応はプロジェクトルートの`THIRD_PARTY_LOCK.json`に記録しています。プロジェクトルートで次を実行すると検証できます。

```bash
python3 scripts/verify_third_party_lock.py
```

このpatch自体はMIT Licenseで提供します。SameBoy由来部分の著作権表示とlicenseは、上流のものを維持します。
