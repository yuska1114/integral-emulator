# ライセンスの適用範囲

Integral Emulatorの公開ソースには、複数のライセンスが適用されます。

## Integral Emulator独自部分

| 対象 | ライセンス |
| --- | --- |
| `src/integral_emulator/` | `AGPL-3.0-or-later` |
| `install.sh`、`pyproject.toml`、`deploy/`、`config/` | `AGPL-3.0-or-later` |
| `README.md`、`LICENSE_SCOPE.md`、`THIRD_PARTY_NOTICES.md`、`ASSET_PROVENANCE.md`、`THIRD_PARTY_LOCK.json`、`docs/` | `AGPL-3.0-or-later` |
| `tests/` | `AGPL-3.0-or-later` |
| `test_fixtures/` | 各fixtureの`PROVENANCE.md`と同梱ライセンス |
| `c_client/`の独自ファイル | `GPL-3.0-or-later` |
| `runtimes/gb/`の独自ファイル | `GPL-3.0-or-later` |
| `runtimes/n64/`の独自ファイル | `GPL-2.0-or-later` |
| `scripts/` | 各ファイルの`SPDX-License-Identifier` |

個別ファイルの`SPDX-License-Identifier`、同梱ライセンス、来歴表示は、上表のディレクトリ既定値に優先します。Runtime配下のfixtureにも同じ原則を適用します。patchのライセンスは、各ファイルのSPDX表記または`THIRD_PARTY_LOCK.json`の記録に従います。GNUライセンス原文や第三者ライセンス原文を、上表のライセンスへ変更するものではありません。

## 第三者コード

`third_party/`以下を含む第三者由来のコードや素材には、それぞれの上流プロジェクトのライセンスと著作権表示が適用されます。

詳細は`THIRD_PARTY_NOTICES.md`および各third-partyディレクトリに収録された`LICENSE`、`COPYING`、ファイルヘッダー等を参照してください。

第三者ファイルに記載された原文のライセンス・著作権表示が、本書の説明に優先します。

## SPDX表記について

`-or-later`は、記載されたバージョンまたはFree Software Foundationが公開する後続バージョンを選択できることを示します。

Integral Emulator独自部分の著作権者は、yuska（GitHub: @yuska1114）です。個別ファイルまたは第三者の表示に別の著作権者が記載されている場合は、その表示を維持します。
