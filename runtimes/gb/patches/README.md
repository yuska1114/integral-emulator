# SameBoy用patch

SameBoy `v1.0.3`（upstream baseline `208ba4afabffab9edde416f2dbb8ae459e34adb8`）に対するINTEGRAL EMULATOR向けのmaterialized patchを、次の順で適用します。

1. `sameboy-integral-rtc-and-windows-build.patch`
   - Server時刻との差分をRTCへ反映する機能
   - Windows/MSYS2向けstatic Core build対応
2. `sameboy-integral-ir-release-delay.patch`
   - IR受信OFF遅延の設定機能
3. `sameboy-integral-sgb-game-border.patch`
   - ゲーム側SGBボーダーが有効になったかをRuntimeから判定する補助API

patchの適用順、SHA-256、upstream treeとの対応はプロジェクトルートの`THIRD_PARTY_LOCK.json`に記録しています。プロジェクトルートで次を実行するとローカルの公開subsetとpatch hashを検証できます。

```bash
python3 scripts/verify_third_party_lock.py
```

upstream checkoutを用意した場合は、materialized patchを順に適用してvendored treeを再現できることも検証できます。

```bash
python3 scripts/verify_third_party_lock.py --upstream-root <upstream-root>
```

これらのpatch自体はMIT Licenseで提供します。SameBoy由来部分の著作権表示とlicenseは、上流のものを維持します。
